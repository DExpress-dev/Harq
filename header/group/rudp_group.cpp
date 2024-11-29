
#include "rudp_group.h"

#include <math.h>
#include <algorithm>

fec_decoder_thread::fec_decoder_thread(rudp_group *parent){
	rudp_group_ = parent;
	if (FEC_REDUNDANCY_SIZE > 0){
		fec_decoder_ptr_ = new fec_decoder();
	}
}

void fec_decoder_thread::init(const uint64 &thread_count, const uint64 &position){
	decoder_thread_count_ = thread_count;
	decoder_thread_postion_ = position;
	if (nullptr != fec_decoder_ptr_){
		fec_decoder_ptr_->write_log_ptr_ = write_log_ptr_;
		fec_decoder_ptr_->init(FEC_GROUP_SIZE, FEC_REDUNDANCY_SIZE, FRAMES_BODY_SIZE);
		fec_decoder_ptr_->init_decode();
	}
	thread_ptr_ = std::thread(&fec_decoder_thread::execute, this);
	thread_ptr_.detach();
}

fec_decoder_thread::~fec_decoder_thread(){
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
	free_group();
	if (fec_decoder_ptr_ != nullptr){
		delete fec_decoder_ptr_;
		fec_decoder_ptr_ = nullptr;
	}
}

void fec_decoder_thread::add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	rudp_group_buffer *buffer_ptr = new rudp_group_buffer;
	buffer_ptr->message_type_ = message_type;
	buffer_ptr->next_ = nullptr;
	buffer_ptr->size_ = size;
	buffer_ptr->recv_timer_ = recv_timer;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	memcpy(buffer_ptr->buffer_, data, size);
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}

void fec_decoder_thread::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	rudp_group_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void fec_decoder_thread::dispense(){
	rudp_group_buffer *work_ptr = nullptr;
	rudp_group_buffer *next_ptr = nullptr;
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
		if(message_data == work_ptr->message_type_){
			frames_buffer *frames_ptr = (frames_buffer *)work_ptr->buffer_;
			frames_ptr->body_.recv_timer_ = work_ptr->recv_timer_;
			add_frames_group(frames_ptr);
		}else if(message_error == work_ptr->message_type_){
			error_buffer *error_ptr = (error_buffer *)work_ptr->buffer_;
			add_error_group(error_ptr);
		}
		delete work_ptr;
		work_ptr = next_ptr;
	}
}

bool fec_decoder_thread::add_frames_group(frames_buffer* frames_buffer_ptr){
	uint64 group_id = ustd::rudp_public::get_group_id(frames_buffer_ptr->header_.index_);
	std::shared_ptr<fec_group_buffer> group_buffer = find_group(group_id);
	if(nullptr == group_buffer){
		group_buffer.reset(new fec_group_buffer);
		group_buffer->group_id_ = group_id;
		group_buffer->group_size_ = sizeof(frames_body) * FEC_GROUP_SIZE;
		group_buffer->last_insert_timer_ = global_rudp_timer_.get_current_timer();
		group_buffer->last_sack_timer_ = global_rudp_timer_.get_current_timer();
		fec_group_map_.insert(std::make_pair(group_id, group_buffer));
	}
	//在这里进行index的判断;
	std::map<uint64, std::shared_ptr<frames_buffer>>::iterator iter = group_buffer->frames_buffer_map_.find(frames_buffer_ptr->header_.index_);
	if(iter == group_buffer->frames_buffer_map_.end()){
		std::shared_ptr<frames_buffer> frames_ptr(new frames_buffer);
		memcpy(&frames_ptr->header_, &frames_buffer_ptr->header_, sizeof(frames_buffer_ptr->header_));
		memset(&frames_ptr->body_, 0, sizeof(frames_ptr->body_));
		memcpy(&frames_ptr->body_, &frames_buffer_ptr->body_, sizeof(frames_buffer_ptr->body_));
		group_buffer->last_insert_timer_ = global_rudp_timer_.get_current_timer();
		group_buffer->frames_buffer_map_.insert(std::make_pair(frames_ptr->header_.index_, frames_ptr));
		return true;
	}
	return false;
}

