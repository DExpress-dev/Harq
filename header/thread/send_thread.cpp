
#include "send_thread.h"

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"
#include "../include/rudp_timer.h"

#if defined(_WIN32)

	#include <Winsock2.h>
#else

	#include <arpa/inet.h>
#endif

client_send_thread::client_send_thread(){
	current_state_ = tst_init;
}

void client_send_thread::init(const harq_fd &fd, const int32 &max_bandwidth){
	fd_ = fd;
	max_bandwidth_ = max_bandwidth;
	add_log(LOG_TYPE_INFO, "check_send_flow init max_bandwidth_=%d max_bandwidth_2=%d", max_bandwidth, max_bandwidth_);
	init_send_pool();
	thread_ptr_ = std::thread(&client_send_thread::execute, this);
	thread_ptr_.detach();
}

client_send_thread::~client_send_thread(void){
	if (current_state_ == tst_runing){
		current_state_ = tst_stoping;
		time_t last_timer = time(nullptr);
		int timer_interval = 0;
		while ((timer_interval <= KILL_THREAD_TIMER)){
			time_t current_timer = time(nullptr);
			timer_interval = static_cast<int>(difftime(current_timer, last_timer));
			if (current_state_ == tst_stoped){
				break;
			}
			ustd::rudp_public::sleep_delay(DESTROY_TIMER, Millisecond);
		}
	}
	free_urgent_buffer();
	free_normal_buffer();
	free_send_pool();
}

bool client_send_thread::add_buffer_addr(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr, bool followed){
	std::lock_guard<std::recursive_mutex> gurad(send_buffer_lock_);
	std::shared_ptr<send_buffer> buffer_ptr = alloc_send_buffer();
	buffer_ptr->feedback_ = true;
	buffer_ptr->followed_ = followed;
	buffer_ptr->index_ = index;
	buffer_ptr->message_type_ = message_type;
	buffer_ptr->linker_handle_ = linker_handle;
	buffer_ptr->size_ = size;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	memcpy(buffer_ptr->buffer_, data, size);
	memset(&buffer_ptr->remote_addr_, 0, sizeof(buffer_ptr->remote_addr_));
	memcpy(&buffer_ptr->remote_addr_, &addr, sizeof(addr));
	send_buffer_list_.push_back(buffer_ptr);
	return true;
}

bool client_send_thread::add_buffer_no_feedback(uint64 index, char *data, int size, int linker_handle, struct sockaddr_in addr){
	std::lock_guard<std::recursive_mutex> gurad(send_buffer_lock_);
	std::shared_ptr<send_buffer> buffer_ptr = alloc_send_buffer();
	buffer_ptr->feedback_ = false;
	buffer_ptr->index_ = index;
	buffer_ptr->linker_handle_ = linker_handle;
	buffer_ptr->size_ = size;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	memcpy(buffer_ptr->buffer_, data, size);
	memset(&buffer_ptr->remote_addr_, 0, sizeof(buffer_ptr->remote_addr_));
	memcpy(&buffer_ptr->remote_addr_, &addr, sizeof(addr));
	send_buffer_list_.push_back(buffer_ptr);
	return true;
}

void client_send_thread::free_normal_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(send_buffer_lock_);
	send_buffer_list_.clear();
}

//缓冲管理
void client_send_thread::init_send_pool(){
	std::lock_guard<std::recursive_mutex> gurad(send_pool_lock_);
	for (uint32 i = 0; i < RUDP_BUFFER_POOL_SIZE; i++){
		std::shared_ptr<send_buffer> buffer_ptr(new send_buffer);
		buffer_ptr->index_ = 0;
		buffer_ptr->linker_handle_ = -1;
		buffer_ptr->size_ = 0;
		memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
		send_buffer_pool_list_.push_back(buffer_ptr);
	}
}

std::shared_ptr<send_buffer> client_send_thread::alloc_send_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(send_pool_lock_);
	if (send_buffer_pool_list_.empty()){
		//缓存池已经清空，需要重新批量申请缓存
		for (uint32 i = 0; i < RUDP_BUFFER_POOL_SIZE; i++){
			std::shared_ptr<send_buffer> buffer_ptr(new send_buffer);
			buffer_ptr->index_ = 0;
			buffer_ptr->linker_handle_ = -1;
			buffer_ptr->size_ = 0;
			memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
			send_buffer_pool_list_.push_back(buffer_ptr);
		}
	}
	std::shared_ptr<send_buffer> buffer_ptr = send_buffer_pool_list_.front();
	send_buffer_pool_list_.pop_front();
	return buffer_ptr;
}

