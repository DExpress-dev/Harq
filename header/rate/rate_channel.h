#pragma once

#ifndef rate_channel_h
#define rate_channel_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>
#include <map>
#include <list>

#include "../../../header/write_log.h"

class rate_channel
{
public:
	rate_type rate_type_;
	rate_channel(rate_type mode);
	~rate_channel(void);

private:
	//当前数据管理;
	std::recursive_mutex current_data_lock_;
	uint64 useful_size_ = 0;			//有用数据量;
	uint64 overall_size_ = 0;			//整体数据量;

public:
	//加入整体数据;
	void insert_overall(const uint64 &overall_size);
	void insert_useful(const uint64 &useful_size);
	void reset_rate();
	uint64 get_overall();
	uint64 get_useful();
    
};

#endif /* rate_channel_h */

