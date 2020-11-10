// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>

#include "tun_ctl.h"

int main(int argc, const char** argv) {
  fx_logger_config_t config = {
      // TODO(brunodalbo) load severity from argc (we use this as injected-services, which doesn't
      // seem to be respecting arguments)
      .min_severity = FX_LOG_INFO,
      .console_fd = -1,
      .log_sink_channel = ZX_HANDLE_INVALID,
      .log_sink_socket = ZX_HANDLE_INVALID,
      .tags = nullptr,
      .num_tags = 0,
  };
  fx_log_reconfigure(&config);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  network::tun::TunCtl ctl(loop.dispatcher());
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  ctx->outgoing()->AddPublicService(ctl.GetHandler());
  loop.Run();
  return 0;
}
