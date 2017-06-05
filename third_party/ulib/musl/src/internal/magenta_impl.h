#pragma once

#include "libc.h"
#include <magenta/process.h>
#include <magenta/types.h>

// TODO: add ATTR_LIBC_VISIBILITY to all 3 exports as soon as Rust no longer depends on these
extern mx_handle_t __magenta_process_self;
extern mx_handle_t __magenta_vmar_root_self;
extern mx_handle_t __magenta_job_default;

#define _mx_process_self() (__magenta_process_self + 0)
#define _mx_vmar_root_self() (__magenta_vmar_root_self + 0)
#define _mx_job_default() (__magenta_job_default + 0)
