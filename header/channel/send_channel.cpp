
#include "send_channel.h"
#include <math.h>
#include <cmath>
#include "rudp_error.h"
#include "linefit/polyfit.h"

//*******************
fec_encoder_thread::fec_encoder_thread(send_channel *parent){
	send_channel_ = parent;
	fec_encoder_ptr_ = new fec_encoder();
	fec_encoder_ptr_->init_encode();
}

void fec_encoder_thread::init(){
	thread_ptr_ = std::thread(&fec_encoder_thread::execute, this);
	thread_ptr_.detach();
}

fec_encoder_thread::~fec_encoder_thread(void){
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
	delete fec_encoder_ptr_;
	fec_encoder_ptr_ = nullptr;
}

void fec_encoder_thread::add_queue(frames_buffer* frames_buffer_ptr, struct sockaddr_in addr){
	std::lock_guard<std::recursive_mutex> gurad(fec_lock_);
	fec_buffer* buffer_ptr = new fec_buffer;
	buffer_ptr->frames_buffer_ = send_channel_->alloc_buffer();
	memcpy(buffer_ptr->frames_buffer_, frames_buffer_ptr, sizeof(frames_buffer));
	buffer_ptr->addr_ = addr;
	buffer_ptr->next_ = nullptr;
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}

void fec_encoder_thread::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(fec_lock_);
	fec_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void fec_encoder_thread::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		check_encoder();
		ustd::rudp_public::sleep_delay(FEC_ENCODE_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void fec_encoder_thread::check_encoder(){
	fec_buffer* work_ptr = nullptr;
	fec_buffer* next_ptr = nullptr;
	{
		std::lock_guard<std::recursive_mutex> gurad(fec_lock_);
		if(work_ptr == nullptr && first_ != nullptr){
			work_ptr = first_;
			first_ = nullptr;
			last_ = nullptr;
		}
	}
	while(work_ptr != nullptr){
		next_ptr = work_ptr->next_;
		//计算FEC冗余包
		fec_encoder_ptr_->add_frames_buffer(work_ptr->frames_buffer_);
		if(fec_encoder_ptr_->get_encoder_size() >= FEC_GROUP_SIZE){
			fec_encoder_ptr_->encode_data();
			send_error_packet(work_ptr->frames_buffer_->header_.index_, work_ptr->addr_);
			fec_encoder_ptr_->init_encode();
		}
		if(work_ptr->frames_buffer_ != nullptr){
			send_channel_->release_buffer(work_ptr->frames_buffer_);
		}
		delete work_ptr;
		work_ptr = next_ptr;
	}
}

void fec_encoder_thread::send_error_packet(const uint64 &index, const struct sockaddr_in &addr){
	uint8 postion  = 0;
	for(auto iter = fec_encoder_ptr_->encode_vector_.begin(); iter != fec_encoder_ptr_->encode_vector_.end(); iter++){
		error_buffer error_ptr;
		memset(&error_ptr, 0, sizeof(error_ptr));
		error_ptr.header_.message_type_ = message_error;
		error_ptr.header_.group_index_ = postion;
		postion++;
		error_ptr.header_.index_ = ustd::rudp_public::get_group_id(index);
		memcpy(error_ptr.data_, (*iter)->buffer_, (*iter)->size_);
		send_channel_->handle_fec_finished(message_error, 0, (char*)&error_ptr, sizeof(error_ptr), addr);
		send_channel_->handle_on_send_overall(sizeof(error_buffer));
    }
}

void fec_encoder_thread::add_log(const int log_type, const char *context, ...){
	const int array_length = 1024 * 10;
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

	if(send_channel_->write_log_ptr_ != nullptr){
		send_channel_->write_log_ptr_->write_log3(log_type, log_text);
	}
}

//*******************
analysis_bandwidth::analysis_bandwidth(send_channel *parent){
	send_channel_ = parent;
	polyfit_ptr_ = new polyfit();
}

analysis_bandwidth::~analysis_bandwidth(void){
	if (nullptr != polyfit_ptr_)
		delete polyfit_ptr_;

	polyfit_ptr_ = nullptr;
}

void analysis_bandwidth::add_null_header(null_rudp_header null_header){
	std::lock_guard<std::recursive_mutex> gurad(null_header_list_lock_);
	if(null_header_list_.size() > max_null_header_count_){
		null_header_list_.pop_front();
	}
	std::shared_ptr<null_rudp_header> null_header_ptr(new null_rudp_header);
	null_header_ptr->header_ = null_header.header_;
	null_header_ptr->bandwidth_ = null_header.bandwidth_;
	null_header_ptr->loss_packet_count_ = null_header.loss_packet_count_;
	null_header_ptr->loss_packet_interval_ = null_header.loss_packet_interval_;
	null_header_ptr->max_group_index_ = null_header.max_group_index_;
	null_header_ptr->max_index_ = null_header.max_index_;
	null_header_ptr->null_index_ = null_header.null_index_;
	null_header_ptr->overall_recv_size_ = null_header.overall_recv_size_;
	null_header_ptr->real_timer_rto_ = null_header.real_timer_rto_;
	null_header_ptr->recv_packet_count_ = null_header.recv_packet_count_;
	null_header_ptr->recv_packet_interval_ = null_header.recv_packet_interval_;
	null_header_ptr->useful_recv_size_ = null_header.useful_recv_size_;
	null_header_list_.push_back(null_header_ptr);
}
	
void analysis_bandwidth::free_null_header(){
	std::lock_guard<std::recursive_mutex> gurad(null_header_list_lock_);
	null_header_list_.clear();
}

std::shared_ptr<null_rudp_header> analysis_bandwidth::get_last_null_header(){
	std::lock_guard<std::recursive_mutex> gurad(null_header_list_lock_);
	if (!null_header_list_.empty())
		return null_header_list_.back();
	else
		return nullptr;
}

//****拟合算法****
double_vector analysis_bandwidth::polyfit_bandwidth(){
	std::lock_guard<std::recursive_mutex> gurad(null_header_list_lock_);
	//得到采样点vector
	std::vector<point> sample;
	sample.clear();
	point temp;
	for (auto iter = null_header_list_.begin(); iter != null_header_list_.end(); iter++){
		temp.x = std::shared_ptr<null_rudp_header>(*iter)->null_index_;
		temp.y = std::shared_ptr<null_rudp_header>(*iter)->overall_recv_size_;
		sample.push_back(temp);
	}
	//获取拟合后的值
	double_vector poly_vector;
	poly_vector.clear();
	if (nullptr == polyfit_ptr_)
		return poly_vector;

	//得到拟合系数
	poly_vector = polyfit_ptr_->poly_bandwidths(sample, 4);
	return poly_vector;
}

double analysis_bandwidth::poly_point_trend(double_vector poly_vector){
	if (poly_vector.size() < 2)
		return multi_gain_;

	double trend = poly_vector[poly_vector.size() - 1] / poly_vector[poly_vector.size() - 2];
	//在这里清除掉太大的斜率（过大的斜率说明第一次的数据为非完整斜率）
	if(trend >= 5)
		return  multi_gain_;

	return trend;
}

uint32 analysis_bandwidth::poly_bandwidth(){
	uint32 new_threshold = send_channel_->get_current_max_bandwidth();
	std::shared_ptr<null_rudp_header> last_null_ptr = get_last_null_header();
	if (nullptr == last_null_ptr)
		return new_threshold;

	//得到拟合后的数据;
	double_vector poly_vector = polyfit_bandwidth();
	//根据拟合后的数据判断趋势;
	double poly_trend = poly_point_trend(poly_vector);
	//拟合斜率大于0.97
	if (poly_trend >= 0.95)	{
		if (poly_trend <= 1.05){
			//斜率在一定范围内，则表示，当前带宽处于平稳状态，需要判断当前带宽是否离拟合数值太远，如果太远则进行修正;
			if (new_threshold > static_cast<uint32>(poly_vector[poly_vector.size() - 1] * 1.25)) {
				new_threshold = static_cast<uint32>(poly_vector[poly_vector.size() - 1]);
			}else{
				//new_threshold = new_threshold;
				new_threshold = multi_bandwidth(new_threshold);
			}
		}else{
			current_detection_state_ = dsMulti;
			new_threshold = multi_bandwidth(new_threshold);
			if (!poly_vector.empty()){
				//判断新的带宽不能大于最后采样的1.1倍
				uint32 last_sample_bandwidth = last_null_ptr->overall_recv_size_;
				if (new_threshold > static_cast<uint32>((double)last_sample_bandwidth * 1.5)){
					new_threshold = send_channel_->get_current_max_bandwidth();
				}
			}
		}
		//带宽阀值限制;
		new_threshold = limit_bandwidth(new_threshold);
		//  if (poly_vector.size() > 2)
		//  {
		//  	send_channel_->add_log(LOG_TYPE_INFO, "poly_bandwidth detection=%d trend=%.2f new_threshold=%d size=%d poly_vector[n-2]=%d poly_vector[n-1]=%d last_sample_bandwidth=%d",
		//  		current_detection_state_,
		//  		poly_trend,
		//  		new_threshold,
		//  		poly_vector.size(),
		//  		static_cast<uint32>(poly_vector[poly_vector.size() - 2]),
		//  		static_cast<uint32>(poly_vector[poly_vector.size() - 1]),
		//  		last_null_ptr->overall_recv_size_);
		//  }
	}else{
		if(dsMulti == current_detection_state_){
			current_detection_state_ = dsLine;
			finish_line_poly_bandwidth_ = static_cast<uint32>(poly_vector[poly_vector.size() - 2]);
			start_line_poly_bandwidth_ = static_cast<uint32>(poly_vector[poly_vector.size() - 1]);
			if(new_threshold < finish_line_poly_bandwidth_){
				new_threshold = start_line_poly_bandwidth_;
			}
		}else{
			if(new_threshold + line_gain_ < finish_line_poly_bandwidth_){
				new_threshold = add_bandwidth(new_threshold);
			}	
		}
		new_threshold = limit_bandwidth(new_threshold);
		//  if (poly_vector.size() > 2)
		//  {
		//  	send_channel_->add_log(LOG_TYPE_INFO, "poly_bandwidth detection=%d trend=%.2f new_threshold=%d size=%d poly_vector[n-2]=%d poly_vector[n-1]=%d last_sample_bandwidth=%d",
		//  		current_detection_state_,
		//  		poly_trend,
		//  		new_threshold,
		//  		poly_vector.size(),
		//  		static_cast<uint32>(poly_vector[poly_vector.size() - 2]),
		//  		static_cast<uint32>(poly_vector[poly_vector.size() - 1]),
		//  		last_null_ptr->overall_recv_size_);
		//  }
	}
	return new_threshold;
}

uint32 analysis_bandwidth::limit_bandwidth(const uint32 &bandwidth){
	uint32 new_bandwidth = bandwidth;
	if (new_bandwidth >= send_channel_->get_max_bandwidth()){
		new_bandwidth = send_channel_->get_max_bandwidth();
		if (bandwidth >= MAX_BANDWIDTH){
			new_bandwidth = MAX_BANDWIDTH;
		}
	}
	if (new_bandwidth <= send_channel_->get_start_bandwidth()){
		new_bandwidth = send_channel_->get_start_bandwidth();
	}
	return new_bandwidth;
}

uint32 analysis_bandwidth::multi_bandwidth(const uint32 &bandwidth){
	return static_cast<uint32>(bandwidth * multi_gain_);
}

uint32 analysis_bandwidth::add_bandwidth(const uint32 &bandwidth){
	return bandwidth + static_cast<uint32>(line_gain_);
}

//*************
send_channel::send_channel(int linker_handle, uint32 start_bandwdith, uint32 max_bandwidth){
	fec_thread_count_ = 1;
	linker_handle_ = linker_handle;
	current_segment_index_ = INIT_SEGMENT_INDEX;
	complete_group_id_ = 0;
	start_bandwidth_ = start_bandwdith;
	max_bandwidth_ = max_bandwidth;
	if (0 == start_bandwidth_){
		start_bandwidth_ = 512 * KB;
	}
	if (0 == max_bandwidth_ || max_bandwidth_ >= MAX_BANDWIDTH){
		max_bandwidth_ = MAX_BANDWIDTH;
	}
	set_current_max_bandwidth(start_bandwidth_);
	init_buffer_pool();
	reset_send_chennel_flow();
	reset_send_thread_flow();
}

send_channel::~send_channel(void){
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
	reset_send_chennel_flow();
	reset_send_thread_flow();
	free_confirm_buffer();	
	free_fec_threads();
	free_pool();
	delete analysis_bandwidth_ptr_;
	analysis_bandwidth_ptr_ = nullptr;
}

uint32 send_channel::get_max_bandwidth(){
	return max_bandwidth_;
}

uint32 send_channel::get_start_bandwidth(){
	return start_bandwidth_;
}

void send_channel::add_sack_queue(sack_rudp_header sack_header){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	fast_sack_buffer *buffer_ptr = new fast_sack_buffer;
	buffer_ptr->message_type_ = message_sack;
	buffer_ptr->next_ = nullptr;
	memset(&buffer_ptr->ack_header_, 0, sizeof(buffer_ptr->ack_header_));
	memset(&buffer_ptr->sack_header_, 0, sizeof(buffer_ptr->sack_header_));
	memcpy(&buffer_ptr->sack_header_, &sack_header, sizeof(buffer_ptr->sack_header_));
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}

void send_channel::add_ack_queue(ack_rudp_header ack_header){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	fast_sack_buffer *buffer_ptr = new fast_sack_buffer;
	buffer_ptr->message_type_ = message_ack;
	buffer_ptr->next_ = nullptr;
	memset(&buffer_ptr->ack_header_, 0, sizeof(buffer_ptr->ack_header_));
	memset(&buffer_ptr->sack_header_, 0, sizeof(buffer_ptr->sack_header_));
	memcpy(&buffer_ptr->ack_header_, &ack_header, sizeof(buffer_ptr->ack_header_));
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}

void send_channel::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	fast_sack_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void send_channel::init(){
	analysis_bandwidth_ptr_ = new analysis_bandwidth(this);
	thread_ptr_ = std::thread(&send_channel::execute, this);
	thread_ptr_.detach();
}

void send_channel::execute(){
	//创建FEC编码线程
	init_fec_threads();
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		dispense();
		delete_confirm_buffer();
		// fast_retransmission();
		// showed_wait_confirm();
		ustd::rudp_public::sleep_delay(CHANNEL_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void send_channel::dispense(){
	fast_sack_buffer *work_ptr = nullptr;
	fast_sack_buffer *next_ptr = nullptr;
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
		if(message_ack == work_ptr->message_type_){
			ack_confirm(work_ptr->ack_header_);
		}else if(message_sack == work_ptr->message_type_){
			check_sack_fast_retransmission(work_ptr->sack_header_);
		}	
		delete work_ptr;
		work_ptr = next_ptr;
	}
}

void send_channel::handle_set_frames_id(const uint64 &frames_id){
	if(nullptr != on_set_frames_id_){
		on_set_frames_id_(frames_id);
	}
}

void send_channel::handle_set_segment_index(const uint64 &index){
	if(nullptr != on_set_segment_index_){
		on_set_segment_index_(index);
	}
}

void send_channel::handle_fec_finished(uint8 message_type, uint64 index, char* data, int size, struct sockaddr_in addr){
	if(message_data == message_type){
		//数据，需要释放;
		if (nullptr != on_add_send_queue_){
			on_add_send_queue_(index, message_type, data, size, linker_handle_, addr);
		}
	}else if(message_error == message_type){
		//纠错，不需要释放;
		if (nullptr != on_add_send_queue_no_feekback_){
			on_add_send_queue_no_feekback_(0, 0, data, size, linker_handle_, addr);
		}
	}
}

void send_channel::handle_on_send_overall(int size){
	if(nullptr != on_send_overall_){
		on_send_overall_(size);
	}
}

void send_channel::handle_on_send_useful(int size){
	if(nullptr != on_send_useful_){
		on_send_useful_(size);
	}
}

void send_channel::handle_resend(std::shared_ptr<confirm_buffer> confirm_buffer_ptr, bool followed){
	if(nullptr != on_resend_){
		confirm_buffer_ptr->buffer_.body_.send_timer_ = global_rudp_timer_.get_current_timer();
		on_resend_(confirm_buffer_ptr, followed);
	}
}

uint64 send_channel::get_segment_index(){
	std::lock_guard<std::recursive_mutex> gurad(segment_index_lock_);
	current_segment_index_++;
	if(current_segment_index_ <= 0){
		current_segment_index_ = 1;
	}
	return current_segment_index_;
}

//发送频道流量管理;
void send_channel::reset_send_chennel_flow(){
	std::lock_guard<std::recursive_mutex> gurad(send_chennel_flow_lock_);
	send_chennel_flow_ = 0;
}
	
void send_channel::add_send_chennel_flow(const uint64 &flow){
	std::lock_guard<std::recursive_mutex> gurad(send_chennel_flow_lock_);
	send_chennel_flow_ += flow;
}

void send_channel::set_current_max_bandwidth(const uint32 &bandwidth){
	std::lock_guard<std::recursive_mutex> gurad(current_max_bandwidth_lock_);
	current_max_bandwidth_ = bandwidth;
}

uint32 send_channel::get_current_max_bandwidth(){
	std::lock_guard<std::recursive_mutex> gurad(current_max_bandwidth_lock_);
	return current_max_bandwidth_;
}

void send_channel::set_remote_nul(null_rudp_header null_header){
	if (loss_index_ != null_header.null_index_){
		loss_index_ = null_header.null_index_;
		if (analysis_bandwidth_ptr_ != nullptr){
			analysis_bandwidth_ptr_->add_null_header(null_header);
		}
	}
}

bool send_channel::is_send_chennel_threshold(const uint64 &size){
	std::lock_guard<std::recursive_mutex> gurad(send_chennel_flow_lock_);
	if(send_chennel_flow_ + size <= get_current_max_bandwidth() * 1.5)
		return false;
	else
		return true;
}
	
//发送线程频道管理;
void send_channel::reset_send_thread_flow(){
	std::lock_guard<std::recursive_mutex> gurad(send_thread_flow_lock_);
	send_thread_flow_ = 0;
}
	
void send_channel::add_send_thread_flow(const uint64 &flow){
	std::lock_guard<std::recursive_mutex> gurad(send_thread_flow_lock_);
	send_thread_flow_ += flow;
}

bool send_channel::is_send_thread_threshold(const uint64 &size){
	std::lock_guard<std::recursive_mutex> gurad(send_thread_flow_lock_);
	if(send_thread_flow_ + size <= get_current_max_bandwidth())
		return true;
	else
		return false;
}

uint64 send_channel::get_current_send_thread_flow(){
	return send_thread_flow_;
}

void send_channel::check_bandwidth(){
	if (current_loss_index_ != loss_index_){
		current_loss_index_ = loss_index_;
		if (analysis_bandwidth_ptr_ != nullptr){
			set_current_max_bandwidth(analysis_bandwidth_ptr_->poly_bandwidth());
		}
	}
}

void send_channel::check_flow(){
	reset_send_chennel_flow();
	reset_send_thread_flow();
}

void send_channel::showed_wait_confirm(){
	time_t curr_time = time(nullptr);
	int second = abs(static_cast<int>(difftime(curr_time, last_showed_wait_confirm_time_)));
	if(second >= 1){
		//算法2：求待确认队列中最大和最小差值所代表的数据大小;
		uint64 max_index = max_confirm_index();
		uint64 min_index = min_confirm_index();
		uint64 cur_confirm_windows_size = (max_index - min_index) * BUFFER_SIZE;
		#ifdef x86
			add_log(LOG_TYPE_INFO, "showed_wait_confirm min_index=%d max_index=%d cur_confirm_windows_size=%s", 
				max_index, min_index, ustd::rudp_public::get_speed(cur_confirm_windows_size).c_str());
		#else
			add_log(LOG_TYPE_INFO, "showed_wait_confirm min_index=%lld max_index=%lld cur_confirm_windows_size=%s", 
				max_index, 
				min_index, 
				ustd::rudp_public::get_speed(cur_confirm_windows_size).c_str());
		#endif
		last_showed_wait_confirm_time_ = time(nullptr);
	}
}

//将数据放入到发送channel中，这里需要注意的是，首先应该判断是否可以全放入到channel中，用来进行流量控制;
bool send_channel::add_buffer_to_send_channel(uint64 frames_no, char *data, int size, struct sockaddr_in addr){
	//初始化
	uint64 position = 0;
	uint64 length = 0;
	uint64 current_packet_no = 1;
	uint16 packet_count = (size + FRAMES_BUFFER_SIZE - 1) / FRAMES_BUFFER_SIZE;
	//设置帧ID
	handle_set_frames_id(frames_no);
    //切片;
	while((size - position) > 0){
		if(size - position >= FRAMES_BUFFER_SIZE)
			length = FRAMES_BUFFER_SIZE;
		else
			length = size - position;

		uint64 segment_index = get_segment_index();
		handle_set_segment_index(segment_index);
		//初始化帧包;
		frames_buffer* frames_ptr = alloc_buffer();
		memset(frames_ptr, 0, sizeof(frames_buffer));
		memset(&frames_ptr->header_, 0, sizeof(frames_ptr->header_));
		frames_ptr->header_.message_type_ = message_data;
		frames_ptr->header_.index_ = segment_index;
		frames_ptr->body_.frames_no_ = frames_no;
		frames_ptr->body_.packet_count_ = packet_count;
		frames_ptr->body_.packet_no_ = current_packet_no;
		frames_ptr->body_.send_count_ = 0;
		frames_ptr->body_.size_ = length;
		memcpy(frames_ptr->body_.data_, data + position, length);
		frames_ptr->body_.frames_timer_ = global_rudp_timer_.get_current_timer();
		frames_ptr->body_.send_timer_ = global_rudp_timer_.get_current_timer();
		frames_ptr->body_.recv_timer_ = global_rudp_timer_.get_current_timer();
		add_confirm_buffer(frames_ptr);
		//在这里进行判断，如果存在FEC的时候，将会把数据交给fec编码线程
		if (FEC_REDUNDANCY_SIZE > 0){
			fec_encoder_thread* fec_encoder_ptr = get_fec_thread(frames_ptr->header_.index_);
			if (fec_encoder_ptr != nullptr){
				fec_encoder_ptr->add_queue(frames_ptr, addr);
			}
		}
		handle_fec_finished(message_data, segment_index, (char*)frames_ptr, sizeof(frames_buffer), addr);
		release_buffer(frames_ptr);
		handle_on_send_useful(sizeof(frames_buffer));
		handle_on_send_overall(sizeof(frames_buffer));
		position += length;
		current_packet_no++;
	}
	return true;
}

void send_channel::add_confirm_buffer(frames_buffer* buffer_ptr){
	std::lock_guard<std::recursive_mutex> gurad(confirm_buffer_lock_);
   	std::shared_ptr<confirm_buffer> confirm_ptr(new confirm_buffer);
   	confirm_ptr->segment_index_ = buffer_ptr->header_.index_;
   	confirm_ptr->frames_no_ = buffer_ptr->body_.frames_no_;
   	confirm_ptr->current_state_ = SENDING;
   	confirm_ptr->go_timer_ = 0;
   	confirm_ptr->send_count_ = 0;
	memcpy(&confirm_ptr->buffer_, buffer_ptr, sizeof(frames_buffer));
	if (get_send_max_index() <= confirm_ptr->segment_index_){
		set_send_max_index(confirm_ptr->segment_index_);
	}
	confirm_buffer_map_.insert(std::make_pair(confirm_ptr->segment_index_, confirm_ptr));
}

std::shared_ptr<confirm_buffer> send_channel::find_confirm_buffer(const uint64 &index){
	std::lock_guard<std::recursive_mutex> gurad(confirm_buffer_lock_);
	confirm_buffer_map::iterator iter = confirm_buffer_map_.find(index);
	if(iter != confirm_buffer_map_.end()){
		return iter->second;
	}
	return nullptr;
}

void send_channel::free_confirm_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(confirm_buffer_lock_);
	if (confirm_buffer_map_.empty())
		return;

	confirm_buffer_map_.clear();
}

uint64 send_channel::min_confirm_index(){
	if(confirm_buffer_map_.empty())
		return 0;

	confirm_buffer_map::iterator iter = confirm_buffer_map_.begin();
	uint64 min_index = (iter)->first;
	return min_index;
}
	
uint64 send_channel::max_confirm_index(){
	if(confirm_buffer_map_.empty())
		return 0;

	confirm_buffer_map::reverse_iterator iter = confirm_buffer_map_.rbegin();
	uint64 max_index = (iter)->first;
	return max_index;
}

bool send_channel::confirm_windows_threshold(windows_math_mode windows_mode){
	if(MATH_SIZE == windows_mode){
		//算法1：直接求待确认队列中的数据大小;
		uint64 cur_confirm_windows_size = confirm_buffer_map_.size() * BUFFER_SIZE;
		if (cur_confirm_windows_size >= get_current_max_bandwidth() * 1.5)
			return true;
		else
			return false;
	}else if(MATH_INTERVAL == windows_mode){
		//算法2：求待确认队列中最大和最小差值所代表的数据大小;
		uint64 max_index = max_confirm_index();
		uint64 min_index = min_confirm_index();
		uint64 cur_confirm_windows_size = (max_index - min_index) * BUFFER_SIZE;
		if (cur_confirm_windows_size >= get_current_max_bandwidth() * 1.5)
			return true;
		else
			return false;
	}
	return false;
}

void send_channel::init_buffer_pool(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_pool_lock_);
	for(uint32 i = 0; i < RUDP_BUFFER_POOL_SIZE; i++){
		frames_buffer* buffer_ptr = new frames_buffer;
		memset(buffer_ptr, 0, sizeof(frames_buffer));
		buffer_pool_list_.push_back(buffer_ptr);
	}
}
	
