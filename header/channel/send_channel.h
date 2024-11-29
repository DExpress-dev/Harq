#pragma once

#ifndef send_channel_h
#define send_channel_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <map>
#include <thread>
#include <vector>
#include <limits.h>

#include "../../../header/write_log.h"
#include "../fec/matrix.h"
#include "../fec/fec_coder.h"
#include "../estimator/kalman_estimator.h"
#include "../estimator/loss_estimator.h"
#include "../estimator/trendline_estimator.h"
#include "../linefit/polyfit.h"

#if defined(_WIN32)

	#include <windows.h>
	#include <algorithm>
	#undef max

#endif

typedef std::function<void (const uint64 &frames_id)> ON_SET_FRAMES_ID;
typedef std::function<void (const uint64 &index)> ON_SET_SEGMENT_INDEX;
typedef std::function<void (uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr)> ON_ADD_SEND_QUEUE;
typedef std::function<void (std::shared_ptr<confirm_buffer> confirm_buffer_ptr, bool followed)> ON_RESEND;
typedef std::function<void (uint8 message_type, uint64 index, char* data, int size, struct sockaddr_in addr)> ON_FEC_FINISHED;
typedef std::function<void (int size)> ON_SEND_OVERALL;
typedef std::function<void (int size)> ON_SEND_USEFUL;

typedef std::map<uint64, std::shared_ptr<confirm_buffer>> confirm_buffer_map;
typedef std::map<uint64, uint64> trouble_map;

//FEC编码类
class fec_encoder;
class buffer_fec_thread;
class send_channel;

struct fec_buffer{
	frames_buffer* frames_buffer_;
	struct sockaddr_in addr_;
	fec_buffer *next_;
};

struct fast_sack_buffer{
	uint8 message_type_;
	sack_rudp_header sack_header_;
	ack_rudp_header ack_header_;
	fast_sack_buffer *next_;
};

//发送频道的FEC计算线程（这里使用多个单独线程原因是因为FEC计算耗时，使用线程可以同时对多个分组进行计算）
class fec_encoder_thread{
public:
	fec_encoder *fec_encoder_ptr_ = nullptr;
	send_channel *send_channel_ = nullptr;
	fec_encoder_thread(send_channel *parent);
	~fec_encoder_thread(void);
	void init();
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void check_encoder();
	void send_error_packet(const uint64 &index, const struct sockaddr_in &addr);
public:
	std::recursive_mutex fec_lock_;
	fec_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(frames_buffer* frames_buffer_ptr, struct sockaddr_in addr);
	void free_queue();
public:
	void add_log(const int log_type, const char *context, ...);
};

typedef std::vector<fec_encoder_thread*> fec_encoder_vector;
typedef std::list<frames_buffer*> buffer_pool_list;

const uint32 MAX_WAIT_TIMER = 5;

//带宽的5中趋势点
enum bandwidth_trend
{		
	btNode = 0,					//初始期
	btRise = 1,					//带宽上升期
	btRiseTransition = 2,		//带宽上升转折期（从上升->平稳）			
	btMaintain = 3,				//带宽平稳期
	btDeclineTransition = 4,	//带宽下降转折期（从平稳->下降）
	btDecline = 5				//带宽需要下降
};

enum detection_state
{
	dsMulti	= 1,				//乘法探测期
	dsLine	= 2					//线性探测期
};
const double line_gain_ = 32 * KB;
const double multi_gain_ = 1.1;

//判断逻辑;
//1：拟合后，斜率大于1.05，则进行乘积增加
//2：拟合后，斜率小于0.95，则线性增加（翻转，增肌）
//3：当前带宽不能大于拟合后数据太远
class analysis_bandwidth{
public:
	send_channel *send_channel_ = nullptr;
	analysis_bandwidth(send_channel *parent);
	~analysis_bandwidth(void);
private:
	//远端采样点记录
	const uint32 max_null_header_count_ = 120;
	std::recursive_mutex null_header_list_lock_;
	std::list<std::shared_ptr<null_rudp_header>> null_header_list_;
	void free_null_header();
	std::shared_ptr<null_rudp_header> get_last_null_header();
private:
	//带宽处理
	uint32 multi_bandwidth(const uint32 &bandwidth);
	uint32 add_bandwidth(const uint32 &bandwidth);
	uint32 limit_bandwidth(const uint32 &bandwidth);
private:
	//状态信息
	detection_state current_detection_state_ = dsMulti;
	uint32 start_line_poly_bandwidth_ = 0;
	uint32 finish_line_poly_bandwidth_ = 0;
private:
	//线性拟合获取可用带宽;
	polyfit *polyfit_ptr_ = nullptr;
	double_vector polyfit_bandwidth();
	double poly_point_trend(double_vector poly_vector);
public:
	void add_null_header(null_rudp_header null_header);
	uint32 poly_bandwidth();
};

