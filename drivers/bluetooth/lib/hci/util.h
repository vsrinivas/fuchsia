// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/hci/hci.h"

namespace bluetooth {
namespace hci {

// Returns a DynamicByteBuffer encoding an HCI command with the given |opcode| and payload.
common::DynamicByteBuffer BuildHCICommand(hci::OpCode opcode, void* params = nullptr,
                                          size_t params_size = 0u);

// Returns a user-friendly string representation of |version|.
std::string HCIVersionToString(hci::HCIVersion version);

}  // namespace hci
}  // namespace adapter
