
#if defined(_WIN32)

	#include <Winsock2.h>
	#include <WS2tcpip.h>
	#include <Mswsock.h>
	#include "windows\delay_windows.h"
	
	#define sleep_delay_system sleep_delay_windows
	#define get_cpu_core get_cpu_core_num
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/ip.h>
	#include <unistd.h>
	#include <arpa/inet.h>
	#include "linux/delay_linux.h"

	#define sleep_delay_system sleep_delay_linux
	#define get_cpu_core get_cpu_core_num
#endif

#include <fcntl.h>
#include <stdarg.h>
#include <random>

#include "rudp_def.h"
#include "rudp_public.h"


namespace ustd
{

	rudp_public::rudp_public(void)
	{
	}

	rudp_public::~rudp_public(void)
	{
	}

	void rudp_public::get_groupid_postion(uint64 segment_index, uint64 &gourp_id, uint64 &postion)
	{
		if(0 == segment_index)
		{
			gourp_id = 0;
			postion = 0;
		}
		else
		{
			gourp_id = ((segment_index - 1) / (FEC_GROUP_SIZE)) + 1;
			postion = (segment_index - 1) - (gourp_id - 1) * FEC_GROUP_SIZE;
		}
	}

	void rudp_public::get_group_min_max_index(uint64 group_id, uint64 &min_index, uint64 &max_index)
	{
		if(group_id <= 0)
		{
			min_index = 1;
			max_index = FEC_GROUP_SIZE - 1;	
		}
		else
		{
			min_index = (group_id - 1) * (FEC_GROUP_SIZE) + 1;
			max_index = min_index + FEC_GROUP_SIZE - 1;	
		}
	}

	uint64 rudp_public::get_group_id(const uint64 &index)
	{
		if(0 == index)
			return 0;

		return ((index - 1) / (FEC_GROUP_SIZE)) + 1;
	}

	rudp_header *rudp_public::get_header(char *data, const int &size)
	{
		if(size >= static_cast<int>(sizeof(rudp_header)))
			return (rudp_header *)(data);
		else
			return nullptr;
	}

	rudp_header rudp_public::get_header2(char *data, const int &size)
	{
		rudp_header header;
		if(size >= static_cast<int>(sizeof(header)))
		{
			memset(&header, 0, sizeof(header));
			memcpy(&header, data, size);
		}
		return header;
	}

	ack_rudp_header *rudp_public::get_ack_header(char *data, const int &size)
	{
		if(size >= static_cast<int>(sizeof(ack_rudp_header)))
			return (ack_rudp_header *)(data);
		else
			return nullptr;
	}

	ack_rudp_header rudp_public::get_ack_header2(char *data, const int &size)
	{
		ack_rudp_header tmp_header;
		if(size >= static_cast<int>(sizeof(ack_rudp_header)))
		{
			memset(&tmp_header, 0, sizeof(tmp_header));
			memcpy(&tmp_header, data, size);
		}
		return tmp_header;
	}

	sack_rudp_header *rudp_public::get_sack_header(char *data, const int &size)
	{
		if(size >= static_cast<int>(sizeof(sack_rudp_header)))
			return (sack_rudp_header *)(data);
		else
			return nullptr;
	}

	sack_rudp_header rudp_public::get_sack_header2(char *data, const int &size)
	{
		sack_rudp_header tmp_header;
		if(size >= static_cast<int>(sizeof(sack_rudp_header)))
		{
			memset(&tmp_header, 0, sizeof(tmp_header));
			memcpy(&tmp_header, data, size);
		}
		return tmp_header;
	}

	syn_rudp_header *rudp_public::get_syn_header(char *data, const int &size)
	{
		if(size >= static_cast<int>(sizeof(syn_rudp_header)))
			return (syn_rudp_header *)(data);
		else
			return nullptr;
	}

	syn_rudp_header rudp_public::get_syn_header2(char *data, const int &size)
	{
		syn_rudp_header tmp_header;
		if(size >= static_cast<int>(sizeof(syn_rudp_header)))
		{
			memset(&tmp_header, 0, sizeof(tmp_header));
			memcpy(&tmp_header, data, size);
		}
		return tmp_header;
	}


