#ifndef SYSROOT_SYS_STATFS_H_
#define SYSROOT_SYS_STATFS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/statvfs.h>

typedef struct __fsid_t {
  int __val[2];
} fsid_t;

#include <bits/statfs.h>

int statfs(const char*, struct statfs*);
int fstatfs(int, struct statfs*);

#ifdef __cplusplus
}
#endif

#endif  // SYSROOT_SYS_STATFS_H_
