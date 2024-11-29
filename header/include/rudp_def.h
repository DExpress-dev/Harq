#pragma once

#if defined(_WIN32)

	#include <Winsock2.h>

	#include <memory>
	#include <map>
	#include <list>
	#include <mutex>
	#include <stdint.h>

	typedef SOCKET	harq_fd;
#else

	#include <memory>
	#include <map>
	#include <list>
	#include <mutex>
	#include <stdint.h>

	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <unistd.h>
	#include <functional>
	typedef int	harq_fd;
#endif

#if defined(_WIN32)

	#define harq_vsprintf(a, b, c) vsprintf(a, b, c)
	#define harq_sprintf sprintf
	#define harq_close(a) closesocket(a)
	#define harq_sleep(a) Sleep(a)
#else

	#define harq_vsprintf(a, b, c) vsprintf(a, b, c)
	#define harq_sprintf sprintf
	#define harq_close(a) close(a)
	#define harq_sleep(a) usleep(a)
#endif

#pragma pack(push, 1)

//类型定义;
typedef unsigned char			uint8;		//范围（0 ~ 255 ）
typedef char					int8;		//范围（-128 ~ 127）
typedef unsigned short			uint16;		//范围（0 ~ 65535）
typedef unsigned int			uint32;		//范围（0 ~ 4294967295）
typedef int						int32;		//范围（-2147483648 ~ 2147483647）
typedef long long 				int64;		//范围（-9223372036854775808 ~ 9223372036854775807）

//定义编译条件;
#define x86

//注意这里，由于32位和64位显示的长度不同，因此一定要注意分组的长度，分组长度中需要计算分组数值，因此分组大于标识时会出现异常。
#ifdef x86

    //32位;
	typedef unsigned int		uint64;		//范围（0 ~ 4294967295）
	typedef long long			rudptimer;	//范围（-9223372036854775808 ~ 9223372036854775807）

	//常量定义，这里注意：
	//对于实时性要求较高的场景，建议使用FEC非0的参数，这样可以提高实时性；
	//对于带宽要求较高的场景，建议使用FEC为0的参数，完全重传可以提高带宽利用率；
	//以上结论需要根据CPU计算能力而定。
	const uint64 FEC_GROUP_SIZE		= 20;	//分组数量
	const uint64 FEC_REDUNDANCY_SIZE= 2;	//分组中纠错数量
#else

    //64位;
	typedef unsigned long long		uint64;
	typedef long long				rudptimer;

	const uint64 FEC_GROUP_SIZE		= 60;	//分组数量
	const uint64 FEC_REDUNDANCY_SIZE= 0;	//分组中纠错数量
#endif

//定义是否使用
//#define xtrouble

//传输中每个分块的大小值（分包大小直接影响到传输的速度）
const uint32 BUFFER_SIZE		= 1024;

//协议定义;
const uint8 message_syn			= 1;	//请求连接类型
const uint8 message_ack			= 2;	//ack
const uint8 message_syn_ack		= 3;	//返回确认
const uint8 message_sack		= 4;	//批量返回
const uint8 message_sysc_time	= 5;	//同步时钟(客户端计算出同步时钟数据后发送给服务端,此消息只在服务端存在)
const uint8 message_rst			= 6;	//复位
const uint8 message_nul			= 7;	//心跳
const uint8 message_error		= 8;	//纠错
const uint8 message_data		= 9;	//数据
const uint8 message_trouble		= 10;	//排查
const uint8 message_probe		= 11;	//探测带宽

//常量定义;
const int32 INVALID_INTERVAL	= -1;
const int32 INVALID_PORT		= -1;
const int32 INVALID_HANDLE		= -1;

#if defined(_WIN32)

#else

	const int32 INVALID_SOCKET	= -1;
	const int32 SOCKET_ERROR	= -1;
#endif

const int32 INIT_SOCKET_HANDLE	= 1000;
const uint64 INIT_PACKET_NO		= 1;
const uint64 INIT_FRAMES_NO		= 0;
const uint64 INIT_SEGMENT_INDEX	= 0;