	null_rudp_header *rudp_public::get_null_header(char *data, const int &size)
	{
		if(size >= static_cast<int>(sizeof(null_rudp_header)))
			return (null_rudp_header *)(data);
		else
			return nullptr;
	}

	null_rudp_header rudp_public::get_null_header2(char *data, const int &size)
	{
		null_rudp_header tmp_header;
		if(size >= static_cast<int>(sizeof(null_rudp_header)))
		{
			memset(&tmp_header, 0, sizeof(tmp_header));
			memcpy(&tmp_header, data, size);
		}
		return tmp_header;
	}

	sysc_time_header *rudp_public::get_sysc_time_header(char *data, const int &size)
	{
		if(size >= static_cast<int>(sizeof(sysc_time_header)))
			return (sysc_time_header *)(data);
		else
			return nullptr;
	}

	sysc_time_header rudp_public::get_sysc_time_header2(char *data, const int &size)
	{
		sysc_time_header tmp_header;
		if(size >= static_cast<int>(sizeof(sysc_time_header)))
		{
			memset(&tmp_header, 0, sizeof(tmp_header));
			memcpy(&tmp_header, data, size);
		}
		return tmp_header;
	}

	bool rudp_public::is_abnormal_header(char* data, int size)
	{
		rudp_header *header = get_header(data, size);
		if(nullptr == header)
			return true;

		if(message_syn == header->message_type_ ||
			message_ack == header->message_type_ ||
			message_sack == header->message_type_ ||
			message_syn_ack == header->message_type_ ||
			message_rst == header->message_type_ ||
			message_nul == header->message_type_ ||
			message_error == header->message_type_ ||
			message_data == header->message_type_ ||
			message_trouble == header->message_type_)
		{
			return false;
		}
		return true;
	}

	bool rudp_public::is_abnormal_header2(char* data, int size)
	{
		rudp_header header = get_header2(data, size);
		if(message_syn == header.message_type_ ||
			message_ack == header.message_type_ ||
			message_sack == header.message_type_ ||
			message_syn_ack == header.message_type_ ||
			message_rst == header.message_type_ ||
			message_nul == header.message_type_ ||
			message_error == header.message_type_ ||
			message_data == header.message_type_ ||
			message_trouble == header.message_type_)
		{
			return false;
		}
		return true;
	}

	bool rudp_public::is_abnormal_header2(char* data, int size, uint8 &message_type)
	{
		rudp_header header;
		if(size >= static_cast<int>(sizeof(header)))
		{
			memset(&header, 0, sizeof(header));
			memcpy(&header, data, size);
			
			if(message_syn == header.message_type_ ||
			message_ack == header.message_type_ ||
			message_sack == header.message_type_ ||
			message_syn_ack == header.message_type_ ||
			message_rst == header.message_type_ ||
			message_nul == header.message_type_ ||
			message_error == header.message_type_ ||
			message_data == header.message_type_ ||
			message_trouble == header.message_type_)
			{
				message_type = header.message_type_;
				return false;
			}		
		}
		return true;
	}

	bool rudp_public::is_abnormal_header(char* data, int size, uint8 &message_type)
	{
		rudp_header *header = get_header(data, size);
		if(nullptr == header)
			return true;

		if(message_syn == header->message_type_ ||
			message_ack == header->message_type_ ||
			message_sack == header->message_type_ ||
			message_syn_ack == header->message_type_ ||
			message_rst == header->message_type_ ||
			message_nul == header->message_type_ ||
			message_error == header->message_type_ ||
			message_data == header->message_type_ ||
			message_trouble == header->message_type_)
		{
			message_type = header->message_type_;
			return false;
		}
		return true;
	}

	bool rudp_public::is_syn_protocol(char* data, int size)
	{
		rudp_header *header = get_header(data, size);
		if(nullptr == header)
			return false;

		if(message_syn == header->message_type_)
			return true;
		else
			return false;
	}

	bool rudp_public::is_syn_protocol2(char* data, int size)
	{
		rudp_header header = get_header2(data, size);
		if(message_syn == header.message_type_)
			return true;
		else
			return false;
	}