void client_send_thread::release_send_buffer(std::shared_ptr<send_buffer> buffer_ptr){
	std::lock_guard<std::recursive_mutex> gurad(send_pool_lock_);
	buffer_ptr->index_ = 0;
	buffer_ptr->linker_handle_ = -1;
	buffer_ptr->size_ = 0;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	send_buffer_pool_list_.push_back(buffer_ptr);
}

void client_send_thread::free_send_pool(){
	std::lock_guard<std::recursive_mutex> gurad(send_pool_lock_);
	send_buffer_pool_list_.clear();
}

bool client_send_thread::handle_check_linker_state(const struct sockaddr_in &addr){
	if (nullptr != on_check_linker_state_){
		return on_check_linker_state_(addr);
	}
	return false;
}

void client_send_thread::handle_set_index_state(const int &linker_handle, const uint64 &index){
	if (nullptr != on_set_index_state_){
		return on_set_index_state_(linker_handle, index);
	}
}

void client_send_thread::check_send_flow(const int32 &current_max_bandwidth){
	std::lock_guard<std::recursive_mutex> gurad(current_send_flow_lock_);
	current_send_flow_ = 0;
	max_bandwidth_ = current_max_bandwidth;
}

int32 client_send_thread::get_current_send_flow(){
	std::lock_guard<std::recursive_mutex> gurad(current_send_flow_lock_);
	return current_send_flow_;
}

bool client_send_thread::check_flow(const int32 &size){
	std::lock_guard<std::recursive_mutex> gurad(current_send_flow_lock_);

	//针对云游戏暂时不考虑带宽的限制 2021-04-05
	// if (current_send_flow_ + size > max_bandwidth_)
	// 	return false;
	// else
		current_send_flow_ += size;

	return true;
}

//normal管理（这里需要增加对于流量的判断）
std::shared_ptr<send_buffer> client_send_thread::get_normal_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(send_buffer_lock_);
	if (send_buffer_list_.empty())
		return nullptr;

	std::shared_ptr<send_buffer> buffer_ptr = send_buffer_list_.front();
	if (check_flow(buffer_ptr->size_)){
		send_buffer_list_.pop_front();
		return buffer_ptr;
	}
	return nullptr;
}

//加急数据
bool client_send_thread::add_buffer_addr_urgent(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr, bool followed){
	std::lock_guard<std::recursive_mutex> gurad(urgent_send_buffer_lock_);
	std::shared_ptr<send_buffer> buffer_ptr = alloc_send_buffer();
	buffer_ptr->feedback_ = true;
	buffer_ptr->followed_ = followed;
	buffer_ptr->index_ = index;
	buffer_ptr->message_type_ = message_type;
	buffer_ptr->linker_handle_ = linker_handle;
	buffer_ptr->size_ = size;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	memcpy(buffer_ptr->buffer_, data, size);
	memset(&buffer_ptr->remote_addr_, 0, sizeof(buffer_ptr->remote_addr_));
	memcpy(&buffer_ptr->remote_addr_, &addr, sizeof(addr));
	urgent_send_buffer_list_.push_back(buffer_ptr);
	return true;
}

bool client_send_thread::add_buffer_no_feedback_urgent(char *data, int size, int linker_handle, struct sockaddr_in addr){
	std::lock_guard<std::recursive_mutex> gurad(urgent_send_buffer_lock_);
	std::shared_ptr<send_buffer> buffer_ptr = alloc_send_buffer();
	buffer_ptr->feedback_ = false;
	buffer_ptr->linker_handle_ = linker_handle;
	buffer_ptr->size_ = size;
	memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
	memcpy(buffer_ptr->buffer_, data, size);
	memset(&buffer_ptr->remote_addr_, 0, sizeof(buffer_ptr->remote_addr_));
	memcpy(&buffer_ptr->remote_addr_, &addr, sizeof(addr));
	urgent_send_buffer_list_.push_back(buffer_ptr);
	return true;
}

//加急管理
void client_send_thread::free_urgent_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(urgent_send_buffer_lock_);
	urgent_send_buffer_list_.clear();
}

