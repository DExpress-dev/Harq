
#include <Winsock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include "delay_windows.h"

int sleep_delay_windows(const int &delay_timer, const timer_mode &cur_timer_mode)
{
	fd_set fds;
	FD_ZERO(&fds);
	SOCKET fd = socket(AF_INET, SOCK_DGRAM, 0);
	FD_SET(fd, &fds);

	struct timeval tv;
	if (Second == cur_timer_mode)
	{
		tv.tv_sec = delay_timer;
		tv.tv_usec = 0;
	}
	else if (Millisecond == cur_timer_mode)
	{
		tv.tv_sec = delay_timer / 1000;
		tv.tv_usec = (delay_timer % 1000) * 1000;
	}
	else if (Microsecond == cur_timer_mode)
	{
		tv.tv_sec = delay_timer / (1000 * 1000);
		tv.tv_usec = delay_timer % (1000 * 1000);
	}

	int ret = select(0, NULL, NULL, &fds, &tv);
	DWORD err = GetLastError();
	if (err != 0)
		return err;

	closesocket(fd);
	return 0;
}

int get_cpu_core_num()
{
	SYSTEM_INFO si;
    GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}