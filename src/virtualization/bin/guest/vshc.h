// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_VSHC_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_VSHC_H_

#include <google/protobuf/message_lite.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/types.h>

#include <optional>

namespace vsh_util {

constexpr int kVshPort = 9001;

// Maximum amount of data that can be sent in a single DataMessage. This is
// picked based on the max message size with generous room for protobuf
// overhead.
constexpr size_t kMaxDataSize = 4000;

// Maximum size allowed for a single protobuf message.
constexpr size_t kMaxMessageSize = 4096;

// Reserved keyword for connecting to the VM shell instead of a container.
// All lxd containers must also be valid hostnames, so any string that is
// not a valid hostname will work here without colliding with lxd's naming.
constexpr char kVmShell[] = "/vm_shell";

// Sends a protobuf MessageLite to the given socket.
bool SendMessage(zx::unowned_socket socket,
                 const google::protobuf::MessageLite& message);

// Receives a protobuf MessageLite from the given socket.
bool RecvMessage(zx::unowned_socket socket,
                 google::protobuf::MessageLite* message);

}  // namespace vsh_util

std::pair<int, int> init_tty(void);

void reset_tty(void);

void handle_vsh(std::optional<uint32_t> env_id, std::optional<uint32_t> cid,
                std::optional<uint32_t> port, async::Loop* loop,
                sys::ComponentContext* context);

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_VSHC_H_