void fec_decoder_thread::add_error_group(error_buffer* error_buffer_ptr){
	std::shared_ptr<fec_group_buffer> group_buffer = find_group(error_buffer_ptr->header_.index_);
	if(nullptr == group_buffer){
		group_buffer.reset(new fec_group_buffer);
		group_buffer->group_id_ = error_buffer_ptr->header_.index_;
		group_buffer->group_size_ = FRAMES_BODY_SIZE * FEC_GROUP_SIZE;
		group_buffer->last_sack_timer_ = global_rudp_timer_.get_current_timer();
		fec_group_map_.insert(std::make_pair(error_buffer_ptr->header_.index_, group_buffer));
	}
	group_buffer->last_insert_timer_ = global_rudp_timer_.get_current_timer();
	std::map<uint8, std::shared_ptr<error_buffer>>::iterator iter = group_buffer->error_buffer_map_.find(error_buffer_ptr->header_.group_index_);
	if(iter == group_buffer->error_buffer_map_.end()){
		std::shared_ptr<error_buffer> error_ptr(new error_buffer);
		memcpy(&error_ptr->header_, &error_buffer_ptr->header_, sizeof(error_buffer_ptr->header_));
		memset(error_ptr->data_, 0, sizeof(error_ptr->data_));
		memcpy(error_ptr->data_, error_buffer_ptr->data_, sizeof(error_buffer_ptr->data_));
		uint8 group_index = error_ptr->header_.group_index_;
		group_buffer->error_buffer_map_.insert(std::make_pair(group_index, error_ptr));
	}
}

std::shared_ptr<fec_group_buffer> fec_decoder_thread::find_group(const uint64 &group_id){
	fec_group_map::iterator iter = fec_group_map_.find(group_id);
	if(iter != fec_group_map_.end()){
		std::shared_ptr<fec_group_buffer> group_buffer = iter->second;
		return group_buffer;
	}
	return nullptr;
}

void fec_decoder_thread::free_group(){
	for(auto iter = fec_group_map_.begin(); iter != fec_group_map_.end(); iter++){
		std::shared_ptr<fec_group_buffer> group_buffer = iter->second;
		group_buffer->frames_buffer_map_.clear();
		group_buffer->error_buffer_map_.clear();
	}
	fec_group_map_.clear();
}