	ack_syn_rudp_header *rudp_public::get_ack_syn_header(char *data, int size)
	{
		if(size >= static_cast<int>(sizeof(ack_syn_rudp_header)))
			return (ack_syn_rudp_header*)(data);
		else
			return nullptr;
	}

	ack_syn_rudp_header rudp_public::get_ack_syn_header2(char *data, int size)
	{
		ack_syn_rudp_header tmp_header;
		if(size >= static_cast<int>(sizeof(ack_syn_rudp_header)))
		{
			memset(&tmp_header, 0, sizeof(tmp_header));
			memcpy(&tmp_header, data, size);
		}
		return tmp_header;
	}

	bool rudp_public::socket_bind_udp_port(const short int &port, const char* bind_ip)
	{
		harq_fd fd = socket(AF_INET, SOCK_DGRAM , 0);
		if(INVALID_SOCKET == fd)
			return false;

		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = PF_INET;
		sa.sin_port = htons(port);

		if(strcmp("0.0.0.0", bind_ip) == 0)
			sa.sin_addr.s_addr = htonl(INADDR_ANY);
		else
			sa.sin_addr.s_addr = inet_addr(bind_ip);	

		if(bind(fd, (const sockaddr*)&sa, sizeof(sa)) != 0)
		{
			harq_close(fd);
			return false;
		}
		else
		{
			harq_close(fd);
			return true;
		}
	}

	int64 rudp_public::get_address(struct sockaddr_in addr_ptr)
	{
		long long tmp_addr = ((int64_t)addr_ptr.sin_addr.s_addr << 32) | addr_ptr.sin_port;
		return tmp_addr;
	}

	int64_t rudp_public::address_to_int64(sockaddr_in *addr_ptr)
	{
		return ((int64_t)addr_ptr->sin_addr.s_addr << 32) | addr_ptr->sin_port;
	}

	int64 rudp_public::get_address(const std::string &ip, const int &port)
	{
		struct sockaddr_in addr_ptr;
		memset(&addr_ptr, 0, sizeof(addr_ptr));
		addr_ptr.sin_family = PF_INET;
		addr_ptr.sin_addr.s_addr = inet_addr(ip.c_str());
		addr_ptr.sin_port = htons(port);

		long long tmp_addr = ((int64_t)addr_ptr.sin_addr.s_addr << 32) | addr_ptr.sin_port;
		return tmp_addr;
	}

	std::string rudp_public::get_remote_ip(struct sockaddr_in addr_ptr)
	{
		char* tmp_remote_ip = inet_ntoa(addr_ptr.sin_addr);
		std::string remote_ip(tmp_remote_ip);
		return remote_ip;
	}

	int rudp_public::digits(int num)
	{
		int dig = 0;
		do
		{
			++dig;
			num = num /10;
		}while(num > 0);
		return dig;
	}

	std::string rudp_public::int_2_string(int num)
	{
		int dig = digits(num);

		char *sz_num = new char[dig + 1];
		memset(sz_num, 0, dig + 1);

		harq_sprintf(sz_num, "%d", num);
		
		std::string str(sz_num);
		delete[] sz_num;

		return str;
	}

	int rudp_public::get_remote_port(struct sockaddr_in addr_ptr)
	{
		return htons(addr_ptr.sin_port);
	}

	#if defined(_WIN32)

	#else
		bool rudp_public::set_socket_enable_blocking(const harq_fd &fd)
		{
			int flags = fcntl(fd, F_GETFL, 0);
			if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
				return false;

			return true;
		}

		bool rudp_public::set_socket_multicastloop(const harq_fd &fd)
		{
			int loop = 1;
			int err = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
			if (err < 0)
				return false;
			else
				return true;
		}

		bool rudp_public::set_socket_addmembership(const harq_fd &fd, const std::string &group_ip)
		{
			struct ip_mreq mreq;
			mreq.imr_multiaddr.s_addr = inet_addr(group_ip.c_str());
			mreq.imr_interface.s_addr = htonl(INADDR_ANY);
			int err = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
			if (err < 0)
				return false;
			else
				return true;
		}