frames_buffer* send_channel::alloc_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_pool_lock_);
	if(buffer_pool_list_.empty()){
		//缓存池已经清空，需要重新批量申请缓存
		for(uint32 i = 0; i < RUDP_BUFFER_POOL_SIZE; i++){
			frames_buffer* buffer_ptr = new frames_buffer;
			memset(buffer_ptr, 0, sizeof(frames_buffer));
			buffer_pool_list_.push_back(buffer_ptr);
		}
	}
	frames_buffer* buffer_ptr = buffer_pool_list_.front();
	buffer_pool_list_.pop_front();
	return buffer_ptr;
}

void send_channel::release_buffer(frames_buffer* buffer_ptr){
	std::lock_guard<std::recursive_mutex> gurad(buffer_pool_lock_);
	memset(buffer_ptr, 0, sizeof(frames_buffer));
	buffer_pool_list_.push_back(buffer_ptr);
}
	
void send_channel::free_pool(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_pool_lock_);
//	while(buffer_pool_list_.size() > 0)
//	{
//		frames_buffer* buffer_ptr = buffer_pool_list_.front();
//		buffer_pool_list_.pop_front();
//		delete buffer_ptr;
//		buffer_ptr = nullptr;
//	}
	for(auto iter = buffer_pool_list_.begin(); iter != buffer_pool_list_.end(); iter++){
		frames_buffer* buffer_ptr = *iter;
		if(buffer_ptr != nullptr){
			delete buffer_ptr;
			buffer_ptr = nullptr;
		}
	}
	buffer_pool_list_.clear();
}

