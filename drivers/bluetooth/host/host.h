// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>

#include <ddk/protocol/bt-hci.h>
#include <zircon/types.h>

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"

#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace bthost {

// Host is the top-level object of this driver and it is responsible for
// managing the host subsystem stack. It owns the core gap::Adapter object, and
// the FIDL server implementations. A Host's core responsibility is to relay
// messages from the devhost environment to the stack.
//
// THREAD SAFETY: This class IS NOT thread-safe.
class Host final : public fxl::RefCountedThreadSafe<Host> {
 public:
  // Initializes the system and reports the status in |success|.
  using InitCallback = std::function<void(bool success)>;
  bool Initialize(InitCallback callback);

  // Shuts down all systems.
  void ShutDown();

 private:
  FRIEND_MAKE_REF_COUNTED(Host);
  explicit Host(const bt_hci_protocol_t& hci_proto);

  // Represents the host subsystem stack for this Host's Bluetooth controller.
  std::unique_ptr<::btlib::gap::Adapter> adapter_;

  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Host);
};

}  // namespace bthost
