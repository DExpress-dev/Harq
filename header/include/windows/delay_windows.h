#pragma once

#ifndef USTD_WINDOWS_DELAY_H_
#define USTD_WINDOWS_DELAY_H_

#include "../rudp_def.h"

int sleep_delay_windows(const int &delay_timer, const timer_mode &cur_timer_mode);
int get_cpu_core_num();


#endif  // USTD_WINDOWS_DELAY_H_