uint64 send_channel::bound(const uint64 &lower, const uint64 &middle, const uint64 &upper){
	return (std::min)(std::max(lower, middle), upper);
}

void send_channel::calculate_rto(const rudptimer &send_timer){
	rudptimer tmp_current_timer = global_rudp_timer_.get_current_timer();
	int rttm = static_cast<int>(tmp_current_timer - send_timer);
	if(rttm < 0 || rttm > static_cast<int>(MAX_RESEND_TIMEOUT)){
		return;
	}
	if(0 == real_timer_rtts_){
		real_timer_rtts_ = static_cast<float>(rttm);
		real_timer_rttd_ = static_cast<float>(rttm / 2);
	}else{
		real_timer_rtts_ = static_cast<float>(0.875 * real_timer_rtts_ + 0.125 * rttm);
		real_timer_rttd_ = static_cast<float>(0.75 * real_timer_rttd_ + 0.25 * std::abs(long(rttm - real_timer_rtts_)));
	}
	real_timer_rto_ = static_cast<uint64>(real_timer_rtts_ + 4 * real_timer_rttd_);
	real_timer_rto_ = bound(MIN_RTO, real_timer_rto_ , MAX_RTO);
}

void send_channel::ack_confirm(ack_rudp_header ack_header_ptr){
	set_complete_group_id(ack_header_ptr.complete_group_id_);
	std::shared_ptr<confirm_buffer> tmp_confirm_buffer = find_confirm_buffer(ack_header_ptr.header_.index_);
	if(nullptr != tmp_confirm_buffer){
		calculate_rto(tmp_confirm_buffer->go_timer_);
		tmp_confirm_buffer->current_state_ = DIRECT_ACKED;
	}
}

