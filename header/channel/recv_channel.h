#pragma once

#ifndef recv_channel_h
#define recv_channel_h

#include <map>
#include <thread>

#include "../../../header/write_log.h"
#include "../include/rudp_def.h"
#include "../include/rudp_public.h"
#include "../fec/matrix.h"
#include "../fec/fec_coder.h"
#include "../estimator/kalman_estimator.h"
#include "../estimator/trendline_estimator.h"

typedef std::function<void (std::shared_ptr<frames_record> frames_ptr)> ON_ADD_FRAMES;

struct recv_channel_buffer{
	std::shared_ptr<packet_record> packet_ptr_;
	recv_channel_buffer *next_;
};

class recv_channel{
public:
	ON_ADD_FRAMES on_add_frames_ = nullptr;
	ON_ADD_LOG on_add_log_ = nullptr;
	void handle_add_frames(std::shared_ptr<frames_record> frames_ptr);
	void handle_add_log(const int log_type, const char *context);
public:
	recv_channel();
	~recv_channel(void);
	void init();
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void dispense();
public:
	std::recursive_mutex buffer_lock_;
	recv_channel_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created);
	void free_queue();
public:
	//接收到的packet包管理;
	segment_packet_map segment_packet_map_;
	void add_packet(std::shared_ptr<packet_record> packet_ptr);
	bool exists_packet(const uint64 &index);
	std::shared_ptr<packet_record> find_packet(const uint64 &index);
	std::shared_ptr<packet_record> pop_packet(const uint64 &index);
	void delete_packets(const uint64 &frames_no);
	void free_packet();
private:
	//统计丢包和使用包的数量;
	std::recursive_mutex packet_static_lock_;
	uint32 loss_packet_size_ = 0;
	uint32 last_loss_packet_size_ =0;
	uint32 use_packet_size_ = 0;
	uint32 last_use_packet_size_ =0;
	rudptimer last_check_packet_static_timer_;
	void inc_loss_packet_size();
	void inc_use_packet_size();
private:
	void check_packet_static();
public:
	//包检测;
	void check_packet();
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
public:
	//完成的index管理;
	std::recursive_mutex index_lock_;
	uint64 complete_index_ = 0;
	void set_complete_index(uint64 index);
	uint64 get_complete_index();
public:
	uint32 loss_packet_count();
	uint32 recv_packet_count(); 
	uint32 loss_packet_interval();
	uint32 recv_packet_interval();
public:
	//kalman_estimator *estimator_ptr_ = nullptr;
	trendline_estimator *estimator_ptr_ = nullptr;
public:
	uint64 get_min_index();
	uint64 get_max_index();
	const uint64 get_index_size() const		{return static_cast<uint64>(segment_packet_map_.size());}

};

#endif /* recv_channel_h */