void fec_decoder_thread::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		//协议拆分;
		dispense();
		//检测分组的FEC;
		check_decoder();
		//删除不能处理的;
		check_group();
		//检测返回sack;
		poll_check_group_sack();
		ustd::rudp_public::sleep_delay(DECODER_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

//检测解码;
void fec_decoder_thread::check_decoder(){
	if (fec_group_map_.empty())
		return;

	//这里不需要进行顺序执行;
	for(auto iter = fec_group_map_.begin(); iter != fec_group_map_.end(); iter++){
		std::shared_ptr<fec_group_buffer> group_buffer = iter->second;
		//无法进行解码的分组;
		if(group_buffer->frames_buffer_map_.size() < FEC_GROUP_SIZE - FEC_REDUNDANCY_SIZE)
			continue;

		//接收完整的分组;
		if(group_buffer->frames_buffer_map_.size() >= FEC_GROUP_SIZE){
			//通知本分组完成;
			rudp_group_->add_complete_group(group_buffer->group_id_, decoder_thread_postion_);		
			continue;
		}
		//可以解码出分包;
		if((FEC_GROUP_SIZE - group_buffer->frames_buffer_map_.size() <= group_buffer->error_buffer_map_.size()) && (FEC_REDUNDANCY_SIZE > 0)){
			//解码;
			decoder_group(group_buffer);
			//通知本分组完成;
			rudp_group_->add_complete_group(group_buffer->group_id_, decoder_thread_postion_);
			continue;
		}
	}
}

void fec_decoder_thread::decoder_group(std::shared_ptr<fec_group_buffer> group_buffer){
	if (fec_decoder_ptr_ != nullptr){
		std::vector<std::shared_ptr<frames_buffer>> frames_vector;
		if (fec_decoder_ptr_->decoder(group_buffer, frames_vector)){
			for (int i = 0; i < (int)frames_vector.size(); i++){
				std::shared_ptr<frames_buffer> frames_ptr = frames_vector[i];
				if (nullptr != frames_ptr){
					std::map<uint64, std::shared_ptr<frames_buffer>>::iterator iter = group_buffer->frames_buffer_map_.find(frames_ptr->header_.index_);
					if (iter == group_buffer->frames_buffer_map_.end()){
						//这里需要注意，由于此帧块是通过FEC计算出来的，因此不设置当前的时间，避免显示耗时时间过长的问题。
						frames_ptr->body_.recv_timer_ = 0;
						handle_add_packet(message_data, (char*)frames_ptr.get(), sizeof(frames_buffer), frames_ptr->body_.recv_timer_, true);
					}
				}
			}
			frames_vector.clear();
		}
	}
}

void fec_decoder_thread::poll_check_group_sack(){
	rudptimer current_timer = global_rudp_timer_.get_current_timer();
	rudptimer interval_timer = global_rudp_timer_.timer_interval(current_timer, last_check_sack_timer_);
	if(interval_timer >= SACK_TIMER){
		uint8 poll_size = get_group_size() + (SACK_SIZE - 1) / SACK_SIZE;
		poll_size = (std::min)(poll_size, SACK_MAX_COUNT);
		for(uint8 i = 0 ; i < poll_size; i++){
			if(!check_group_sack())
				break;
		}
		last_check_sack_timer_ = global_rudp_timer_.get_current_timer();
	}	
}

uint64 fec_decoder_thread::check_sack_start_group_id(const uint64 &start_group_id, const uint64 &end_group_id){
	//在这里进行判断，并得出应该起始的分组编号。
	//判断逻辑：
	/*
		1：分批判断
		2：如果发送sack小于一定阈值时，则不进行重传请求（这个阈值需要进行计算）
	*/
	if(start_group_id <= last_check_sack_group_id_ && last_check_sack_group_id_ <= end_group_id)
		return last_check_sack_group_id_;
	else if(start_group_id > last_check_sack_group_id_)
		return start_group_id;
	else if(last_check_sack_group_id_ > end_group_id)
		return start_group_id;
	else
		return start_group_id;	
}

bool fec_decoder_thread::check_group_sack(){
	//判断是否为空;
	if(fec_group_map_.empty())
		return false;

	//初始化数据;
	const int DEFALUT_POSTION = -1;
	int group_postion = DEFALUT_POSTION;
	int current_back_count = 0;
	//每次检测最大返回的数量;
	const int max_sack_count = FEC_GROUP_SIZE;
	uint64 min_group_id = get_min_group_id();
	uint64 max_group_id = get_max_group_id();
	min_group_id = (std::min)(min_group_id, rudp_group_->next_complete_group_id());
	//封装sack包;
	sack_rudp_header sack;
	memset(&sack, 0, sizeof(sack));
	sack.header_.message_type_ = message_sack;
	sack.header_.index_ = min_group_id;
	sack.header_.group_index_ = min_group_id;
	sack.complete_group_id_ = rudp_group_->get_complete_group_id();
	//得到本次应该的起始和终止分组编号;
	uint64 start_group_id = check_sack_start_group_id(min_group_id, max_group_id);
	uint64 group_id;
	for(group_id = start_group_id; group_id <= max_group_id; group_id++){
		//判断是否装载完毕;
		if(current_back_count >= max_sack_count)
			break;

		//判断本分组是否由本线程进行处理;
		if(!handle_group_id(group_id))
			continue;

		//判断当前分组是否存在;
		std::shared_ptr<fec_group_buffer> group_buffer = find_group(group_id);
		if(nullptr == group_buffer){
			group_postion++;
			sack.followsegment[group_postion].group_id_ = group_id;
			sack.followsegment[group_postion].group_followsegment_ = 0;
			current_back_count++;
		}else{
			//判断如果本分组所有数据已经接收到，则对此分组不返回SACK
			if(FEC_GROUP_SIZE == group_buffer->frames_buffer_map_.size())
				continue;

			//判断如果本分组所有数据+冗余数据 >= 本分组数量，则不返回SACK
			if(FEC_GROUP_SIZE <= group_buffer->frames_buffer_map_.size() + group_buffer->error_buffer_map_.size())
				continue;

			//注意，这里应该求对方的RTO时间，而不是自己的RTO时间，因为要参考的应该是对方的RTO时间值
			uint64 remote_rto = rudp_group_->handle_get_remote_rto();
			//插入间隔（避免刚插入就进行sack的请求）;
			rudptimer insert_interval = ustd::rudp_public::abs_sub(global_rudp_timer_.get_current_timer(), group_buffer->last_insert_timer_);
			//sack间隔(避免sack请求太过频繁);
			rudptimer sack_interval = ustd::rudp_public::abs_sub(global_rudp_timer_.get_current_timer(), group_buffer->last_sack_timer_);
			//进行判断;
			rudptimer insert_delay_interval = static_cast<rudptimer>(INSERT_REQUEST_DELAY  * remote_rto);
			rudptimer sack_delay_interval = static_cast<rudptimer>(SACK_REQUEST_DELAY  * remote_rto);
			if((insert_interval >= insert_delay_interval) && (sack_interval >= sack_delay_interval)){
				uint64 min_index, max_index;
				ustd::rudp_public::get_group_min_max_index(group_buffer->group_id_, min_index, max_index);
				uint64 follow_index = 0;
				uint64 postion = 0;
				//在这里添加判断，由于冗余包的存在，因此返回SACK的时候，可以用冗余包来抵消缺少的数据包;
				uint16 redundancy = static_cast<uint16>(group_buffer->error_buffer_map_.size());
				for(uint64 index = min_index; index <= max_index; index++){
					std::map<uint64, std::shared_ptr<frames_buffer>>::iterator iter = group_buffer->frames_buffer_map_.find(index);
					if(iter != group_buffer->frames_buffer_map_.end()){
						follow_index = follow_index | ((uint64)1 << postion);
					}else{
						//这里需要使用冗余包来抵消未收到的包，对于无法抵消的才需要进行重传;
						if(redundancy <= 0){
							follow_index = follow_index | ((uint64)0 << postion);
						} else{
							follow_index = follow_index | ((uint64)1 << postion);
							redundancy--;
						}
					}		
					postion++;
				}
				group_postion++;
				sack.followsegment[group_postion].group_id_ = group_id;
				sack.followsegment[group_postion].group_followsegment_ = follow_index;
				group_buffer->last_sack_timer_ = global_rudp_timer_.get_current_timer();
				group_buffer->sack_count_++;
				group_buffer->sack_count_ = (std::min)(group_buffer->sack_count_, SACK_MAX_REQUEST);
				current_back_count++;
			}
		}
		if(group_postion >= SACK_SIZE - 1){
			sack.group_count_ = SACK_SIZE;
			rudp_group_->handle_send_sack(sack);
			group_postion = DEFALUT_POSTION;
			sack.group_count_ = 0;
			memset(&sack.followsegment, 0, sizeof(sack.followsegment));
		}
	}
	set_sack_start_group_id(group_id);
	if(DEFALUT_POSTION != group_postion){
		sack.group_count_ = group_postion + 1;
		rudp_group_->handle_send_sack(sack);
	}
	if (group_postion >= SACK_SIZE - 1)
		return true;
	else
		return false;
}

//删除不符合规则的分组;
void fec_decoder_thread::check_group(){
	uint64 current_index = rudp_group_->handle_get_complete_index();
	if (0 == current_index)
		return;

	uint64 current_group_id = ustd::rudp_public::get_group_id(current_index);
	for(auto iter = fec_group_map_.begin(); iter != fec_group_map_.end();){
		std::shared_ptr<fec_group_buffer> group_buffer = iter->second;
		if(group_buffer->group_id_ >= current_group_id)
			return;

		group_buffer->frames_buffer_map_.clear();
		group_buffer->error_buffer_map_.clear();
		fec_group_map_.erase(iter++);
	}
}

//将解码出来的数据加入到分组中;
void fec_decoder_thread::handle_add_packet(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created){
	if(rudp_group_->on_add_packet_ != nullptr){
		rudp_group_->on_add_packet_(message_type, data, size, recv_timer, fec_created);
	}
}

void fec_decoder_thread::add_log(const int log_type, const char *context, ...){
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

uint64 fec_decoder_thread::get_follow_from_pointer(std::shared_ptr<fec_group_buffer> fec_group_ptr){
	uint64 min_index, max_index;
	ustd::rudp_public::get_group_min_max_index(fec_group_ptr->group_id_, min_index, max_index);
	uint64 follow_index = 0;
	uint64 postion = 0;
	//在这里添加判断，由于冗余包的存在，因此返回SACK的时候，可以用冗余包来抵消缺少的数据包;
	uint16 redundancy = static_cast<uint16>(fec_group_ptr->error_buffer_map_.size());
	for(uint64 tmp_index = min_index; tmp_index <= max_index; tmp_index++){
		std::map<uint64, std::shared_ptr<frames_buffer>>::iterator iter = fec_group_ptr->frames_buffer_map_.find(tmp_index);
		if(iter != fec_group_ptr->frames_buffer_map_.end()){
			follow_index = follow_index | ((uint64)1 << postion);
		}else{
			//这里需要使用冗余包来抵消未收到的包，对于无法抵消的才需要进行重传;
			if(redundancy <= 0){
				follow_index = follow_index | ((uint64)0 << postion);
			}else{
				follow_index = follow_index | ((uint64)1 << postion);
				redundancy--;
			}
		}		
		postion++;
	}
	return follow_index;
}

//得到指定分组的follow_index
uint64 fec_decoder_thread::get_follow_from_id(const uint64 &group_id){
	add_log(LOG_TYPE_ERROR, "fec_decoder_thread::get_follow_from_id group_id=%d", group_id);
	std::shared_ptr<fec_group_buffer> group_buffer = find_group(group_id);
	if(nullptr == group_buffer){
		add_log(LOG_TYPE_ERROR, "fec_decoder_thread::get_follow_from_id group_id=%d Not Found", group_id);
		return 0;
	}
	uint64 min_index, max_index;
	uint64 follow_index = 0;
	uint64 postion = 0;
	ustd::rudp_public::get_group_min_max_index(group_buffer->group_id_, min_index, max_index);
	add_log(LOG_TYPE_ERROR, "fec_decoder_thread::get_follow_from_id get_group_min_max_index group_id=%d min=%d max=%d", group_id, min_index, max_index);
	for(uint64 index = min_index; index <= max_index; index++){
		std::map<uint64, std::shared_ptr<frames_buffer>>::iterator iter = group_buffer->frames_buffer_map_.find(index);
		if(iter != group_buffer->frames_buffer_map_.end()){
			follow_index = follow_index | ((uint64)1 << postion);
		}else{
			follow_index = follow_index | ((uint64)0 << postion);
		}		
		postion++;
	}
	return follow_index;
}

void fec_decoder_thread::set_sack_start_group_id(const uint64 &group_id){
	last_check_sack_group_id_ = group_id;
}

uint64 fec_decoder_thread::get_min_group_id(){
	if(fec_group_map_.empty())
		return 0;

	fec_group_map::iterator iter = fec_group_map_.begin();
	return (iter)->first;
}

uint64 fec_decoder_thread::get_max_group_id(){
	if(fec_group_map_.empty())
		return 0;

	fec_group_map::reverse_iterator iter = fec_group_map_.rbegin();
	return (iter)->first;
}

uint64 fec_decoder_thread::get_complete_group_id(){
	if(rudp_group_ != nullptr){
		return rudp_group_->get_complete_group_id();
	}
	return 0;
}

uint64 fec_decoder_thread::get_group_size(){
	if(fec_group_map_.empty())
		return 0;

	return static_cast<uint64>(fec_group_map_.size());
}

bool fec_decoder_thread::handle_group_id(const uint64 &group_id){
	uint16 postion = group_id % decoder_thread_count_;
	if(postion == decoder_thread_postion_){
		return true;
	}
	return false;
}

//******************
rudp_group::rudp_group(){
	last_check_trouble_timer_ = time(nullptr);
}

void rudp_group::init(){
	last_check_trouble_timer_ = time(nullptr);
	//fec_decoder_count_ = ustd::rudp_public::get_cpu_cnum();
	fec_decoder_count_ = 1;
	init_fec_decoder_vector();
	thread_ptr_ = std::thread(&rudp_group::execute, this);
	thread_ptr_.detach();
}

rudp_group::~rudp_group(){
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
	free_complete_group();
	free_fec_decoder_vector();
}

void rudp_group::handle_add_log(const int log_type, const char *context){
	if (on_add_log_ != nullptr){
		on_add_log_(log_type, context);
	}
}

void rudp_group::add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	rudp_group_buffer *buffer_ptr = new rudp_group_buffer;
	buffer_ptr->message_type_ = message_type;
	buffer_ptr->next_ = nullptr;
	buffer_ptr->size_ = size;
	buffer_ptr->recv_timer_ = recv_timer;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	memcpy(buffer_ptr->buffer_, data, size);
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}

void rudp_group::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	rudp_group_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void rudp_group::dispense(){
	rudp_group_buffer *work_ptr = nullptr;
	rudp_group_buffer *next_ptr = nullptr;
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
		if(message_error == work_ptr->message_type_){
			//如果是冗余数据;
			error_buffer *error_ptr = (error_buffer *)work_ptr->buffer_;
			fec_decoder_thread* fec_decoder_ptr = get_fec_decoder(error_ptr->header_.index_);
			if(fec_decoder_ptr != nullptr){
				fec_decoder_ptr->add_queue(work_ptr->message_type_, work_ptr->buffer_, work_ptr->size_, work_ptr->recv_timer_);
			}
		}else if(message_data == work_ptr->message_type_){
			frames_buffer *frames_ptr = (frames_buffer *)work_ptr->buffer_;
			frames_ptr->body_.recv_timer_ = work_ptr->recv_timer_;
			uint64 group_id = ustd::rudp_public::get_group_id(frames_ptr->header_.index_);
			fec_decoder_thread* fec_decoder_ptr = get_fec_decoder(group_id);
			if(fec_decoder_ptr != nullptr){
				fec_decoder_ptr->add_queue(work_ptr->message_type_, work_ptr->buffer_, work_ptr->size_, work_ptr->recv_timer_);
			}
		}
		delete work_ptr;
		work_ptr = next_ptr;
	}
}

