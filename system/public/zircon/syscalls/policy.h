// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Policy is applied for the conditions that are not
// specified by the parent job policy.
#define ZX_JOB_POL_RELATIVE                 0u
// Policy is either applied as-is or the syscall fails.
#define ZX_JOB_POL_ABSOLUTE                 1u

// Basic policy topic.
#define ZX_JOB_POL_BASIC                    0u

// Input structure to use with ZX_JOB_POL_BASIC.
typedef struct zx_policy_basic {
    uint32_t condition;
    uint32_t policy;
} zx_policy_basic_t;

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
#ifdef _KERNEL
#define ZX_POL_MAX                          13u
#endif

// Policy actions.
// ZX_POL_ACTION_ALLOW and ZX_POL_ACTION_DENY can be ORed with ZX_POL_ACTION_EXCEPTION.
// ZX_POL_ACTION_KILL implies ZX_POL_ACTION_DENY.
#define ZX_POL_ACTION_ALLOW                 0u
#define ZX_POL_ACTION_DENY                  1u
#define ZX_POL_ACTION_EXCEPTION             2u
#define ZX_POL_ACTION_KILL                  5u

__END_CDECLS
