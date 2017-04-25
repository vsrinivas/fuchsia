// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

class FifoDispatcher;
struct GuestState;
struct VmxState;

status_t vmexit_handler(const VmxState& vmx_state, GuestState* guest_state,
                        GuestPhysicalAddressSpace* gpas, FifoDispatcher* serial_fifo);