void rudp_group::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		//协议拆分;
		dispense();
		//检测完成的分组情况;
		check_complete_group();
		//排错机制;
		// check_trouble();
		ustd::rudp_public::sleep_delay(SACK_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void rudp_group::handle_add_packet(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created){
	if(on_add_packet_ != nullptr){
		on_add_packet_(message_type, data, size, recv_timer, fec_created);
	}
}

void rudp_group::handle_send_sack(const sack_rudp_header &sack){
	if(on_send_sack_ != nullptr){
		return on_send_sack_(sack);
	}
}

uint64 rudp_group::handle_get_remote_rto(){
	if(on_remote_rto_ != nullptr){
		return on_remote_rto_();
	}
	return MAX_RTO;
}

uint64 rudp_group::handle_get_complete_index(){
	if(on_get_index_ != nullptr){
		return on_get_index_();
	}
	return 0;
}

void rudp_group::add_complete_group(const uint64 &group_id, const uint64 &postion){
	std::lock_guard<std::recursive_mutex> gurad(complete_group_lock_);
	std::shared_ptr<complete_group> complete_group_ptr(new complete_group);
	complete_group_ptr->group_id_ = group_id;
	complete_group_ptr->complete_position_ = postion;
	complete_group_ptr->complete_timer_ = static_cast<uint64>(global_rudp_timer_.get_current_timer());
	complete_group_map_.insert(std::make_pair(group_id, complete_group_ptr));
}
	
void rudp_group::check_complete_group(){
	std::lock_guard<std::recursive_mutex> gurad(complete_group_lock_);
	uint64 current_complete_index = handle_get_complete_index();
	if (0 == current_complete_index)
		return;

	uint64 recv_channel_complete_group_id;
	recv_channel_complete_group_id = ustd::rudp_public::get_group_id(current_complete_index);
	if(recv_channel_complete_group_id <= 0)
		return;

	uint64 max_group_id = (std::max)(recv_channel_complete_group_id - 1, get_complete_group_id());
	set_complete_group_id(max_group_id);
	for(auto iter = complete_group_map_.begin(); iter != complete_group_map_.end();){
		std::shared_ptr<complete_group> complete_group_ptr = iter->second;
		if(complete_group_ptr->group_id_ >= max_group_id)
			return;

		complete_group_map_.erase(iter++);
	}
}
	
void rudp_group::free_complete_group(){
	std::lock_guard<std::recursive_mutex> gurad(complete_group_lock_);
	complete_group_map_.clear();
}
	
uint64 rudp_group::min_complete_group(){
	std::lock_guard<std::recursive_mutex> gurad(complete_group_lock_);
	if(complete_group_map_.empty())
		return 0;

	complete_group_map::iterator iter = complete_group_map_.begin();
	return (iter)->first;
}
	
uint64 rudp_group::max_complete_group(){
	std::lock_guard<std::recursive_mutex> gurad(complete_group_lock_);
	if(complete_group_map_.empty())
		return 0;

	complete_group_map::reverse_iterator iter = complete_group_map_.rbegin();
	return (iter)->first;
}

void rudp_group::set_complete_group_id(uint64 group_id){
	std::lock_guard<std::recursive_mutex> gurad(group_lock_);
	complete_group_id_ = group_id;
}
	
uint64 rudp_group::get_complete_group_id(){
	std::lock_guard<std::recursive_mutex> gurad(group_lock_);
	return complete_group_id_;
}

uint64 rudp_group::next_complete_group_id(){
	return get_complete_group_id() + 1;
}

uint64 rudp_group::prev_complete_group_id(){
	uint64 current_complete_group_id = get_complete_group_id();
	if(current_complete_group_id <= 0)
		return 0;
	else
		return current_complete_group_id - 1;
}

void rudp_group::init_fec_decoder_vector(){
	fec_decoder_vector_.reserve(fec_decoder_count_);
	add_log(LOG_TYPE_INFO, "Init Fec Decoder Count=%d", fec_decoder_count_);
	for(uint16 i = 0; i < fec_decoder_count_; i++){
		fec_decoder_thread* fec_decoder_ptr = new fec_decoder_thread(this);
		fec_decoder_ptr->write_log_ptr_ = write_log_ptr_;
		fec_decoder_ptr->init(fec_decoder_count_, i);
		fec_decoder_vector_.push_back(fec_decoder_ptr);
	}
	add_log(LOG_TYPE_INFO, "Init Fec Decoder OK");
}
	
void rudp_group::free_fec_decoder_vector(){
	for(auto iter = fec_decoder_vector_.begin(); iter != fec_decoder_vector_.end(); iter++){
		fec_decoder_thread* fec_decoder_ptr = *iter;
		if(nullptr != fec_decoder_ptr){
			delete fec_decoder_ptr;
			fec_decoder_ptr = nullptr;
		}
	}
	fec_decoder_vector_.clear();
}
	
uint16 rudp_group::get_fec_decoder_postion(uint64 group_id){
	uint16 postion = static_cast<uint16>(group_id % fec_decoder_vector_.size());
	return postion;
}
	
fec_decoder_thread* rudp_group::get_fec_decoder(uint64 group_id){
	uint16 postion = get_fec_decoder_postion(group_id);
	return fec_decoder_vector_[postion];
}

void rudp_group::add_log(const int log_type, const char *context, ...){
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

	if (nullptr != write_log_ptr_)
		write_log_ptr_->write_log3(log_type, log_text);
}

	

