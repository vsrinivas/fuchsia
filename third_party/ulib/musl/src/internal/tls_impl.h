#pragma once

#include <pthread.h>
#include <magenta/types.h>

pthread_t __copy_tls(unsigned char*);
void __reset_tls(void);
