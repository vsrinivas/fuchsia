#pragma once

#include "libc.h"
#include <time.h>

extern const char __gmt[] ATTR_LIBC_VISIBILITY;

char* __asctime(const struct tm* restrict, char* restrict)
    ATTR_LIBC_VISIBILITY;

int __days_in_month(int, int) ATTR_LIBC_VISIBILITY;
int __month_to_secs(int, int) ATTR_LIBC_VISIBILITY;
long long __year_to_secs(long long, int*) ATTR_LIBC_VISIBILITY;
long long __tm_to_secs(const struct tm*) ATTR_LIBC_VISIBILITY;
int __secs_to_tm(long long, struct tm*) ATTR_LIBC_VISIBILITY;
void __secs_to_zone(long long, int, int*, long*, long*, const char**)
    ATTR_LIBC_VISIBILITY;
const unsigned char* __map_file(const char*, size_t*) ATTR_LIBC_VISIBILITY;
