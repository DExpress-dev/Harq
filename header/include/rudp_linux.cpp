

#if defined(_WIN32)

	#include <Winsock2.h>
	#include <WS2tcpip.h>
#else

	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/ip.h>
	#include <unistd.h>
	#include <arpa/inet.h>
#endif

#include <fcntl.h>
#include <stdarg.h>
#include <random>

#include "rudp_def.h"
#include "rudp_public.h"
#include "rudp_linux.h"

namespace ustd
{

	rudp_linux::rudp_linux(void)
	{
	}

	rudp_linux::~rudp_linux(void)
	{
	}

	#if defined(_WIN32)

	
	#else
		bool rudp_linux::set_socket_reuseport(const harq_fd &fd)
		{
			int reuse = 1;
			int err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char *)&reuse, sizeof(reuse));
			if (err < 0)
				return false;
			else
				return true;

			// return true;
		}
	#endif	

	bool rudp_linux::socket_bind_port(const int &port, const char* bind_ip)
	{
		harq_fd fd = socket(AF_INET, SOCK_DGRAM, 0);
		if(INVALID_SOCKET == fd)
			return false;

		sockaddr_in sa;
		sa.sin_family = PF_INET;
		if(strcmp("0.0.0.0", bind_ip) == 0)
			sa.sin_addr.s_addr = INADDR_ANY;
		else
			sa.sin_addr.s_addr = inet_addr(bind_ip);


		#if defined(_WIN32)


		#else
			//需要设置端口复用;
			if (!set_socket_reuseport(fd))
			{
				harq_close(fd);
				return false;
			}
		#endif

		if(bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		{
			harq_close(fd);
			return false;
		}
		harq_close(fd);
		return true;
	}

	bool rudp_linux::set_fd_attribute(const harq_fd &fd, const int &send_buffer, const int &recv_buffer)
	{
		//设置套接字的接收缓存;
		if(!ustd::rudp_public::set_socket_recv_buffer(fd, recv_buffer))
			return false;

		//设置地址复用;
		if (!ustd::rudp_public::set_socket_reuseaddr(fd))
			return false;

		//设置套接字的TOS;
		if(!ustd::rudp_public::set_socket_tos(fd))
			return false;

		//在这里加入对于windows的判断;

		#if defined(_WIN32)
			if (!ustd::rudp_public::set_socket_connreset(fd))
				return false;
		#endif




		////设置IP_DONTFRAGMENT;
		//if (!ustd::rudp_public::set_socket_dontfragment(fd))
		//	return false;

		////设置IP_MULTICAST_LOOP;
		//if (!ustd::rudp_public::set_socket_multicast_loop(fd))
		//	return false;

		#if defined(_WIN32)


		#else
			//设置端口复用
			if (!set_socket_reuseport(fd))
				return false;
		#endif

		////设置套接字的ttl；
		//if (!ustd::rudp_public::set_socket_ttl(fd))
		//	return false;

		return true;
	}

	int rudp_linux::rand_local_port(const std::string &bind_ip, const int &begin_port, const int &end_port)
	{
		int tmp_port = 0;
		while(1)
		{
			//设置非确定性种子;
			std::random_device rd;
			srand(rd());
			tmp_port = begin_port + rand() % (end_port - begin_port);
			if(socket_bind_port(tmp_port, bind_ip.c_str()))
				break;
		}
		return tmp_port;
	}

}