const uint32 SECOND_TIMER		= 1000;	//毫秒

//区分出一些时钟的用途;
const int32 DESTROY_TIMER		= 100;	//销毁时钟间隔
const int32 RATE_TIMER			= 100;	//限速时钟间隔
const int32 PROTOCOL_TIMER		= 5;	//协议拆分使用的时钟间隔
const int32 FRAME_TIMER			= 5;	//帧检测使用的时钟间隔
const int32 SEND_TIMER			= 10;	//发送数据时钟间隔;
const int32 CHANNEL_TIMER		= 5;	//接收通道检测使用的时钟间隔	
const int32 DECODER_TIMER		= 10;	//返回SACK协议使用的时钟间隔
const int32 FEC_ENCODE_TIMER	= 5;	//FEC编码时钟间隔
const int32 SACK_TIMER			= 5;	//
const int32 CUMULATIVE_TIMER	= 5;	//帧延迟连续性判断时使用的间隔

const int32 CHECK_BANDWIDTH_INTERVAL = 2;	//检测带宽间隔

//RSA所使用的时钟间隔
const int32 ENCRYPT_TIMER		= 2;	//加密判断时钟间隔
const int32 DECRYPT_TIMER		= 2;	//解密判断时钟间隔

const uint64 MAX_RESEND_TIMEOUT	= 3000;	//最大重发超时
const uint64 MAX_RTO			= 1500;	//最大RTO时间
const uint64 MIN_RTO			= 2;	//最小RTO时间

const int32 SYN_MAX_GO_COUNT	= 10;	//最大SYN发送次数
const int32 RST_MAX_GO_COUNT	= 5;	//最大RST发送次数
const int32 HEART_MAX_GO_COUNT	= 5;	//最大心跳发送次数
const int32 TROUBLE_MAX_GO_COUNT= 6;	//最大纠错发送次数

const uint8 SACK_MAX_COUNT		= 5;	//一次检测最大的sack次数;
const uint8 SACK_MAX_REQUEST	= 2;	//分组中SACK请求的最大次数;

const float INSERT_REQUEST_DELAY= 2;	//刚插入分组后多久可以请求sack（RTO的倍数）
const float FAST_RESEND_DELAY	= 2;	//自判断使用的快速重传时间间隔（RTO的倍数）
const float SACK_REQUEST_DELAY	= 2;	//两次sack请求之间的间隔（RTO的倍数）

const int32 CHECK_TIME_OUT		= 15 * 1000;//超时
const uint8 KILL_THREAD_TIMER	= 5;		//
const uint8 INFO_SHOW_INTERVAL	= 1;		//信息显示时间（秒）

//对于UDP来说，设置SEND_BUFFER意义不大
//对于UDP来说，加大设置RECV_BUFFER意义非凡，可以保证由于底层缓存不足造成的数据丢包现象。
const uint32 KB						= 1 * 1024;
const uint32 MB						= 1 * 1024 * 1024;

const uint32 START_BANDWIDTH		= 2 * MB;
const uint32 MAX_BANDWIDTH			= 10 * MB;

const uint32 MAX_RECV_BUFFER		= MAX_BANDWIDTH;	//这里限制接收缓存的最大值（不能设置过大，如果过大，有可能创建套接字失败）
const uint32 RUDP_BUFFER_POOL_SIZE 	= 500;
const uint8 MAX_KEY_SIZE			= 32;
const uint16 MAX_SINGLE_SEND		= 1000;

//服务启动的模式（服务端、客户端）
enum service_mode
{
	SERVER,		//服务端模式
	CLIENT		//客户端模式
};

//滑动算法（大小模式、间隔模式）
enum windows_math_mode
{
	MATH_SIZE,		//大小模式
	MATH_INTERVAL	//间隔模式
};
const windows_math_mode current_windows_mode = MATH_INTERVAL;

//RUDP链接状态
enum rudp_state
{
	CLOSE, 
	LISTEN, 
	SYN_SENT, 
	OPEN, 
	CLOSE_WAIT
};

