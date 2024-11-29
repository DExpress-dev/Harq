#pragma once

#ifndef trendline_estimator_h
#define trendline_estimator_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>
#include <map>
#include <list>
#include <mutex>

#include "../../../header/write_log.h"
#include "estimator.h"

#if defined(_WIN32)

	#include <windows.h>
	#include <algorithm>
	#undef max

#endif

//********趋势线预估算法
struct trendline_buffer
{
	body_buffer buffer_;
	trendline_buffer *next_;
};

struct packet_timing
{
	double arrival_time_ms_;
    double smoothed_delay_ms_;
    double raw_delay_ms_;
};
typedef std::map<double, std::shared_ptr<packet_timing>> packet_timing_map;

enum BandwidthUsage
{
	kNone = 0,
	kBwNormal = 1,
	kBwUnderusing = 2,			//带宽未充分利用
	kBwOverusing = 3,			//带宽使用过载
	kLast
};

struct packet_group
{
	uint64 size_;					//分组数据大小;
	rudptimer first_timestamp_;		//第一个数据包的发送时间
	rudptimer timestamp_;			//分组中的发送时间;
	rudptimer first_arrival_ms_;	//第一个数据包的接收时间
	rudptimer arrival_time_ms_;		//分组中的接收时间;
	rudptimer complete_time_ms_;	//分组的完成时间;
	rudptimer last_system_time_ms_;
};

const int32 kDeltaCounterMax 					= 1000;
const int32 window_size							= 100;
const int32 CHECK_TIMER							= 5;		
const double kDefaultTrendlineSmoothingCoeff 	= 0.9;
const double kDefaultTrendlineThresholdGain 	= 4.0;
const double kMaxAdaptOffsetMs 					= 15.0;
const int32 kMinNumDeltas 						= 60;
const double kOverUsingTimeThreshold			= 10;
const double k_up 								= 0.0087;
const double k_down 							= 0.039;
const rudptimer kMaxTimeDeltaMs 				= 100;
const int64_t kArrivalTimeOffsetThresholdMs 	= 3000;
const int kReorderedResetThreshold 				= 3;
const int kBurstDeltaThresholdMs 				= 5;
const int kMaxBurstDurationMs 					= 100;
const int kTimestampGroupLengthMs 				= 5;
const int kAbsSendTimeFraction 					= 18;
const int kAbsSendTimeInterArrivalUpshift 		= 8;
const int kInterArrivalShift = kAbsSendTimeFraction + kAbsSendTimeInterArrivalUpshift;
const int kTimestampGroupTicks = (kTimestampGroupLengthMs << kInterArrivalShift) / 1000;
const double kTimestampToMs = 1000.0 / static_cast<double>(1 << kInterArrivalShift);

/*拥塞控制算法处理(带宽控制的核心算法)*/
class trendline_estimator
{
public:
	trendline_estimator();
	~trendline_estimator(void);
	void init();

private:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void dispense();

public:
	std::recursive_mutex buffer_lock_;
	trendline_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(uint64 index, uint64 size, rudptimer send_timer, rudptimer recv_timer);
	void free_queue();

private:
	int32 num_of_deltas_ = 0;
	rudptimer first_arrival_time_ms_ = -1;
	double accumulated_delay_ = 0;
	double smoothed_delay_ = 0;
	double prev_trend_;
	void reset();

private:
	std::list<std::shared_ptr<packet_timing>> delay_hist_;
	void add_delay_hist(double time_deltas, double smoothed_delay_ms, double raw_delay_ms);

private:
	//*到达时间滤波器*/
	void UpdateTrendline(double recv_delta_ms, double send_delta_ms, rudptimer send_time_ms, rudptimer arrival_time_ms, int packet_size);
	bool linearFitSlope(double *trend);

private:
	bool ComputeDeltas( rudptimer timestamp, 			//包发送时间
						rudptimer arrival_time_ms,		//包到达时间
						rudptimer system_time_ms,		//当前时间
						int32 packet_size,				//包大小
						double* timestamp_delta,		//包组发送时间差
						double* arrival_time_delta_ms, 	//包组到达时间差
						double* packet_size_delta);		//包组大小差值

	bool BelongsToBurst(rudptimer arrival_time_ms, rudptimer timestamp);
	bool NewTimestampGroup(rudptimer arrival_time_ms, rudptimer timestamp); 
	void groupReset();

private:
	rudptimer LatestTimestamp(rudptimer timestamp1, rudptimer timestamp2);
	bool IsNewerTimestamp(rudptimer timestamp, rudptimer prev_timestamp);
	bool IsNewer(rudptimer value, rudptimer prev_value);

private:
	bool IsFirstPacket(packet_group timestamp_group);
	bool PacketInOrder(rudptimer timestamp);

private:
	double prev_modified_trend_;
	double threshold_;
	double time_over_using_;
	int overuse_counter_;
	BandwidthUsage hypothesis_;
	BandwidthUsage hypothesis_predicted_;
	rudptimer last_update_ms_;
	void detect(double trend, double ts_delta, rudptimer now_ms);

private:
	void updateThreshold(double modified_trend, rudptimer now_ms);
	double bound(const double &lower, const double &middle, const double &upper);

public:
	bool math_state_ = false;
	BandwidthUsage state();
	std::string state_string();

private:
	//时间管理;
	std::recursive_mutex bitrate_lock_;
	body_list body_list_;
	void add_bitrate_buffer(body_buffer buffer_ptr);
	void free_bitrate_buffer();
	void convert_buffer(trendline_buffer *work_ptr);

private:
	rudptimer prev_check_send_timer_ = 0;
	int num_consecutive_reordered_packets_;
	packet_group prev_timestamp_group_;	//上一个时间分组;
	packet_group current_timestamp_group_;	//当前时间分组;

private:
	time_t last_check_timer_ = time(nullptr);
	void check_current_state();

public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
};
	
#endif /* trendline_estimator_h */

