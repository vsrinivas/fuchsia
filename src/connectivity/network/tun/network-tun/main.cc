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
      -2,                 // min_severity
      -1,                 // console_fd
      ZX_HANDLE_INVALID,  // log_service_channel
      nullptr,            // tags
      0,                  // num_tags
  };
  fx_log_init_with_config(&config);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  network::tun::TunCtl ctl(loop.dispatcher());
  auto ctx = sys::ComponentContext::Create();
  ctx->outgoing()->AddPublicService(ctl.GetHandler());
  loop.Run();
  return 0;
}
