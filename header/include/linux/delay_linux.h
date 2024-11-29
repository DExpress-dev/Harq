#pragma once

#ifndef USTD_LINUX_DEALY_H_
#define USTD_LINUX_DEALY_H_

#include "../rudp_def.h"

int sleep_delay_linux(const int &delay_timer, const timer_mode &cur_timer_mode);
int get_cpu_core_num();

#endif  // USTD_LINUX_DEALY_H_
