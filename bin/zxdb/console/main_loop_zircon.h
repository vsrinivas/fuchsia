// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fdio/private.h>
#include <lib/zx/port.h>

#include "garnet/bin/zxdb/console/main_loop.h"

namespace zxdb {

class PlatformMainLoop : public MainLoop {
 public:
  PlatformMainLoop();
  ~PlatformMainLoop() override;

 protected:
  // MainLoop implementation.
  void PlatformRun() override;
  void PlatformStartWatchingConnection(size_t connection_id,
                                       AgentConnection* connection) override;
  void PlatformStopWatchingConnection(size_t connection_id,
                                      AgentConnection* connection) override;

 private:
  zx::port port_;

  // The underlying handle to stdin. This is owned by fdio.
  fdio_t* stdin_fdio_ = nullptr;
};

}  // namespace zxdb
