
#include <math.h>
#include <map>
#include <algorithm>
#include <climits>

#include "../include/rudp_def.h"
#include "../include/rudp_timer.h"

#include "../fec/matrix.h"
#include "../channel/recv_channel.h"

recv_channel::recv_channel(){
	last_check_packet_static_timer_ = time(nullptr);
	estimator_ptr_ = new trendline_estimator();
}

recv_channel::~recv_channel(){
	if(current_state_ == tst_runing){
		current_state_ = tst_stoping;
		time_t last_timer = time(nullptr);
		int timer_interval = 0;
		while((timer_interval <= KILL_THREAD_TIMER)){
			time_t current_timer = time(nullptr);
			timer_interval = static_cast<int>(difftime(current_timer, last_timer));
			if(current_state_ == tst_stoped){
				break;
			}
			ustd::rudp_public::sleep_delay(DESTROY_TIMER, Millisecond);
		}
	}
	free_queue();
	free_packet();
	//释放带宽控制;
	if(estimator_ptr_ != nullptr){
		delete estimator_ptr_;
		estimator_ptr_ = nullptr;
	}
}

void recv_channel::handle_add_log(const int log_type, const char *context){
	if (on_add_log_ != nullptr){
		on_add_log_(log_type, context);
	}
}

void recv_channel::init(){
	last_check_packet_static_timer_ = time(nullptr);
	if(estimator_ptr_ != nullptr){
		estimator_ptr_->write_log_ptr_ = write_log_ptr_;
		estimator_ptr_->init();
	}
	thread_ptr_ = std::thread(&recv_channel::execute, this);
	thread_ptr_.detach();
}

void recv_channel::add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	struct frames_buffer *frames_ptr = (struct frames_buffer *)data;
	recv_channel_buffer *buffer_ptr = new recv_channel_buffer;
	buffer_ptr->next_ = nullptr;
	std::shared_ptr<packet_record> packet_ptr(new packet_record);
	packet_ptr->segment_index_ = frames_ptr->header_.index_;
	packet_ptr->frames_no_ = frames_ptr->body_.frames_no_;
	packet_ptr->packet_count_ = frames_ptr->body_.packet_count_;
	packet_ptr->packet_no_ = frames_ptr->body_.packet_no_;
	packet_ptr->frames_tiemr_ = frames_ptr->body_.frames_timer_;
	packet_ptr->recv_tiemr_ = recv_timer;
	packet_ptr->size_ = frames_ptr->body_.size_;
	packet_ptr->send_count_ = frames_ptr->body_.send_count_;
	packet_ptr->fec_created_ = fec_created;
	memset(packet_ptr->buffer_, 0, sizeof(packet_ptr->buffer_));
	if (packet_ptr->size_ > 0){
		memcpy(packet_ptr->buffer_, frames_ptr->body_.data_, packet_ptr->size_);
	}
	buffer_ptr->packet_ptr_ = packet_ptr;
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
	if(estimator_ptr_ != nullptr && !fec_created){
		estimator_ptr_->add_queue(packet_ptr->segment_index_, packet_ptr->size_, packet_ptr->frames_tiemr_, packet_ptr->recv_tiemr_);
	}
}

void recv_channel::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	recv_channel_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void recv_channel::set_complete_index(uint64 index){
	std::lock_guard<std::recursive_mutex> gurad(index_lock_);
	complete_index_ = index;
}				
	
uint64 recv_channel::get_complete_index(){
	std::lock_guard<std::recursive_mutex> gurad(index_lock_);
	return complete_index_;
}

void recv_channel::dispense(){
	recv_channel_buffer *work_ptr = nullptr;
	recv_channel_buffer *next_ptr = nullptr;
	{
		std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
		if(work_ptr == nullptr && first_ != nullptr){
			work_ptr = first_;
			first_ = nullptr;
			last_ = nullptr;
		}
	}
	while(work_ptr != nullptr){
		next_ptr = work_ptr->next_;
		add_packet(work_ptr->packet_ptr_);
		delete work_ptr;
		work_ptr = next_ptr;
	}
}

