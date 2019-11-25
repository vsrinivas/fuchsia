// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_POLICY_H_
#define SYSROOT_ZIRCON_SYSCALLS_POLICY_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Policy is applied for the conditions that are not
// specified by the parent job policy.
#define ZX_JOB_POL_RELATIVE                     0u
// Policy is either applied as-is or the syscall fails.
#define ZX_JOB_POL_ABSOLUTE                     1u

// Basic policy topic.
#define ZX_JOB_POL_BASIC_V1                     0u
#define ZX_JOB_POL_BASIC_V2            0x01000000u

// Timer slack policy topic.
#define ZX_JOB_POL_TIMER_SLACK                  1u

// Input structure to use with ZX_JOB_POL_BASIC_V1.
typedef struct zx_policy_basic_v1 {
    uint32_t condition;
    uint32_t policy;
} zx_policy_basic_v1_t;

// Input structure to use with ZX_JOB_POL_BASIC_V2.
typedef struct zx_policy_basic_v2 {
    uint32_t condition;
    uint32_t action;
    uint32_t flags;
} zx_policy_basic_v2_t;

#define ZX_JOB_POL_BASIC        ZX_JOB_POL_BASIC_V1
typedef struct zx_policy_basic_v1 zx_policy_basic;
typedef struct zx_policy_basic_v1 zx_policy_basic_t;

// Conditions handled by job policy.
#define ZX_POL_BAD_HANDLE                    0u
#define ZX_POL_WRONG_OBJECT                  1u
#define ZX_POL_VMAR_WX                       2u
#define ZX_POL_NEW_ANY                       3u
#define ZX_POL_NEW_VMO                       4u
#define ZX_POL_NEW_CHANNEL                   5u
#define ZX_POL_NEW_EVENT                     6u
#define ZX_POL_NEW_EVENTPAIR                 7u
#define ZX_POL_NEW_PORT                      8u
#define ZX_POL_NEW_SOCKET                    9u
#define ZX_POL_NEW_FIFO                     10u
#define ZX_POL_NEW_TIMER                    11u
#define ZX_POL_NEW_PROCESS                  12u
#define ZX_POL_NEW_PROFILE                  13u
#define ZX_POL_AMBIENT_MARK_VMO_EXEC        14u
#ifdef _KERNEL
#define ZX_POL_MAX                          15u
#endif

// Policy actions.
#define ZX_POL_ACTION_ALLOW                 0u
#define ZX_POL_ACTION_DENY                  1u
#define ZX_POL_ACTION_ALLOW_EXCEPTION       2u
#define ZX_POL_ACTION_DENY_EXCEPTION        3u
#define ZX_POL_ACTION_KILL                  4u
#ifdef _KERNEL
#define ZX_POL_ACTION_MAX                   5u
#endif

// Policy override.
#define ZX_POL_OVERRIDE_ALLOW               0u
#define ZX_POL_OVERRIDE_DENY                1u


// Input structure to use with ZX_JOB_POL_TIMER_SLACK.
typedef struct zx_policy_timer_slack {
    zx_duration_t min_slack;
    uint32_t default_mode;
    uint8_t padding1[4];
} zx_policy_timer_slack_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_POLICY_H_