class send_channel{
public:
	ON_SET_FRAMES_ID on_set_frames_id_ = nullptr;
	ON_SET_SEGMENT_INDEX on_set_segment_index_ = nullptr;
	ON_ADD_SEND_QUEUE on_add_send_queue_ = nullptr;
	ON_ADD_SEND_QUEUE on_add_send_queue_no_feekback_ = nullptr;
	ON_RESEND on_resend_ = nullptr;
	ON_SEND_OVERALL on_send_overall_ = nullptr;
	ON_SEND_USEFUL on_send_useful_ = nullptr;
	void handle_set_frames_id(const uint64 &frames_id);
	void handle_set_segment_index(const uint64 &index);
	void handle_resend(std::shared_ptr<confirm_buffer> confirm_buffer_ptr, bool followed);
	void handle_fec_finished(uint8 message_type, uint64 index, char* data, int size, struct sockaddr_in addr);
	void handle_on_send_overall(int size);
	void handle_on_send_useful(int size);
public:
	int linker_handle_ = -1;
	send_channel(int linker_handle, uint32 start_bandwdith, uint32 max_bandwidth);
	~send_channel(void);
private:
	uint64 complete_group_id_ = 0;
	uint64 send_max_segment_index_ = 0;
public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void init();
	void execute();
	void dispense();
public:
	//缓冲池管理;
	std::recursive_mutex buffer_pool_lock_;
	buffer_pool_list buffer_pool_list_;
	void init_buffer_pool();
	frames_buffer* alloc_buffer();
	void release_buffer(frames_buffer* buffer_ptr);
	void free_pool();
public:
	//获取实时RTO;
	float real_timer_rtts_ = 0;
	float real_timer_rttd_ = 0;
	uint64 real_timer_rto_ = MIN_RTO;
	uint16 get_local_rto();
	uint64 bound(const uint64 &lower, const uint64 &middle, const uint64 &upper);
	void calculate_rto(const rudptimer &send_timer);
public:
	//编码线程管理;
	uint16 fec_thread_count_;
	fec_encoder_vector fec_threads_vector_;
	void init_fec_threads();
	void free_fec_threads();
	uint16 get_fec_postion(uint64 index); 
	fec_encoder_thread* get_fec_thread(uint64 index);
public:
	//快速重发;
	void fast_retransmission();
	void ack_confirm(ack_rudp_header ack_header_ptr);
	void check_sack_fast_retransmission(sack_rudp_header sack_header_ptr);
public:
    //设置数据包的状态
	void set_confirm_buffer_wait(const uint64 &index);
private:
	//拥塞带宽发送判断
	int block_channel_bandwidth(const int &size);
	//拥塞确认带宽发送判断
	int block_confirm_bandwidth(const int &size);
public:
	int block_bandwidth(const int &size);
public:
	//确认包管理;
	std::recursive_mutex confirm_buffer_lock_;
	confirm_buffer_map confirm_buffer_map_;
	void add_confirm_buffer(frames_buffer* buffer_ptr);
	std::shared_ptr<confirm_buffer> find_confirm_buffer(const uint64 &index);
	void delete_confirm_buffer();
	void free_confirm_buffer();
	uint64 min_confirm_index();
	uint64 max_confirm_index();
public:
	//显示当前等待确认的数据包长度;
	time_t last_showed_wait_confirm_time_;
	void showed_wait_confirm();
	bool confirm_windows_threshold(windows_math_mode windows_mode);
	bool add_buffer_to_send_channel(uint64 frames_no, char *data, int size, struct sockaddr_in addr);
public:
	//确认管理(ACK确认和SACK确认)
	std::recursive_mutex buffer_lock_;
	fast_sack_buffer *first_ = nullptr, *last_ = nullptr;
	void add_sack_queue(sack_rudp_header sack_header);
	void add_ack_queue(ack_rudp_header ack_header);
	void free_queue();
/*这里进行了分开处理，因为发送线程不能及时进行返回，因此使用发送线程和发送频道的流量控制*/
private:
	//发送频道流量控制
	std::recursive_mutex send_chennel_flow_lock_;
	uint64 send_chennel_flow_ = 0;
	void reset_send_chennel_flow();
private:
	//发送线程流量控制
	std::recursive_mutex send_thread_flow_lock_;
	uint64 send_thread_flow_ = 0;
	void reset_send_thread_flow();
public:
	void check_bandwidth();
	void check_flow();
	uint64 get_current_send_thread_flow();
public:
	void add_send_chennel_flow(const uint64 &flow);
	void add_send_thread_flow(const uint64 &flow);
	bool is_send_chennel_threshold(const uint64 &size);
	bool is_send_thread_threshold(const uint64 &size);
public:
	uint32 start_bandwidth_ = 1 * MB;
	uint32 max_bandwidth_ = MAX_BANDWIDTH;
	uint32 get_max_bandwidth();
	uint32 get_start_bandwidth();
public:
	std::recursive_mutex current_max_bandwidth_lock_;
	uint32 current_max_bandwidth_ = start_bandwidth_;
	void set_current_max_bandwidth(const uint32 &bandwidth);
	uint32 get_current_max_bandwidth();
public:
	analysis_bandwidth *analysis_bandwidth_ptr_ = nullptr;
public:
	uint64 loss_index_;
	uint64 current_loss_index_;
	void set_remote_nul(null_rudp_header null_header);
public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
private:
	std::recursive_mutex segment_index_lock_;
	uint64 current_segment_index_ = INIT_SEGMENT_INDEX;
	uint64	get_segment_index();
public:
	const uint64 get_complete_group_id() const 						{return complete_group_id_;}
	const uint64 get_confirm_buffer_size() const 					{return static_cast<uint64>(confirm_buffer_map_.size());}
	const uint64 get_send_max_index() const							{return send_max_segment_index_;}
	void set_complete_group_id(const uint64 &group_id)				{complete_group_id_ = std::max(complete_group_id_, group_id);}
	void set_segment_index(const uint64 &index)						{current_segment_index_ = index;}
	void set_send_max_index(const uint64 &index)					{send_max_segment_index_ = index;}
};

#endif /* send_channel_h */

