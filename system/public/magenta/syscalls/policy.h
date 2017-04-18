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


// Conditons handled by job policy.
#define MX_BAD_HANDLE_POLICY                1u
#define MX_WRONG_OBJECT_POLICY              2u
#define MX_CREATION_POLICY                  3u
#define MX_VMAR_MAP_POLICY                  4u

// General policy to either generate an alarm or terminate
// the process. Ir can be ORed to any below.

#define MX_POL_GENERATE_ALARM               (1u << 14)
#define MX_POL_TERMINATE                    (1u << 15)

// Policies for MX_BAD_HANDLE_POLICY:
#define MX_POL_BAD_HANDLE_ALLOW             1u

// Policies for MX_WRONG_OBJECT_POLICY:
#define MX_POL_WRONG_HANDLE_ALLOW           1u

// Policies for MX_CREATION_POLICY:
#define MX_POL_ALL_DENY                     1u
#define MX_POL_ALL_ALLOW                    2u
#define MX_POL_VM_OBJECT_DENY               3u
#define MX_POL_VM_OBJECT_ALLOW              4u
#define MX_POL_CHANNEL_DENY                 5u
#define MX_POL_CHANNEL_ALLOW                6u
#define MX_POL_EVENT_DENY                   7u
#define MX_POL_EVENT_ALLOW                  7u
#define MX_POL_EVPAIR_DENY                  8u
#define MX_POL_EVPAIR_ALLOW                 9u
#define MX_POL_PORT_DENY                   10u
#define MX_POL_PORT_ALLOW                  11u
#define MX_POL_SOCKET_DENY                 12u
#define MX_POL_SOCKET_ALLOW                13u
#define MX_POL_FIFO_DENY                   14u
#define MX_POL_FIFO_ALLOW                  15u

// Policies for MX_VMAR_MAP_POLICY
#define MX_POL_WX_MAP_DENY                  1u
#define MX_POL_WX_MAP_ALLOW                 2u


__END_CDECLS
