#pragma once

#ifndef loss_estimator_h
#define loss_estimator_h

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

//丢包分为3个区段 
//1：丢包率 <= 2% 时， 带宽需要进行上升 (bw * 105%)
//2：丢包率 > 2% && < 10% 时，带宽保持不变
//3：丢包率 >= 10% 时，带宽需要降低  (bw * (1 - p%)) 其中 p% 为丢包率
const double loss_gain[] = {0.02, 0.1};

//********丢包预估算法
class loss_estimator
{
public:
	loss_estimator();
	~loss_estimator(void);
	void init();

public:
	uint32 max_bandwidth_ = MAX_BANDWIDTH;
	uint32 get_max_bandwidth();
	void set_max_bandwidth(const uint32 &max_bandwidth);

public:
	float current_loss_scale_ = 0;
	float get_current_loss_scale();
	uint64 loss_bandwidth(uint64 send_packet_count, uint64 loss_packet_count, uint64 current_bandwidth);

public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
};
	
#endif /* loss_estimator_h */

