#pragma once

#ifndef RUDP_GROUP_H_
#define RUDP_GROUP_H_

#include "../include/rudp_def.h"

#include <map>
#include <mutex>
#include <thread>

#include "../../../header/write_log.h"

#include "../fec/matrix.h"
#include "../fec/fec_coder.h"

//回调函数定义;
typedef std::function<void (uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created)> ON_ADD_PACKET;
typedef std::function<void (const sack_rudp_header &sack)> ON_SEND_SACK;
typedef std::function<uint64 ()> ON_GET_REMOTE_RTO;
typedef std::function<uint64 ()> ON_GET_INDEX;

struct rudp_group_buffer{
	uint8 message_type_;
	char buffer_[BUFFER_SIZE];
	int size_;
	rudptimer recv_timer_;
	rudp_group_buffer *next_;
};

enum trouble_index_state{
	EXITSED, 		//存在状态;
	DELETED			//被删除状态
};

struct trouble_group{
	uint64 index_;
	trouble_index_state trouble_state_; 
	uint64 group_id_;
};
typedef std::map<uint64, std::shared_ptr<trouble_group>> check_trouble_map;

class fec_decoder;
class rudp_group;

//FEC解码线程
class fec_decoder_thread{
public:
	rudp_group *rudp_group_;
	uint64 decoder_thread_count_ = 0;
	uint64 decoder_thread_postion_ = 0;
	fec_decoder_thread(rudp_group *parent);
	~fec_decoder_thread(void);
	void init(const uint64 &thread_count, const uint64 &position);
public:
	//分组内容管理;
	fec_group_map fec_group_map_;
	bool add_frames_group(frames_buffer* frames_buffer_ptr);
	void add_error_group(error_buffer* error_buffer_ptr);
	bool exist_group_buffer(const uint64 &index);
	std::shared_ptr<fec_group_buffer> find_group(const uint64 &group_id);
	void free_group();
private:
	//解码对象;
	fec_decoder *fec_decoder_ptr_ = nullptr;
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void dispense();
public:
	void check_decoder();			//检测解码;
	bool check_group_sack();		//检测sack返回;
	void check_group();				//检测分组;
public:
	rudptimer last_check_sack_timer_;
	void poll_check_group_sack();	//批量检测sack;
public:
	void decoder_group(std::shared_ptr<fec_group_buffer> group_buffer);					//解码;
	uint64 get_follow_from_pointer(std::shared_ptr<fec_group_buffer> fec_group_ptr);	//得到分组follow;
	uint64 get_follow_from_id(const uint64 &group_id);	//得到分组follow;
private:
	bool handle_group_id(const uint64 &group_id);
private:
	//最大阈值判断;
	uint64 max_threshold_ = 0;
public:
	//管理正在检测的分组
	uint64 last_check_sack_group_id_ = 0;
	void set_sack_start_group_id(const uint64 &group_id);
	uint64 check_sack_start_group_id(const uint64 &start_group_id, const uint64 &end_group_id);
public:
	uint64 get_min_group_id();
	uint64 get_max_group_id();
	uint64 get_complete_group_id();
	uint64 get_group_size();
private:
	void handle_add_packet(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created);
public:
	std::recursive_mutex buffer_lock_;
	rudp_group_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer);
	void free_queue();
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
};

struct complete_group{
	uint64 group_id_;
	rudptimer complete_timer_;
	uint64 complete_position_;
};

typedef std::vector<fec_decoder_thread*> fec_decoder_vector;
typedef std::map<uint64, std::shared_ptr<complete_group>> complete_group_map;

class rudp_group{
public:
	//接口函数;
	ON_ADD_PACKET on_add_packet_ = nullptr;
	ON_SEND_SACK on_send_sack_ = nullptr;
	ON_GET_REMOTE_RTO on_remote_rto_ = nullptr;
	ON_GET_INDEX on_get_index_ = nullptr;
	ON_ADD_LOG on_add_log_ = nullptr;
	void handle_add_packet(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created);
	void handle_send_sack(const sack_rudp_header &sack);
	uint64 handle_get_remote_rto();
	uint64 handle_get_complete_index();
	void handle_add_log(const int log_type, const char *context);
public:
	rudp_group();
	~rudp_group(void);
	void init();
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void dispense();
public:
	// 队列
	std::recursive_mutex buffer_lock_;
	rudp_group_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer);
	void free_queue();
public:
	//完成的分组管理;
	std::recursive_mutex complete_group_lock_;
	complete_group_map complete_group_map_;
	void add_complete_group(const uint64 &group_id, const uint64 &postion);
	void check_complete_group();
	void free_complete_group();
	uint64 min_complete_group();
	uint64 max_complete_group();
	const uint64 complete_group_size() const			{return static_cast<uint64>(complete_group_map_.size());}
public:
	//完成的分组管理;
	std::recursive_mutex group_lock_;
	uint64 complete_group_id_ = 0;
	void set_complete_group_id(uint64 group_id);
	uint64 get_complete_group_id();
	uint64 next_complete_group_id();
	uint64 prev_complete_group_id();
public:
	uint64 remote_max_index_ = 0;
	void set_remote_max_index(uint64 max_index)			{remote_max_index_ = max_index;}
	const uint64 get_remote_max_index() const 			{return remote_max_index_;}
public:
	//解码分组管理 group_id % fec_thread_count_
	uint16 fec_decoder_count_;
	fec_decoder_vector fec_decoder_vector_;
	void init_fec_decoder_vector();
	void free_fec_decoder_vector();
	uint16 get_fec_decoder_postion(uint64 group_id); 
	fec_decoder_thread* get_fec_decoder(uint64 group_id);
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);

//********************************
//排错机制
public:
	time_t last_check_trouble_timer_;
	uint64 last_check_group_id_ = 0;
	void check_trouble();
	uint64 get_follow_segment(const uint64 &group_id);
	void self_trouble(const uint64 &start_index, const uint64 &end_index, const uint64 &current_index);
};

#endif  // RUDP_GROUP_H_
