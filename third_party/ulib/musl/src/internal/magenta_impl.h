#pragma once

#include "libc.h"
#include <magenta/process.h>
#include <magenta/types.h>

extern mx_handle_t __magenta_process_self ATTR_LIBC_VISIBILITY;
extern mx_handle_t __magenta_vmar_root_self ATTR_LIBC_VISIBILITY;
extern mx_handle_t __magenta_job_default ATTR_LIBC_VISIBILITY;

#define _mx_process_self() (__magenta_process_self + 0)
#define _mx_vmar_root_self() (__magenta_vmar_root_self + 0)
#define _mx_job_default() (__magenta_job_default + 0)
