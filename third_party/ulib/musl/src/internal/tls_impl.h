#pragma once

#include <pthread.h>
#include <magenta/types.h>

pthread_t __copy_tls(unsigned char*) __attribute__((visibility("hidden")));
void __reset_tls(void) __attribute__((visibility("hidden")));
