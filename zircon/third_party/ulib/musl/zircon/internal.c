#include "zircon_impl.h"

#undef _zx_process_self
#undef _zx_vmar_root_self
#undef _zx_job_default

#include <zircon/process.h>

zx_handle_t __zircon_process_self;
zx_handle_t __zircon_vmar_root_self;
zx_handle_t __zircon_job_default;

zx_handle_t _zx_process_self(void) {
    return __zircon_process_self;
}
__typeof(zx_process_self) zx_process_self
    __attribute__((weak, alias("_zx_process_self")));

zx_handle_t _zx_vmar_root_self(void) {
    return __zircon_vmar_root_self;
}
__typeof(zx_vmar_root_self) zx_vmar_root_self
    __attribute__((weak, alias("_zx_vmar_root_self")));

zx_handle_t _zx_job_default(void) {
    return __zircon_job_default;
}
__typeof(zx_job_default) zx_job_default
    __attribute__((weak, alias("_zx_job_default")));
