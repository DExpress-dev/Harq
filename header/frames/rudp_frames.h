#pragma once

#ifndef rudp_fraems_hpp
#define rudp_fraems_hpp

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <map>
#include <mutex>
#include <thread>

#include "../../../header/write_log.h"

typedef std::function<void (char *data, int size, int consume_timer)> ON_HANDLE_RECV;
typedef std::function<void (int size)> ON_HANDLE_RECV_USEFUL;
typedef std::function<uint64 ()> ON_HANDLE_FIRST_RTO;
typedef std::function<int32 ()> ON_HANDLE_SYS_SERVER_TIMER_STAMP;
typedef std::function<int32 ()> ON_HANDLE_SYS_CLIENT_TIMER_STAMP;

struct frames_class_buffer{
	std::shared_ptr<frames_record> frames_ptr_;
	frames_class_buffer *next_;
};

class frames_class{
public:
	ON_HANDLE_RECV on_handle_recv_ = nullptr;
	ON_HANDLE_RECV_USEFUL on_handle_recv_useful_ = nullptr;
	ON_HANDLE_FIRST_RTO on_first_rto_ = nullptr;
	ON_HANDLE_SYS_SERVER_TIMER_STAMP on_sys_server_timer_stamp_ = nullptr;
	ON_HANDLE_SYS_CLIENT_TIMER_STAMP on_sys_client_timer_stamp_ = nullptr;
	void handle_recv(rudptimer frame_timer, rudptimer recv_timer, char *data, int size);
	void handle_recv_useful(int size);
	uint64 handle_first_rto();
	int32 handle_sys_server_timer_stamp();
	int32 handle_sys_client_timer_stamp();
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void dispense();
public:
	service_mode service_mode_;
	void set_service_mode(const service_mode &mode);
	service_mode get_service_mode();
public:
	std::recursive_mutex buffer_lock_;
	frames_class_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(std::shared_ptr<frames_record> frames_ptr);
	void free_queue();
private:
	time_t last_static_timer_ = time(nullptr);
	void frames_static();
public:
	bool delay_ = false;
	int delay_interval_ = 2000;
	bool start_ = false;
	rudptimer local_first_timer_ = 0;
	rudptimer remote_first_timer_ = 0;
	int cumulative_timer_ = 0;
	frames_class(service_mode mode);
	~frames_class(void);
	void init(bool delay = false, int delay_interval = 2000, int cumulative_timer = 10);
public:
	//设置客户端的时间信息
	rudptimer remote_base_timer_ = 0;
	rudptimer local_base_timer_ = 0;
	void set_base_timer(rudptimer remote_timer, rudptimer local_timer);
public:
	//计算最大和最小消耗时间;
	rudptimer min_cumulative_timer_ = 10000;
	rudptimer max_cumulative_timer_ = 0;
	rudptimer deal_min_cumulative_timer_ = 10000;
	rudptimer deal_max_cumulative_timer_ = 0;
	rudptimer total_cumulative_timer_ = 0;
	uint64 frequency_ = 0;
	void init_cumulative_timer();
	void set_cumulative_timer(const rudptimer &cumulative_timer);
	void set_deal_cumulative_timer(const rudptimer &cumulative_timer);
	rudptimer get_average_cumulative_timer();
	const rudptimer get_min_cumulative_timer() const 		{return min_cumulative_timer_;}
	const rudptimer get_max_cumulative_timer() const 		{return max_cumulative_timer_;}
	const rudptimer get_deal_min_cumulative_timer() const 		{return deal_min_cumulative_timer_;}
	const rudptimer get_deal_max_cumulative_timer() const 		{return deal_max_cumulative_timer_;}
private:
	//显示统计信息;
	rudptimer last_check_static_timer_ = 0;
	void check_cumulative_timer();
public:
	void get_cumulative_timer(rudptimer *min_timer, rudptimer *max_timer, rudptimer *average_timer);
private:
	//已经完成的frames_no管理;
	std::recursive_mutex complete_frames_no_lock_;
	uint64 current_frames_no_ = INIT_FRAMES_NO;
	rudptimer current_frames_no_timer_;
	void set_frames_no(const uint64 &frames_no);
	uint64 get_current_frames_no();
	rudptimer get_current_frames_no_timer();
public:
	//检测和弹出帧管理;
	void pop_frames(std::shared_ptr<frames_record> frames_ptr);
	void check_frames();
public:
	//帧管理;
	frames_map complete_frames_map_;
	void add_complete_frames(std::shared_ptr<frames_record> frames_ptr);
	bool exists_frames(uint64 frames_no, rudptimer &frame_timer);
	std::shared_ptr<frames_record> pop_complete_frames(const uint64 &frames_no);
	std::shared_ptr<frames_record> find_complete_frames(const uint64 &frames_no);
	void delete_complete_frames();
	void free_complete_frames();
public:
	void get_frames_min_max_index(uint64 frames_no, uint64 &min_index, uint64 &max_index);
	uint64 get_complete_max_frames_no();
	uint64 get_complete_min_frames_no();
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
};

#endif /* rudp_fraems_hpp */

