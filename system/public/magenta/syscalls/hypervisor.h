// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Opcodes for mx_hypervisor_op().

#pragma once

#define MX_HYPERVISOR_OP_GUEST_CREATE       1u
#define MX_HYPERVISOR_OP_GUEST_ENTER        2u

#if __x86_64__
#define MX_HYPERVISOR_OP_GUEST_SET_CR3      3u
#endif // __x86_64__

#define MX_HYPERVISOR_OP_GUEST_SET_ENTRY    4u