void send_channel::fast_retransmission(){
	std::lock_guard<std::recursive_mutex> gurad(confirm_buffer_lock_);
	//检测是否有需要重新发送的等待确认数据
	for(auto iter = confirm_buffer_map_.begin(); iter != confirm_buffer_map_.end(); iter++){
		std::shared_ptr<confirm_buffer> confirm_ptr = iter->second;
		if(nullptr != confirm_ptr){
			if(WAITING == confirm_ptr->current_state_){
				uint64 current_rto = get_local_rto();
				rudptimer request_interval = ustd::rudp_public::abs_sub(global_rudp_timer_.get_current_timer(), confirm_ptr->go_timer_);
				rudptimer delay_interval = static_cast<rudptimer>(FAST_RESEND_DELAY * current_rto);
				if(request_interval >= delay_interval){
					confirm_ptr->current_state_ = SENDING;
					handle_resend(confirm_ptr, true);
				}
			}
		}
	}
}

void send_channel::check_sack_fast_retransmission(sack_rudp_header sack_header_ptr){
	std::lock_guard<std::recursive_mutex> gurad(confirm_buffer_lock_);
	set_complete_group_id(sack_header_ptr.complete_group_id_);
	for(uint64 group_postion = 0; group_postion < sack_header_ptr.group_count_; group_postion++){
		group_followsegment tmp_group_followsegment = sack_header_ptr.followsegment[group_postion];
		uint64 group_min_index, group_max_index;
		ustd::rudp_public::get_group_min_max_index(tmp_group_followsegment.group_id_, group_min_index, group_max_index);
		int postion = 0;
		for(uint64 index = group_min_index; index <= group_max_index; index++){
			std::shared_ptr<confirm_buffer> confirm_ptr = find_confirm_buffer(index);
			if(nullptr != confirm_ptr){
				uint64 tmp_loss = tmp_group_followsegment.group_followsegment_ & ((uint64)1 << postion);
				if(0 == tmp_loss){
					if(WAITING == confirm_ptr->current_state_){
						uint64 current_rto = get_local_rto();
						rudptimer request_interval = ustd::rudp_public::abs_sub(global_rudp_timer_.get_current_timer(), confirm_ptr->go_timer_);
						rudptimer delay_interval = static_cast<rudptimer>(SACK_REQUEST_DELAY * current_rto);
						if(request_interval >= delay_interval){
							confirm_ptr->current_state_ = SENDING;
							handle_resend(confirm_ptr, true);
						}
					}
				}else{
					if(WAITING == confirm_ptr->current_state_){
						confirm_ptr->current_state_ = INDIRECT_SACKED;
					}else if(SENDING == confirm_ptr->current_state_){
						confirm_ptr->current_state_ = INDIRECT_SACKED;
					}
				}
			}
			postion++;
        }
    }
}

