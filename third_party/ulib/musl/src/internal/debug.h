#pragma once

#include <magenta/compiler.h>

void _panic(void* caller, const char* fmt, ...) __PRINTFLIKE(2, 3) __NO_RETURN;
#define panic(x...) _panic(__GET_CALLER(), x)
