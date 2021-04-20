// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/svc/outgoing.h>
#include <lib/syslog/global.h>

#include <iostream>

#include "tun_ctl.h"

int main(int argc, const char** argv) {
  fx_logger_config_t config = {
      // TODO(brunodalbo) load severity from argc (we use this as injected-services, which doesn't
      // seem to be respecting arguments)
      .min_severity = FX_LOG_INFO,          .console_fd = -1, .log_sink_channel = ZX_HANDLE_INVALID,
      .log_sink_socket = ZX_HANDLE_INVALID, .tags = nullptr,  .num_tags = 0,
  };
  fx_log_reconfigure(&config);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  network::tun::TunCtl ctl(dispatcher);
  svc::Outgoing outgoing(dispatcher);

  zx_status_t status;
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    std::cerr << "error: ServeFromStartupInfo: " << zx_status_get_string(status) << std::endl;
    return -1;
  }

  status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_net_tun::Control>,
      fbl::MakeRefCounted<fs::Service>([&ctl](fidl::ServerEnd<fuchsia_net_tun::Control> request) {
        ctl.Connect(std::move(request));
        return ZX_OK;
      }));
  if (status != ZX_OK) {
    std::cerr << "error: AddEntry: " << zx_status_get_string(status) << std::endl;
    return -1;
  }

  loop.Run();
  return 0;
}
