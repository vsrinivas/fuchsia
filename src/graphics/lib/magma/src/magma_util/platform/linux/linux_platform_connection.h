// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_CONNECTION_H
#define LINUX_PLATFORM_CONNECTION_H

#include "magma_util/macros.h"
#include "platform_connection.h"

namespace magma {

class LinuxPlatformConnection : public PlatformConnection {
 public:
  LinuxPlatformConnection(std::unique_ptr<Delegate> delegate, msd_client_id_t client_id)
      : PlatformConnection(nullptr /* shutdown event */, client_id),
        delegate_(std::move(delegate)) {}

  // Channels not supported, using in-process instead.
  uint32_t GetClientEndpoint() override { return -1; }
  uint32_t GetClientNotificationEndpoint() override { return -1; }

  bool HandleRequest() override { return DRETF(false, "HandleRequest not supported"); }

  Delegate* delegate() { return delegate_.get(); }

 private:
  std::unique_ptr<Delegate> delegate_;
};

}  // namespace magma

#endif  // LINUX_PLATFORM_CONNECTION_H
