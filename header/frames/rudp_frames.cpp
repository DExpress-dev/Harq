
#include <mutex>
#include <cmath>
#include "rudp_frames.h"
#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

frames_class::frames_class(service_mode mode){
	last_static_timer_ = time(nullptr);
	set_service_mode(mode);
}

void frames_class::init(bool delay, int delay_interval, int cumulative_timer){
	delay_ = delay;
	delay_interval_ = delay_interval;
	cumulative_timer_ = cumulative_timer;
	last_check_static_timer_ = global_rudp_timer_.get_current_timer();
	last_static_timer_ = time(nullptr);
	thread_ptr_ = std::thread(&frames_class::execute, this);
	thread_ptr_.detach();
}

void frames_class::set_base_timer(rudptimer remote_timer, rudptimer local_timer){
	remote_base_timer_ = remote_timer;
	local_base_timer_ = local_timer;
}	

frames_class::~frames_class(){
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
	free_complete_frames();
}

void frames_class::add_queue(std::shared_ptr<frames_record> frames_ptr){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	frames_class_buffer *buffer_ptr = new frames_class_buffer;
	buffer_ptr->next_ = nullptr;
	buffer_ptr->frames_ptr_ = frames_ptr;
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}

void frames_class::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	frames_class_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void frames_class::frames_static(){
	time_t curr_time = time(nullptr);
	int second = abs(static_cast<int>(difftime(curr_time, last_static_timer_)));
	if(second >= 1){
		//add_log(LOG_TYPE_INFO, "frames_static current_frames_no=%d", get_current_frames_no());
		last_static_timer_ = time(nullptr);
	}
}

void frames_class::set_service_mode(const service_mode &mode){
	service_mode_ = mode;
}
	
service_mode frames_class::get_service_mode(){
	return service_mode_;
}

void frames_class::dispense(){
	frames_class_buffer *work_ptr = nullptr;
	frames_class_buffer *next_ptr = nullptr;
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
		add_complete_frames(work_ptr->frames_ptr_);
		delete work_ptr;
		work_ptr = next_ptr;
	}
}