std::shared_ptr<send_buffer> client_send_thread::get_urgent_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(urgent_send_buffer_lock_);
	std::shared_ptr<send_buffer> buffer_ptr = urgent_send_buffer_list_.front();
	if (check_flow(buffer_ptr->size_)){
		urgent_send_buffer_list_.pop_front();
		return buffer_ptr;
	}
	return nullptr;
}

send_buffer_state client_send_thread::urgent_send_dispense(){
	while (true){
		if (urgent_send_buffer_list_.empty())
			return SNED_CONTINUE;

		std::shared_ptr<send_buffer> buffer_ptr = get_urgent_buffer();
		if (nullptr == buffer_ptr)
			return SNED_BANDLIMIT;

		//判断当前这个地址所代表的链接是否正常;
		if (handle_check_linker_state(buffer_ptr->remote_addr_)){
			int ret = sendto(fd_, buffer_ptr->buffer_, buffer_ptr->size_, 0, (struct sockaddr *)&(buffer_ptr->remote_addr_), sizeof(struct sockaddr_in));
			if (buffer_ptr->feedback_){
				handle_set_index_state(buffer_ptr->linker_handle_, buffer_ptr->index_);
			}
			if (SOCKET_ERROR == ret && EAGAIN == errno){
				add_log(LOG_TYPE_ERROR, "Socket Send Pool Fill EAGAIN");
			}
			if (SOCKET_ERROR == ret && EINTR == errno){
				add_log(LOG_TYPE_ERROR, "Socket Send Pool Fill EINTR");
			}
			if (SOCKET_ERROR == ret || 0 == ret){
				std::string remote_ip = ustd::rudp_public::get_remote_ip(buffer_ptr->remote_addr_);
				int remote_port_ = ustd::rudp_public::get_remote_port(buffer_ptr->remote_addr_);
				add_log(LOG_TYPE_ERROR, "Socket Send Failed IP=%s Port=%d", remote_ip.c_str(), remote_port_);
				close_fd();
				break;
			}
		}
		release_send_buffer(buffer_ptr);
	}
	return SNED_CONTINUE;
}

void client_send_thread::normal_send_dispense(){
	while (true){
		if (send_buffer_list_.empty())
			return ;

		//获取数据
		std::shared_ptr<send_buffer> buffer_ptr = get_normal_buffer();
		if (nullptr == buffer_ptr)
			return ;

		//判断当前这个地址所代表的链接是否正常;
		if (handle_check_linker_state(buffer_ptr->remote_addr_)){
			int ret = sendto(fd_, buffer_ptr->buffer_, buffer_ptr->size_, 0, (struct sockaddr *)&(buffer_ptr->remote_addr_), sizeof(struct sockaddr_in));
			if (buffer_ptr->feedback_){
				handle_set_index_state(buffer_ptr->linker_handle_, buffer_ptr->index_);
			}
			if (SOCKET_ERROR == ret && EAGAIN == errno){
				add_log(LOG_TYPE_ERROR, "Socket Send Pool Fill EAGAIN");
			}
			if (SOCKET_ERROR == ret && EINTR == errno){
				add_log(LOG_TYPE_ERROR, "Socket Send Pool Fill EINTR");
			}
			if (SOCKET_ERROR == ret || 0 == ret){
				std::string remote_ip = ustd::rudp_public::get_remote_ip(buffer_ptr->remote_addr_);
				int remote_port_ = ustd::rudp_public::get_remote_port(buffer_ptr->remote_addr_);
				add_log(LOG_TYPE_ERROR, "Socket Send Failed IP=%s Port=%d", remote_ip.c_str(), remote_port_);
				close_fd();
				break;
			}
		}
		release_send_buffer(buffer_ptr);
	}
}

void client_send_thread::send_dispense(){
	if (urgent_send_buffer_list_.empty() && send_buffer_list_.empty())
		return;

	//发送紧急数据
	send_buffer_state send_state = urgent_send_dispense();
	if (SNED_CONTINUE == send_state){
		//发送普通数据
		normal_send_dispense();
	}
}

void client_send_thread::execute(){
	current_state_ = tst_runing;
	while (tst_runing == current_state_){
		send_dispense();
		ustd::rudp_public::sleep_delay(SEND_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void client_send_thread::add_log(const int log_type, const char *context, ...){
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