//线程状态
enum thread_state_type
{
	tst_init, 
	tst_runing, 
	tst_stoping, 
	tst_stoped
};

//RUDP采用的时钟模式枚举
enum timer_mode
{
	Second, 		//秒级
	Millisecond, 	//毫秒级
	Microsecond		//微秒级
};
//设置当前使用采用的模式
const timer_mode current_timer_mode = Millisecond;

//速率类型
enum rate_type{SEND_TYPE, RECV_TYPE};

//包头
struct rudp_header			
{
	uint8 message_type_:4,		//协议类型	
		  group_index_:4;		//分组编号（在冗余包中代表冗余包的位置）		
	uint64 index_;				//块编号
};
const uint16 RUDP_HEADER_SIZE = sizeof(rudp_header);

//纠错信息包
const uint16 RUDP_ERROR_BUFFER_SIZE = BUFFER_SIZE - RUDP_HEADER_SIZE;
struct error_buffer
{
	rudp_header header_;
	int8 data_[RUDP_ERROR_BUFFER_SIZE];
};

//心跳包
struct null_rudp_header
{
	rudp_header header_;
	uint64 null_index_;				//心跳的序号，用来避免对于一次心跳的多次拥塞计算;
	uint64 max_index_;				//当前客户端最大的index
	uint64 max_group_index_;		//当前客户端产生的最大group_index
	
	uint64 recv_packet_count_;		//接收的数据包数量
	uint64 loss_packet_count_;		//丢失的数据包数量

	uint32 recv_packet_interval_;	//每秒钟接收的数据包数量
	uint32 loss_packet_interval_;	//每秒钟丢失的数据包数量

	uint64 useful_recv_size_;		//有效接收size;
	uint64 overall_recv_size_;		//整体接收size;

	uint64 bandwidth_;				//接收端计算的建议带宽
	uint16 real_timer_rto_;			//实时RTO
};

//探测带宽数据包
struct probe_rudp_header
{
	rudp_header header_;
};

//时钟同步包
struct sysc_time_header
{
	rudp_header header_;
	rudptimer server_base_timer_;		//服务端基础时间
	rudptimer client_base_timer_;		//客户端基础时间
	uint16 first_rto_;					//首次RTO
	int32 sys_server_time_stamp_;		//系统时间差(服务端时间 - 客户端时间)
	int32 sys_client_time_stamp_;		//系统时间差(客户端时间 - 服务端时间)
};

//排错包
struct trouble_rudp_header
{
	rudp_header header_;
	uint64 start_index_;		//出错起始序号;
	uint64 end_index_;			//出错终止序号;
};

//syn信息包(秘钥小于等于32)
struct syn_rudp_header
{
	rudp_header header_;	
	rudptimer client_timer_;	//客户端时间

	bool encrypt_;
	uint8 key_size_;
	uint8 key_[MAX_KEY_SIZE];

	uint8 iv_size_;
	uint8 iv_[MAX_KEY_SIZE];
};

//syn应答包
struct ack_syn_rudp_header
{
	rudp_header header_;
	rudptimer client_timer_;
	rudptimer server_timer_;
	uint16 server_port_;

	bool encrypt_;
	uint8 key_[MAX_KEY_SIZE];
	uint8 iv_[MAX_KEY_SIZE];
};

//应答包
struct ack_rudp_header
{
	rudp_header header_;
	uint64 complete_group_id_;
};

//批量应答包
const uint16 SACK_SIZE	 = 20;
struct group_followsegment
{
	uint64 group_id_;
	uint64 group_followsegment_;
};
struct sack_rudp_header
{
	rudp_header 	header_;
	uint64 			complete_group_id_;
	uint8 			group_count_;
	group_followsegment followsegment[SACK_SIZE];
};