void frames_class::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		dispense();
		check_frames();
		frames_static();
		//check_cumulative_timer();
		ustd::rudp_public::sleep_delay(FRAME_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void frames_class::set_frames_no(const uint64 &frames_no){
	std::lock_guard<std::recursive_mutex> gurad(complete_frames_no_lock_);
	current_frames_no_ = frames_no;
	current_frames_no_timer_ = global_rudp_timer_.get_current_timer();
}

uint64 frames_class::get_current_frames_no(){
	std::lock_guard<std::recursive_mutex> gurad(complete_frames_no_lock_);
	return current_frames_no_;
}

rudptimer frames_class::get_current_frames_no_timer(){
	std::lock_guard<std::recursive_mutex> gurad(complete_frames_no_lock_);
	return current_frames_no_timer_;
}

bool frames_class::exists_frames(uint64 frames_no, rudptimer &frame_timer){
	frames_map::iterator iter = complete_frames_map_.find(frames_no);
	if(iter != complete_frames_map_.end()){
		std::shared_ptr<frames_record> frames_ptr = iter->second;
		frame_timer = frames_ptr->frames_timer_;
		return true;
	}
	return false;
}

std::shared_ptr<frames_record> frames_class::pop_complete_frames(const uint64 &frames_no){
	frames_map::iterator iter = complete_frames_map_.find(frames_no);
	if(iter != complete_frames_map_.end()){
		std::shared_ptr<frames_record> frames_ptr = iter->second;
		complete_frames_map_.erase(iter);
		return frames_ptr;
	}
	return nullptr;
}

std::shared_ptr<frames_record> frames_class::find_complete_frames(const uint64 &frames_no){
	frames_map::iterator iter = complete_frames_map_.find(frames_no);
	if(iter != complete_frames_map_.end()){
		std::shared_ptr<frames_record> frames_ptr = iter->second;
		return frames_ptr;
	}
	return nullptr;
}

void frames_class::free_complete_frames(){
	for(auto iter = complete_frames_map_.begin(); iter != complete_frames_map_.end(); iter++){
		std::shared_ptr<frames_record> frames_ptr = iter->second;
		if (frames_ptr != nullptr){
			frames_ptr->packet_map_.clear();
		}
	}
	complete_frames_map_.clear();
}

void frames_class::add_complete_frames(std::shared_ptr<frames_record> frames_ptr){
	if(nullptr == frames_ptr)
		return;

	rudptimer frame_timer;
	if(exists_frames(frames_ptr->frames_no_, frame_timer))
		return;

	if(frames_ptr->frames_no_ <= get_current_frames_no())
		return;

	complete_frames_map_.insert(std::make_pair(frames_ptr->frames_no_, frames_ptr));
}

void frames_class::check_frames(){
	if (complete_frames_map_.empty())
		return;

	while(true){
		//得到当前帧号;
		uint64 current_frames_no = get_current_frames_no() + 1;
		//判断是否存在;
		rudptimer frames_timer;
		bool exists = exists_frames(current_frames_no, frames_timer);
		if(!exists)
			return;

		//判断是否延时;
		if(!delay_){
			std::shared_ptr<frames_record> frames_ptr = pop_complete_frames(current_frames_no);
			if(nullptr != frames_ptr){
				pop_frames(frames_ptr);
			}
			continue;
		}else{
			//设置起始时间;
			if(INIT_SEGMENT_INDEX + 1 == current_frames_no && 0 == local_first_timer_ && 0 == remote_first_timer_){
				local_first_timer_ = global_rudp_timer_.get_current_timer();
				remote_first_timer_ = frames_timer;
				return;
			}
			//判断是否开始;
			if(!start_){
				int local_timer_interval = static_cast<int>(global_rudp_timer_.timer_interval(global_rudp_timer_.get_current_timer(), local_first_timer_));
				if(delay_interval_ <= local_timer_interval){
					start_ = true;
				}
				return;
			}else{
				//判断时间间隔;
				int local_timer_interval = static_cast<int>(global_rudp_timer_.timer_interval(global_rudp_timer_.get_current_timer(), local_first_timer_));
				int remote_timer_interval = static_cast<int>(global_rudp_timer_.timer_interval(frames_timer, remote_first_timer_));
				if((local_timer_interval - delay_interval_) + cumulative_timer_ < remote_timer_interval)
					return;

				std::shared_ptr<frames_record> frames_ptr = pop_complete_frames(current_frames_no);
				if (nullptr != frames_ptr){
					pop_frames(frames_ptr);
					continue;
				}
			}
		}
	}
}

// void frames_class::handle_recv(rudptimer frame_timer, rudptimer recv_timer, char *data, int size)
// {
// 	if(nullptr != on_handle_recv_)
// 	{
// 		/*这里需要计算整体消耗时间;
// 		消耗时间算法：
		
// 		求本帧时间（对端时间）相对于起始时间（对端时间）的差值：
// 		本帧时间：A2
// 		起始时间：A1
// 		A2 - A1 = A`

// 		求本帧接收时间差值;
// 		B2 本帧接收时间
// 		B2 - A1 = B`

// 		求传输时间
// 		B` - A` = C
// 		*/

// 		//求出远端时间差值;
// 		rudptimer frame_remote_timer_interval = frame_timer - remote_base_timer_;
// 		//求出近端时间差值;
// 		rudptimer frame_local_timer_interval = recv_timer - local_base_timer_;

// 		//求传输耗时;
// 		rudptimer rudp_consume_timer = (frame_remote_timer_interval - frame_local_timer_interval) + handle_first_rto() / 2;

// 		//设置消耗时间;
// 		set_cumulative_timer(rudp_consume_timer);

// 		//求处理耗时
// 		rudptimer rudp_deal_consume_timer = 0;
// 		rudp_deal_consume_timer = global_rudp_timer_.get_current_timer() - recv_timer;
// 		set_cumulative_timer(rudp_deal_consume_timer);
// 		on_handle_recv_(data, size, rudp_consume_timer);
// 	}
// }

void frames_class::handle_recv(rudptimer frame_timer, rudptimer recv_timer, char *data, int size){
	if(nullptr != on_handle_recv_){
		/*这里需要计算整体消耗时间;
		消耗时间算法：
		
		假设两个服务器时间一致时，传输时间 = 接收到的时间 - 帧发送时间
		假设两个服务器时间不一致时，传输时间 = 接收时间 - 帧发送时间 - 系统时间差
		*/
		//求传输耗时;
		rudptimer rudp_consume_timer = recv_timer - frame_timer;
		if(SERVER == get_service_mode())
			rudp_consume_timer -= handle_sys_server_timer_stamp();
		else if(CLIENT == get_service_mode())
			rudp_consume_timer -= handle_sys_client_timer_stamp();

		//设置消耗时间;
		set_cumulative_timer(rudp_consume_timer);
		//求处理耗时
		rudptimer rudp_deal_consume_timer = 0;
		rudp_deal_consume_timer = global_rudp_timer_.get_current_timer() - recv_timer;
		set_cumulative_timer(rudp_deal_consume_timer);
		on_handle_recv_(data, size, static_cast<int>(rudp_consume_timer));
	}
}

void frames_class::init_cumulative_timer(){
	min_cumulative_timer_ = 10000;
	max_cumulative_timer_ = 0;
	total_cumulative_timer_ = 0;
	frequency_ = 0;
	deal_min_cumulative_timer_ = 10000;
	deal_max_cumulative_timer_ = 0;
}

void frames_class::get_cumulative_timer(rudptimer *min_timer, rudptimer *max_timer, rudptimer *average_timer){
	*min_timer = get_min_cumulative_timer();
	*max_timer = get_max_cumulative_timer();
	*average_timer = get_average_cumulative_timer();
	init_cumulative_timer();
}

void frames_class::check_cumulative_timer(){
	rudptimer current_timer = global_rudp_timer_.get_current_timer();
	if(current_timer - last_check_static_timer_ >= 1000){
		add_log(LOG_TYPE_INFO, "check_cumulative_timer min=%lld max=%lld deal_min=%lld deal_max=%lld", 
			get_min_cumulative_timer(), get_max_cumulative_timer(), get_deal_min_cumulative_timer(), get_deal_max_cumulative_timer());
		init_cumulative_timer();
		last_check_static_timer_ = global_rudp_timer_.get_current_timer();
	}
}

void frames_class::handle_recv_useful(int size){
	if(nullptr != on_handle_recv_useful_){
		on_handle_recv_useful_(size);
	}
}

uint64 frames_class::handle_first_rto(){
	if(nullptr != on_first_rto_){
		return on_first_rto_();
	}
	return 0;
}

int32 frames_class::handle_sys_server_timer_stamp(){
	if(nullptr != on_sys_server_timer_stamp_){
		return on_sys_server_timer_stamp_();
	}
	return 0;
}
	
int32 frames_class::handle_sys_client_timer_stamp(){
	if(nullptr != on_sys_client_timer_stamp_){
		return on_sys_client_timer_stamp_();
	}
	return 0;
}

void frames_class::pop_frames(std::shared_ptr<frames_record> frames_ptr){
	set_frames_no(frames_ptr->frames_no_);
	char *frames_data = new char[frames_ptr->frames_size_];
	memset(frames_data, 0, frames_ptr->frames_size_);
	uint64 postion = 0;
	for(auto packet_iter = frames_ptr->packet_map_.begin(); packet_iter != frames_ptr->packet_map_.end(); packet_iter++){
		std::shared_ptr<packet_record> packet_ptr = packet_iter->second;
		if(nullptr == packet_ptr){
			delete[] frames_data;
			return;
		}
		if(postion + packet_ptr->size_ > frames_ptr->frames_size_){
			delete[] frames_data;
			return;
		}
		memcpy(frames_data + postion, packet_ptr->buffer_, packet_ptr->size_);
		postion = postion + packet_ptr->size_;
		//这里使用了增加固定数值FRAMES_BUFFER_SIZE，因为packet中有些数值不被使用，但是确实被传输的；
		handle_recv_useful(FRAMES_BUFFER_SIZE);
	}
	frames_ptr->packet_map_.clear();
	//这里增加判断，有可能此帧全是被计算出来的, 对于计算出来的帧设置帧的起始时间和接收时间均为0;
	if(LLONG_MAX == frames_ptr->frames_timer_ || LLONG_MIN == frames_ptr->recv_timer_){
		handle_recv(0, 0, frames_data, frames_ptr->frames_size_);	
	}else{
		handle_recv(frames_ptr->frames_timer_, frames_ptr->recv_timer_, frames_data, frames_ptr->frames_size_);
	}
	delete[] frames_data;
}

uint64 frames_class::get_complete_max_frames_no(){
	if(complete_frames_map_.empty())
		return 0;

	frames_map::reverse_iterator iter = complete_frames_map_.rbegin();
	uint64 max_frames_no = (iter)->first;
	return max_frames_no;
}

uint64 frames_class::get_complete_min_frames_no(){
	if(complete_frames_map_.empty())
		return 0;

	frames_map::iterator iter = complete_frames_map_.begin();
	uint64 min_frames_no = (iter)->first;
	return min_frames_no;
}

void frames_class::get_frames_min_max_index(uint64 frames_no, uint64 &min_index, uint64 &max_index){
	std::shared_ptr<frames_record> frames_ptr = find_complete_frames(frames_no);
	if(frames_ptr != nullptr){
		if(!frames_ptr->packet_map_.empty()){
			max_index = (frames_ptr->packet_map_.rbegin())->first;
			min_index = (frames_ptr->packet_map_.begin())->first;
			return;
		}
	}
	min_index = 0;
	max_index = 0;
}

void frames_class::set_cumulative_timer(const rudptimer &cumulative_timer){
	if(min_cumulative_timer_ >= cumulative_timer){
		min_cumulative_timer_ = cumulative_timer;
	}
	if(max_cumulative_timer_ <= cumulative_timer){
		max_cumulative_timer_ = cumulative_timer;
	}
	frequency_++;
	total_cumulative_timer_ += cumulative_timer;
}

void frames_class::set_deal_cumulative_timer(const rudptimer &cumulative_timer){
	if(deal_min_cumulative_timer_ >= cumulative_timer){
		deal_min_cumulative_timer_ = cumulative_timer;
	}
	if(deal_max_cumulative_timer_ <= cumulative_timer){
		deal_max_cumulative_timer_ = cumulative_timer;
	}
}

rudptimer frames_class::get_average_cumulative_timer(){
	if(frequency_ != 0)
		return total_cumulative_timer_ / frequency_;
	else
		return 0;
}

void frames_class::add_log(const int log_type, const char *context, ...){
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



