#pragma once

#ifndef kalman_estimator_h
#define kalman_estimator_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>
#include <map>
#include <mutex>

#include "../../../header/write_log.h"
#include "estimator.h"

#if defined(_WIN32)

	#include <windows.h>
	#include <algorithm>
	#undef max

#endif

#define VCM_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define VCM_MIN(a, b) (((a) < (b)) ? (a) : (b))

struct kalman_buffer
{
	body_buffer buffer_;
	kalman_buffer *next_;
};

const double PHI							= 0.97;	
const double PSI							= 0.9999;
const double NOISE_STDDEVS					= 2.33;		//噪声系数
const uint16 MAX_ALPHA_COUNT				= 400;
const uint8 FS_START_COUNT					= 5;
const uint16 POST_KALMAN_ESTIMATE			= 200;
const double MIN_THETA_LOW					= 0.000001;

const double _noiseStdDevOffset 			= 30;		//噪声扣除常数
const double _time_deviation_upper_bound 	= 3.5;
const uint8 _numStdDevDelayOutlier 			= 15;
const uint8 _numStdDevFrameSizeOutlier 		= 3;
const uint32 _kStartupDelaySamples 			= 30;

const int32 BITRATE_TIMER					= 5;		//统计时间;

//********卡尔曼预估算法
class kalman_estimator
{
public:
	kalman_estimator();
	~kalman_estimator(void);
	void init();

public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();
	void dispense();

public:
	std::recursive_mutex buffer_lock_;
	kalman_buffer *first_ = nullptr, *last_ = nullptr;
	void add_queue(	uint64 index, uint64 size, rudptimer send_timer, rudptimer recv_timer);
	void free_queue();

private:
	uint64 statistics_frame_size(rudptimer *dT_send_timer, rudptimer *dT_recv_timer);

private:
	double theta_[2];							//估计参数（信道传输速率, 网络排队延迟）
	double thetaCov_[2][2];  					//预估协方差
  	double Qcov_[2][2];      					//噪声协方差
	uint64 maxFrameSize_;						//自回话开始以来收到的最大帧大小
	uint64 fsSum_;								//总帧大小
  	uint64 fsCount_;							//计算次数
	double avgFrameSize_;						//平均帧大小
	double varNoise_;							//噪声的方差
	double varFrameSize_;						//帧大小的方差
	uint64 prevFrameSize_;						//上一次的帧大小

private:
	uint16 startupCount_;
	double prevEstimate_;     					//上一次返回的抖动预估值;
	rudptimer lastUpdateTimer_;
	uint32 alphaCount_;
	double avgNoise_;         					//随机抖动的平均值
	void reset();

public:
	double filterJitterEstimate_;  				//抖动估计值的滤波和

private:
	void kalmanEstimateChannel(rudptimer frameDelayMS, int64 deltaFSBytes);
	void estimateRandomJitter(double deviation);
	double calculateEstimate();
	double noiseThreshold();
	double getFrameRate();

public:
	//*到达时间滤波器*/
	//使用kalman滤波对于延迟变化做平滑处理;
	void kalmanFrameDelayMS();

public:
	//时间管理;
	std::recursive_mutex bitrate_lock_;
	body_map body_map_;
	void add_bitrate_buffer(body_buffer buffer_ptr);
	void free_bitrate_buffer();
	void convert_buffer(kalman_buffer *work_ptr);

public:
	time_t last_show_bitrate_timer_;
	void checkShowed();
	void showBitrate(uint64 frameSizeBytes, rudptimer frameDelayMS);

public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
};
	
#endif /* kalman_estimator_h */

