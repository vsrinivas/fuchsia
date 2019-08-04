#ifndef SYSROOT_SYS_KLOG_H_
#define SYSROOT_SYS_KLOG_H_

#ifdef __cplusplus
extern "C" {
#endif

int klogctl(int, char*, int);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_KLOG_H_