void send_channel::set_confirm_buffer_wait(const uint64 &index){
	std::shared_ptr<confirm_buffer> confirm_ptr = find_confirm_buffer(index);
	if(nullptr != confirm_ptr){
		confirm_ptr->go_timer_ = global_rudp_timer_.get_current_timer();
		confirm_ptr->send_count_++;
		confirm_ptr->current_state_ = WAITING;
	}
}

void send_channel::delete_confirm_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(confirm_buffer_lock_);
	uint64 min_index = 0, max_index = 0;
	ustd::rudp_public::get_group_min_max_index(get_complete_group_id(), min_index, max_index);
	for(auto iter = confirm_buffer_map_.begin(); iter != confirm_buffer_map_.end();){
		std::shared_ptr<confirm_buffer> confirm_ptr = iter->second;
		if(confirm_ptr->segment_index_ > max_index)
			return;

		iter = confirm_buffer_map_.erase(iter);
	}
}
	
uint16 send_channel::get_local_rto(){
	return real_timer_rto_;
}

void send_channel::init_fec_threads(){
	if (FEC_REDUNDANCY_SIZE > 0){
		fec_threads_vector_.reserve(fec_thread_count_);
		add_log(LOG_TYPE_INFO, "Init Fec Math Thread Count=%d", fec_thread_count_);
		for (uint16 i = 0; i < fec_thread_count_; i++){
			fec_encoder_thread* fec_encoder_ptr = new fec_encoder_thread(this);
			fec_encoder_ptr->init();
			fec_threads_vector_.push_back(fec_encoder_ptr);
		}
		add_log(LOG_TYPE_INFO, "Init Fec Math Thread OK");
	}
}

