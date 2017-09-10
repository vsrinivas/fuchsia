// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Policy is applied for the conditions that are not
// specified by the parent job policy.
#define MX_JOB_POL_RELATIVE                 0u
// Policy is either applied as-is or the syscall fails.
#define MX_JOB_POL_ABSOLUTE                 1u

// Basic policy topic.
#define MX_JOB_POL_BASIC                    0u

// Input structure to use with MX_JOB_POL_BASIC.
typedef struct mx_policy_basic {
    uint32_t condition;
    uint32_t policy;
} mx_policy_basic_t;

// Conditions handled by job policy.
#define MX_POL_BAD_HANDLE                    0u
#define MX_POL_WRONG_OBJECT                  1u
#define MX_POL_VMAR_WX                       2u
#define MX_POL_NEW_ANY                       3u
#define MX_POL_NEW_VMO                       4u
#define MX_POL_NEW_CHANNEL                   5u
#define MX_POL_NEW_EVENT                     6u
#define MX_POL_NEW_EVPAIR                    7u
#define MX_POL_NEW_PORT                      8u
#define MX_POL_NEW_SOCKET                    9u
#define MX_POL_NEW_FIFO                     10u
#define MX_POL_NEW_TIMER                    11u
#define MX_POL_MAX                          12u

// Policy actions.
// MX_POL_ACTION_ALLOW and MX_POL_ACTION_DENY can be ORed with MX_POL_ACTION_EXCEPTION.
// MX_POL_ACTION_KILL implies MX_POL_ACTION_DENY.
#define MX_POL_ACTION_ALLOW                 0u
#define MX_POL_ACTION_DENY                  1u
#define MX_POL_ACTION_EXCEPTION             2u
#define MX_POL_ACTION_KILL                  5u

__END_CDECLS
