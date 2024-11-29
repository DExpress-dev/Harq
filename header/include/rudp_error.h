#pragma once

#ifndef RUDP_ERROR_H_
#define RUDP_ERROR_H_

#include "rudp_def.h"

const int32 INVALID_SEND		= -1;
const int32 INVALID_CLASS		= -2;
const int32 FLOW_OVERLOAD		= -3;
const int32 ENCRYPT_ERROR		= -4;
const int32 CONFIRM_OVERLOAD	= -5;
const int32 SIZE_OVERLOAD		= -6;

#endif  // RUDP_ERROR_H_