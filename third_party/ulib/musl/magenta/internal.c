#include "magenta_impl.h"

#undef _mx_process_self
#undef _mx_vmar_root_self
#undef _mx_job_default

#include <magenta/process.h>

// TODO: add ATTR_LIBC_VISIBILITY to all 3 exports as soon as Rust no longer depends on these
mx_handle_t __magenta_process_self;
mx_handle_t __magenta_vmar_root_self;
mx_handle_t __magenta_job_default;

mx_handle_t _mx_process_self(void) {
    return __magenta_process_self;
}
__typeof(mx_process_self) mx_process_self
    __attribute__((weak, alias("_mx_process_self")));

mx_handle_t _mx_vmar_root_self(void) {
    return __magenta_vmar_root_self;
}
__typeof(mx_vmar_root_self) mx_vmar_root_self
    __attribute__((weak, alias("_mx_vmar_root_self")));

mx_handle_t _mx_job_default(void) {
    return __magenta_job_default;
}
__typeof(mx_job_default) mx_job_default
    __attribute__((weak, alias("_mx_job_default")));