void recv_channel::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		dispense();
		check_packet();
		check_packet_static();
		ustd::rudp_public::sleep_delay(CHANNEL_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void recv_channel::handle_add_frames(std::shared_ptr<frames_record> frames_ptr){
	if(nullptr != on_add_frames_){
		on_add_frames_(frames_ptr);
	}
}

void recv_channel::check_packet(){
	if (segment_packet_map_.empty())
		return;

	while(true){
		uint64 current_index = get_complete_index() + 1;
		std::shared_ptr<packet_record> packet_ptr = find_packet(current_index);
		if(nullptr == packet_ptr)
			return;

		uint64 frames_no = packet_ptr->frames_no_;
		uint16 packet_count = packet_ptr->packet_count_;
		uint16 packet_no = packet_ptr->packet_no_;
		uint64 index = packet_ptr->segment_index_;
		uint64 begin_index = index - (packet_no - 1);
		uint64 end_index = begin_index + (packet_count - 1);
		for(uint64 i = begin_index; i <= end_index; i++){
			if(!exists_packet(i)){
				return;
			}
		}
		//取出此帧下的所有包;
		std::shared_ptr<frames_record> frames_ptr(new frames_record);
		frames_ptr->frames_no_ = frames_no;
		frames_ptr->packet_count_ = packet_count;
		frames_ptr->frames_timer_ = LLONG_MAX;
		frames_ptr->recv_timer_ = LLONG_MIN;
		frames_ptr->frames_size_ = 0;
		frames_ptr->frames_max_index_ = 0;
		for(uint64 i = begin_index; i <= end_index; i++){
			std::shared_ptr<packet_record> packet_ptr = pop_packet(i);
			if(nullptr != packet_ptr){
				if(!packet_ptr->fec_created_){
					//设置此帧的产生时间（取此帧下的所有包的帧时间最小值）
					frames_ptr->frames_timer_ = (std::min)(frames_ptr->frames_timer_, packet_ptr->frames_tiemr_);
					//设置此帧的接收时间（取此帧下所有包接收时间的最大值）
					frames_ptr->recv_timer_ = (std::max)(frames_ptr->recv_timer_, packet_ptr->recv_tiemr_);
				}
				frames_ptr->frames_max_index_ = (std::max)(frames_ptr->frames_max_index_, packet_ptr->segment_index_);
				frames_ptr->packet_map_.insert(std::make_pair(packet_ptr->packet_no_, packet_ptr));
				frames_ptr->frames_size_ += packet_ptr->size_;
			}
			set_complete_index(packet_ptr->segment_index_);
		}
		handle_add_frames(frames_ptr);
		delete_packets(frames_ptr->frames_no_);
	}
}

void recv_channel::add_packet(std::shared_ptr<packet_record> packet_ptr){
	if(packet_ptr->segment_index_ <= get_complete_index()){
		inc_loss_packet_size();
		return;
	}
	if(exists_packet(packet_ptr->segment_index_)){
		inc_loss_packet_size();
		return;
	}
	if(packet_ptr->send_count_ != 0 && !packet_ptr->fec_created_){
		inc_loss_packet_size();
	}
	segment_packet_map_.insert(std::make_pair(packet_ptr->segment_index_, packet_ptr));
	inc_use_packet_size();
}

void recv_channel::delete_packets(const uint64 &frames_no){
	for(auto iter = segment_packet_map_.begin(); iter != segment_packet_map_.end();){
		if((iter->second)->frames_no_ >= frames_no)
			return;

		iter = segment_packet_map_.erase(iter);
	}
}

bool recv_channel::exists_packet(const uint64 &index){
	segment_packet_map::iterator iter = segment_packet_map_.find(index);
	if(iter != segment_packet_map_.end())
		return true;
	else
		return false;
}

std::shared_ptr<packet_record> recv_channel::find_packet(const uint64 &index){
	segment_packet_map::iterator iter = segment_packet_map_.find(index);
	if(iter != segment_packet_map_.end())
		return iter->second;

	return nullptr;
}

std::shared_ptr<packet_record> recv_channel::pop_packet(const uint64 &index){
	segment_packet_map::iterator iter = segment_packet_map_.find(index);
	if(iter != segment_packet_map_.end()){
		std::shared_ptr<packet_record> packet_ptr = iter->second;
		segment_packet_map_.erase(iter);
		return packet_ptr;
	}
	return nullptr;
}

void recv_channel::free_packet(){
	segment_packet_map_.clear();
}

uint64 recv_channel::get_min_index(){
	if(segment_packet_map_.empty())
		return 0;

	segment_packet_map::iterator iter = segment_packet_map_.begin();
	return (iter)->first;
}

uint64 recv_channel::get_max_index(){
	if(segment_packet_map_.empty())
		return 0;

	segment_packet_map::reverse_iterator iter = segment_packet_map_.rbegin();
	return (iter)->first;
}

void recv_channel::inc_loss_packet_size(){
	std::lock_guard<std::recursive_mutex> gurad(packet_static_lock_);
	loss_packet_size_++;
}
	
void recv_channel::inc_use_packet_size(){
	std::lock_guard<std::recursive_mutex> gurad(packet_static_lock_);
	use_packet_size_++;
}

uint32 recv_channel::loss_packet_count()
{
	std::lock_guard<std::recursive_mutex> gurad(packet_static_lock_);
	return loss_packet_size_;
}
	
uint32 recv_channel::recv_packet_count(){
	std::lock_guard<std::recursive_mutex> gurad(packet_static_lock_);
	return loss_packet_size_ + use_packet_size_;
}

uint32 recv_channel::loss_packet_interval(){
	std::lock_guard<std::recursive_mutex> gurad(packet_static_lock_);
	return loss_packet_size_ - last_loss_packet_size_;
}
	
uint32 recv_channel::recv_packet_interval(){
	std::lock_guard<std::recursive_mutex> gurad(packet_static_lock_);
	return (loss_packet_size_ + use_packet_size_) - last_use_packet_size_;
}

void recv_channel::check_packet_static(){
	time_t curr_time = time(nullptr);
	int second = abs(static_cast<int>(difftime(curr_time, last_check_packet_static_timer_)));
	if(second >= INFO_SHOW_INTERVAL){
		last_loss_packet_size_ = loss_packet_size_;
		last_use_packet_size_ = use_packet_size_;
		last_check_packet_static_timer_ = time(nullptr);
	}
}

void recv_channel::add_log(const int log_type, const char *context, ...){
	const int array_length = 10 * KB;
	char log_text[array_length];
	memset(log_text, 0x00, array_length);
	va_list arg_ptr;
	va_start(arg_ptr, context);
	int result = harq_vsprintf(log_text, context, arg_ptr);
	va_end(arg_ptr);
	if (result <= 0)
		return;

	if (result > array_length)
		return;

	if (nullptr != write_log_ptr_){
		write_log_ptr_->write_log3(log_type, log_text);
	}
}