		bool rudp_public::set_socket_multicastif(const harq_fd &fd, const std::string &local_ip)
		{
			struct ip_mreq mreq;

			if ("0.0.0.0" == local_ip)
				mreq.imr_interface.s_addr = htonl(INADDR_ANY);
			else
				mreq.imr_interface.s_addr = inet_addr(local_ip.c_str());

			int err = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&mreq.imr_interface, sizeof(struct in_addr));
			if (err < 0)
				return false;
			else
				return true;
		}
	#endif	

	uint16 rudp_public::get_sum(char *data, int size)
	{
		uint16 cksum = 0;
		while (size > 1)
		{
			cksum += *data++;
			size -= sizeof(uint16);
		}
		if (size)
		{
			cksum += *(char*)data;
		}
		cksum = (cksum >> 16) + (cksum & 0xffff);
		cksum += (cksum >> 16);
		return ~cksum;
	}

	bool rudp_public::set_socket_send_buffer(const harq_fd &fd, const int &size)
	{
		socklen_t optlen;
		optlen = sizeof(size);

		//设置发送缓存;
		int err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&size, optlen);
		if(err < 0)
			return false;
		else
			return true;
	}

	bool rudp_public::set_socket_recv_buffer(const harq_fd &fd, const int &size)
	{
		socklen_t optlen;
		optlen = sizeof(size);

		//设置接收缓存;
		int err = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&size, optlen);
		if(err < 0)
			return false;
		else
			return true;
	}

	bool rudp_public::set_socket_ttl(const harq_fd &fd)
	{
		int ttl = 255;
		int err = setsockopt(fd, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl));
		if(err < 0)
			return false;
		else
			return true;
	}

	bool rudp_public::set_socket_tos(const harq_fd &fd)
	{
		//	IP_TOS：
		//	设置源于该套接字的每个IP包的Type-Of-Service（TOS 服务类型）字段。它被用来在网络上区分包的优先级>。TOS是单字节的字段。定义了一些的标准TOS标识：
		//	IPTOS_LOWDELAY：用来为交互式通信最小化延迟时间
		//	IPTOS_THROUGHPUT：用来优化吞吐量
		//	IPTOS_RELIABILITY：用来作可靠性优化，
		//	IPTOS_MINCOST应该被用作“填充数据”，对于这些数据，低速传输是无关紧要的。
		//	至多只能声明这些 TOS 值中的一个，其它的都是无效的，应当被清除。缺省时,Linux首先发送IPTOS_LOWDELAY数据报，但是确切的做法要看配置的排队规则而定。
		//	一些高优先级的层次可能会要求一个有效的用户标识0或者CAP_NET_ADMIN能力。优先级也可以以于协议无关的方式通过( SOL_SOCKET, SO_PRIORITY )套接字选项来设置。
		//	IPTOS_RELIABILITY;//IPTOS_THROUGHPUT|IPTOS_LOWDELAY|IPTOS_RELIABILITY|IPTOS_MINCOST;

		#if defined(_WIN32)

			char tos = IPTOS_LOWDELAY;
		#else

			int tos = IPTOS_LOWDELAY;
		#endif

		int err = setsockopt(fd, IPPROTO_IP, IP_TOS, (const char*)&tos, sizeof(tos));
		if(err < 0)
			return false;
		else
			return true;
	}

	bool rudp_public::set_socket_reuseaddr(const harq_fd &fd)
	{
		int reuse = 1;
		int err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
		if(err < 0)
			return false;
		else
			return true;
	}

	#if defined(_WIN32)
		
		bool rudp_public::set_socket_connreset(const harq_fd &fd)
		{
			int bNewBehavior = 0;
			DWORD dwBytesReturned;
			WSAIoctl(fd, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);
			return true;
		}
	#endif

	//bool rudp_public::set_socket_dontfragment(const harq_fd &fd)
	//{
	//	//该值指定是否允许将 Internet 协议 (IP) 数据报分段
	//	int flag = 0;
	//	int err = setsockopt(fd, IPPROTO_IP, IP_DONTFRAGMENT, (char *)&flag, sizeof(flag));
	//	if (err < 0)
	//		return false;
	//	else
	//		return true;
	//}

	//bool rudp_public::set_socket_multicast_loop(const harq_fd &fd)
	//{
	//	//网络参数控制IP层是否回送所送的数据
	//	int flag = 0;
	//	int err = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&flag, sizeof(flag));
	//	if (err < 0)
	//		return false;
	//	else
	//		return true;
	//}

	int rudp_public::sleep_delay(const int &delay_timer, const timer_mode &cur_timer_mode)
	{
		sleep_delay_system(delay_timer, cur_timer_mode);
		return 0;
	}

	std::string rudp_public::get_time_string()
	{
		time_t t = time(nullptr);
		tm* local = localtime(&t);

		char buf[128]= {0};
		strftime(buf, 64, "%Y-%m-%d %H:%M:%S", local);
		std::string strTime(buf, sizeof(buf));
		return strTime;
	}

	//输出日志;
	void rudp_public::log(std::string Format,...)
	{
		const int array_length = 1024 * 10;
		char log_text[array_length];
		memset(log_text, 0x00, array_length);
		va_list vaList;
		va_start(vaList, Format);
		int result = vsnprintf(log_text, array_length, Format.c_str(), vaList);
		va_end(vaList);
		if (result <= 0)
			return;

		if (result > array_length)
			return;

		std::string time_string = get_time_string();
		printf("[%s]->%s\n", time_string.c_str(), log_text);
	}

	int rudp_public::get_cpu_cnum()
	{
		//对于安卓，此函数不可以使用;
		//return get_cpu_core();
		return 2;
	}

	std::string rudp_public::get_random_string(const int length)
	{
		char *result_char = new char[length];
		std::string random_string = "0123456789ABCDEF";
		std::random_device rd;
		srand(rd());
		for(int i = 0; i < length; i++)
		{
			int index = rand() % (random_string.length());
			result_char[i] = random_string[index];
		}
		std::string result_string(result_char, length);
		delete[] result_char;
		return result_string;
	}

	void rudp_public::hexstr_to_byte(const char* source, unsigned char* dest, int sourceLen)
	{
		short i;
		unsigned char highByte, lowByte;
		for (i = 0; i < sourceLen; i += 2)
		{
			highByte = toupper(source[i]);
			lowByte  = toupper(source[i + 1]);

			if (highByte > 0x39)
				highByte -= 0x37;
			else
				highByte -= 0x30;

			if (lowByte > 0x39)
				lowByte -= 0x37;
			else
				lowByte -= 0x30;

			dest[i / 2] = (highByte << 4) | lowByte;
		}
		return ;
	}

	rudptimer rudp_public::abs_sub(rudptimer first, rudptimer second)
	{
		if(first >= second)
			return first - second;
		else
			return second - first;	
	}

	std::string rudp_public::get_speed(const uint64 &flow)
	{
		uint64 mb_round = 0;
		uint64 kb_round = 0;
		uint64 spare = flow;

		//得到MB
		if(spare >= MB)
		{
			mb_round = (spare / MB);
			spare = (spare % MB);
		}

		//得到KB
		if(spare >= KB)
		{
			kb_round = (spare / KB);
			spare = (spare % KB);
		}

		//组合;
		char speed[1024] = {0};
		if(mb_round > 0)
		{
			#ifdef x86

				harq_sprintf(speed, "%dMB%dKB%d/s", mb_round, kb_round, spare);
			#else

				harq_sprintf(speed, "%lldMB%lldKB%lld/s", mb_round, kb_round, spare);
			#endif
		
		}
		else if(kb_round > 0)
		{
			#ifdef x86

				harq_sprintf(speed, "%dKB%d/s", kb_round, spare);			
			#else

				harq_sprintf(speed, "%lldKB%lld/s", kb_round, spare);
			#endif
			
		}
		else
		{
			#ifdef x86

				harq_sprintf(speed, "%d/s", spare);
			#else

				harq_sprintf(speed, "%lld/s", spare);
			#endif
			
		}
		
		std::string speed_string(speed);
		return speed_string;
	}
}