void send_channel::free_fec_threads(){
	for(auto iter = fec_threads_vector_.begin(); iter != fec_threads_vector_.end(); iter++){
		fec_encoder_thread* thread_ptr = *iter;
		if(nullptr != thread_ptr){
			delete thread_ptr;
			thread_ptr = nullptr;
		}
	}
	fec_threads_vector_.clear();
}

uint16 send_channel::get_fec_postion(uint64 index){
	uint64 index_spare = (index - 1) / FEC_GROUP_SIZE;
	uint16 postion = static_cast<uint16>(index_spare % fec_threads_vector_.size());
	return postion;
}

fec_encoder_thread* send_channel::get_fec_thread(uint64 index){
	uint16 postion = get_fec_postion(index);
	return fec_threads_vector_[postion];
}

int send_channel::block_channel_bandwidth(const int &size){
	//判断流量过载，在这里使用了同步发送策略，避免上层再进行处理
	if (is_send_chennel_threshold(size)){
		//由于这里已经过载，需要在这里进行等待处理，等待多长时间
		uint32 delay_count = 0;
		while (1){
			ustd::rudp_public::sleep_delay(CHECK_BANDWIDTH_INTERVAL, Millisecond);
			if (!is_send_chennel_threshold(size)){
				break;
			}
			delay_count++;
			if (delay_count >= MAX_WAIT_TIMER * (SECOND_TIMER / CHECK_BANDWIDTH_INTERVAL)){
				return FLOW_OVERLOAD;
			}
		}
	}
	return 0;
}

