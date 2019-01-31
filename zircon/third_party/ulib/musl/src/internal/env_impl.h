#pragma once

#include "libc.h"

extern char** __env_map ATTR_LIBC_VISIBILITY;
int __putenv(char* s, int a) ATTR_LIBC_VISIBILITY;
