#pragma once

#include "libc.h"
#include <zircon/process.h>
#include <zircon/types.h>

// TODO: add ATTR_LIBC_VISIBILITY to all 3 exports as soon as Rust no longer depends on these
extern zx_handle_t __zircon_process_self;
extern zx_handle_t __zircon_vmar_root_self;
extern zx_handle_t __zircon_job_default;

#define _zx_process_self() (__zircon_process_self + 0)
#define _zx_vmar_root_self() (__zircon_vmar_root_self + 0)
#define _zx_job_default() (__zircon_job_default + 0)
