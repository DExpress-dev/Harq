#pragma once

#ifndef recv_thread_h
#define recv_thread_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>

#include "../../../header/write_log.h"

typedef std::function<bool (uint8 message_type, char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer)> ON_SYN;
typedef std::function<bool (uint8 message_type, char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer)> ON_DATA;

class recv_thread{
public:
	service_mode mode_ = SERVER;
	ON_SYN on_syn_ = nullptr;
	ON_DATA on_data_ = nullptr;
	void handle_recv(char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer);
public:
	harq_fd fd_ = INVALID_SOCKET;
	recv_thread(service_mode mode);
	~recv_thread(void);
	void init(const harq_fd &fd);
	bool is_abnormal_header2(char* data, int size, uint8 &message_type);
	bool is_abnormal_header(char* data, int size, uint8 &message_type);
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
};

#endif /* recv_thread_h */