struct math_frames_body
{
	uint64			frames_no_;		//帧编号
	uint16			packet_no_;		//块编号
	uint16			packet_count_;	//块总数量（这里有个隐形的限制，即一帧的数据不能超过 548 * 65535 大约 34MB左右）
	rudptimer 		frames_timer_;	//发送帧的时间
	rudptimer 		send_timer_;	//帧体的发送时间，每次发送都会发生变化;
	rudptimer		recv_timer_;	//帧体的接收时间
	uint16			send_count_:6,	//发送次数
					size_:10;		//实际帧数据量
};
const uint16 FRAMES_BUFFER_SIZE = BUFFER_SIZE - RUDP_HEADER_SIZE - sizeof(math_frames_body);
struct frames_body
{
	uint64			frames_no_;		//帧编号
	uint16			packet_no_;		//块编号
	uint16			packet_count_;	//块总数量（这里有个隐形的限制，即一帧的数据不能超过 1024 * 65535 大约 65MB左右）
	rudptimer 		frames_timer_;	//发送帧的时间
	rudptimer 		send_timer_;	//帧体的发送时间，每次发送都会发生变化;
	rudptimer		recv_timer_;	//帧体的接收时间
	uint16			send_count_:6,	//发送次数
					size_:10;		//实际帧数据量
	int8 			data_[FRAMES_BUFFER_SIZE];
};
const uint16 FRAMES_BODY_SIZE = BUFFER_SIZE - RUDP_HEADER_SIZE;
struct frames_buffer
{
	rudp_header header_;
	frames_body body_;
};

//FEC编码数据包
struct encode_buffer
{
	bool has_;
	uint8 buffer_[FRAMES_BODY_SIZE];
	uint32 size_;
};

struct fec_group_buffer
{
	uint64 		group_id_;
	uint64 		group_size_;
	uint8		sack_count_;	
	rudptimer 	last_insert_timer_;
	rudptimer 	last_sack_timer_;
	std::map<uint64, std::shared_ptr<frames_buffer>> frames_buffer_map_;
	std::map<uint8, std::shared_ptr<error_buffer>> error_buffer_map_;
};
typedef std::map<uint64, std::shared_ptr<fec_group_buffer>> fec_group_map;

struct packet_record
{
	uint64 		segment_index_;
	uint64 		frames_no_;
	uint16 		packet_count_;
	uint16 		packet_no_;
	rudptimer 	frames_tiemr_;
	rudptimer 	recv_tiemr_;
	bool 		fec_created_;
	uint16 		size_:10,
				send_count_:6;
	int8		buffer_[FRAMES_BUFFER_SIZE];
};
typedef std::map<uint64, std::shared_ptr<packet_record>> packet_map;
typedef std::map<uint64, std::shared_ptr<packet_record>> segment_packet_map;
struct frames_record
{
	uint64			frames_no_;
	uint16			packet_count_;
	rudptimer		frames_timer_;
	rudptimer		recv_timer_;
	uint64			frames_size_;
	uint64			frames_max_index_;
	packet_map		packet_map_;
};
typedef std::map<uint64, std::shared_ptr<frames_record>> frames_map;

enum frames_buffer_state
{
	NONE, 				//初始状态;
	WAITING, 			//正在等待发送;
	SENDING, 			//正在发送;
	DIRECT_ACKED,		//对方通过直接ACK方式进行了回复
	INDIRECT_SACKED		//对方通过间接SACK的方式进行了回复
};
struct confirm_buffer	//确认包结构;
{
	uint64			segment_index_;         		//数据包编号;
	uint64			frames_no_;             		//帧编号;
	rudptimer   	go_timer_;              		//发送时间;
	uint16			send_count_:6,					//发送次数
					noused_:10;						//没有使用
	frames_buffer_state current_state_; 			//当前确认帧数据的状态;
	frames_buffer buffer_;              			//帧数据;
};

struct linker_record
{
	rudptimer last_recv_timer_;
	rudptimer last_send_timer_;
	rudptimer last_heart_timer_;
	time_t free_time_;
	time_t close_time_;

	uint16 recv_rto_;
	int32 linker_handle_;

	bool encrypted_;
	uint8 key_[MAX_KEY_SIZE];
	uint8 iv_[MAX_KEY_SIZE];
};

typedef std::function<void(const int log_type, const char *context)> ON_ADD_LOG;

#pragma pack(pop)



