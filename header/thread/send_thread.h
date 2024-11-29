
#pragma once

#ifndef send_thread_h
#define send_thread_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <list>
#include <thread>

#include "../../../header/write_log.h"

//用于判断数据是否可以进行继续发送的回调
typedef std::function<bool (struct sockaddr_in remote_addr)> ON_CHECK_LINKER_STATE;
//用于在发送数据成功后设置发送状态的回调
typedef std::function<void (const int &linker_handle, const uint64 &index)> ON_SET_INDEX_STATE;
//用于判断带宽限制的回调
typedef std::function<bool (const int &linker_handle, const uint64 &size)> ON_THRESHOLD;

struct send_buffer{
	bool followed_;
	bool feedback_;
	uint64 index_;
	uint8 message_type_;
	char buffer_[BUFFER_SIZE];
	int size_;
	int linker_handle_;
	struct sockaddr_in remote_addr_;
};
typedef std::list<std::shared_ptr<send_buffer>> send_buffer_list;

//客户端发送线程（多对象共用方式进行数据发送）;
enum send_buffer_state{
	SNED_BANDLIMIT,		//受到带宽限制
	SNED_CONTINUE		//可以继续发送
};

class client_send_thread{
public:
	ON_CHECK_LINKER_STATE on_check_linker_state_ = nullptr;
	ON_SET_INDEX_STATE on_set_index_state_ = nullptr;
	void handle_set_index_state(const int &linker_handle, const uint64 &index);
	bool handle_check_linker_state(const struct sockaddr_in &addr);
public:
	harq_fd fd_ = INVALID_SOCKET;
	int32 max_bandwidth_ = MAX_BANDWIDTH;
	client_send_thread();
	~client_send_thread(void);
	void init(const harq_fd &fd, const int32 &max_bandwidth);
	int32 get_current_send_flow();
	void check_send_flow(const int32 &current_max_bandwidth);
private:
	//发送流量管理;
	std::recursive_mutex current_send_flow_lock_;
	int32 current_send_flow_ = 0;
	bool check_flow(const int32 &size);
	//普通数据管理;
private:	
	std::recursive_mutex send_buffer_lock_;
	send_buffer_list send_buffer_list_;
	void free_normal_buffer();
	std::shared_ptr<send_buffer> get_normal_buffer();
public:
	bool add_buffer_addr(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr, bool followed = false);
	bool add_buffer_no_feedback(uint64 index, char *data, int size, int linker_handle, struct sockaddr_in addr);
	//加急数据管理;
private:
	std::recursive_mutex urgent_send_buffer_lock_;
	send_buffer_list urgent_send_buffer_list_;
	void free_urgent_buffer();
	std::shared_ptr<send_buffer> get_urgent_buffer();
public:
	bool add_buffer_addr_urgent(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr, bool followed = false);
	bool add_buffer_no_feedback_urgent(char *data, int size, int linker_handle, struct sockaddr_in addr);
private:
	//发送池管理;
	std::recursive_mutex send_pool_lock_;
	send_buffer_list send_buffer_pool_list_;
	void init_send_pool();
	std::shared_ptr<send_buffer> alloc_send_buffer();
	void release_send_buffer(std::shared_ptr<send_buffer> send_buffer_ptr);
	void free_send_pool();
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
private:
	void send_dispense();
	void normal_send_dispense();
	send_buffer_state urgent_send_dispense();
public:
	//测试使用的变量
	bool testChecked = false;
	void testCheck();
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
public:
	void close_fd() { fd_ = INVALID_SOCKET; }
};

#endif /* send_thread_h */



