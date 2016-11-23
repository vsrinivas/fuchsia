#pragma once

#include <magenta/compiler.h>

void _warn_unsupported(void* caller, const char* fmt, ...) __PRINTFLIKE(2, 3);
#define warn_unsupported(x...) _warn_unsupported(__GET_CALLER(), x)
