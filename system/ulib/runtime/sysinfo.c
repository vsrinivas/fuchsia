#include <runtime/sysinfo.h>
#include <magenta/syscalls.h>


int mxr_get_nprocs_conf(void) {
    return _magenta_num_cpus();
}

int mxr_get_nprocs(void) {
    return _magenta_num_idle_cpus();
}