int send_channel::block_confirm_bandwidth(const int &size){
	if (confirm_windows_threshold(current_windows_mode)){
		//由于这里已经过载，需要在这里进行等待处理，等待多长时间
		uint32 delay_count = 0;
		while (1){
			ustd::rudp_public::sleep_delay(CHECK_BANDWIDTH_INTERVAL, Millisecond);
			if (!confirm_windows_threshold(current_windows_mode)){
				break;
			}
			delay_count++;
			if (delay_count >= MAX_WAIT_TIMER * (SECOND_TIMER / CHECK_BANDWIDTH_INTERVAL)){
				return CONFIRM_OVERLOAD;
			}
		}
	}
	return 0;
}

int send_channel::block_bandwidth(const int &size){
	//频道带宽控制;
	int block_channel_ret = block_channel_bandwidth(size);
	if (block_channel_ret != 0){
		add_log(LOG_TYPE_ERROR, "send_channel block_channel_bandwidth ret=%d", block_channel_ret);
		return block_channel_ret;
	}
	//确认带宽控制;
	int block_confirm_ret = block_confirm_bandwidth(size);
	if (block_confirm_ret != 0){
		add_log(LOG_TYPE_ERROR, "send_channel block_confirm_bandwidth ret=%d", block_confirm_ret);
		return block_confirm_ret;
	}
	return 0;
}

void send_channel::add_log(const int log_type, const char *context, ...){
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

