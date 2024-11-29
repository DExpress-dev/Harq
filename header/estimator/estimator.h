#pragma once

#ifndef estimator_h
#define estimator_h

#include <thread>
#include <map>
#include <list>
#include <mutex>

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#if defined(_WIN32)

	#include <windows.h>
	#include <algorithm>
	#undef max
#else

#endif

#pragma pack(push, 1)

//传输接收到的数据包
struct body_buffer
{
	uint64 index; 				//数据包序号
	uint64 size; 				//数据包大小
	rudptimer send_timer;		//数据包发送时间
	rudptimer recv_timer;		//数据包接收时间
};
typedef std::map<rudptimer, std::shared_ptr<body_buffer>> body_map;
typedef std::list<std::shared_ptr<body_buffer>> body_list;

#pragma pack(pop)
	
#endif /* estimator_h */

