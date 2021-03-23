// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>

#include "dockyard_proxy.h"
#include "info_resource.h"
#include "os.h"

namespace harvester::fidl {

void HarvesterImpl::ConnectGrpc(zx::socket socket, ConnectGrpcCallback cb) {
  int fd;

  if (auto status = fdio_fd_create(socket.release(), &fd); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not create fd from zx::socket";
    cb(fuchsia::systemmonitor::Harvester_ConnectGrpc_Result::WithErr(
        int32_t(status)));
  } else {
    auto proxy = std::make_unique<harvester::DockyardProxyGrpc>(
        grpc::CreateInsecureChannelFromFd("fidl", fd), clock_);
    cb(fuchsia::systemmonitor::Harvester_ConnectGrpc_Result::WithResponse(
        fuchsia::systemmonitor::Harvester_ConnectGrpc_Response()));
    async::PostTask(
        slow_dispatcher_, [this, proxy = std::move(proxy)]() mutable {
          if (auto status = proxy->Init();
              status != harvester::DockyardProxyStatus::OK) {
            FX_LOGS(ERROR) << harvester::DockyardErrorString("Init", status);
          } else {
            zx_handle_t info_resource;
            if (auto status = harvester::GetInfoResource(&info_resource);
                status != ZX_OK) {
              FX_PLOGS(ERROR, status) << "Could not get info resource";
            } else {
              std::unique_ptr<harvester::OS> os =
                  std::make_unique<harvester::OSImpl>();

              auto harvester = std::make_unique<harvester::Harvester>(
                  info_resource, std::move(proxy), std::move(os));
              harvester->GatherDeviceProperties();
              harvester->GatherFastData(fast_dispatcher_);
              harvester->GatherSlowData(async_get_default_dispatcher());
              harvester->GatherLogs();

              harvesters_.emplace_back(std::move(harvester));
            }
          }
        });
  }
}

}  // namespace harvester::fidl
