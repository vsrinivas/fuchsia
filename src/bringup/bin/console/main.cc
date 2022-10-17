// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/fs-pty/service.h>
#include <lib/svc/outgoing.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/string_printf.h>

#include "src/bringup/bin/console/args.h"
#include "src/bringup/bin/console/console.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

zx::resource GetDebugResource() {
  auto client_end = component::Connect<fuchsia_kernel::DebugResource>();
  if (client_end.is_error()) {
    printf("console: Could not connect to DebugResource service: %s\n", client_end.status_string());
    return {};
  }

  fidl::WireSyncClient client{std::move(client_end.value())};
  auto result = client->Get();
  if (result.status() != ZX_OK) {
    printf("console: Could not retrieve DebugResource: %s\n",
           zx_status_get_string(result.status()));
    return {};
  }
  return std::move(result->resource);
}

zx_status_t ConnectListener(fidl::ClientEnd<fuchsia_logger::LogListenerSafe> listener,
                            std::vector<std::string> allowed_log_tags) {
  auto client_end = component::Connect<fuchsia_logger::Log>();
  if (client_end.is_error()) {
    printf("console: fdio_service_connect() = %s\n", client_end.status_string());
    return client_end.status_value();
  }

  fidl::WireSyncClient log{std::move(client_end.value())};
  std::vector<fidl::StringView> tags;
  for (auto& tag : allowed_log_tags) {
    tags.emplace_back(fidl::StringView::FromExternal(tag));
  }
  fuchsia_logger::wire::LogFilterOptions options{
      .filter_by_pid = false,
      .filter_by_tid = false,
      .min_severity = fuchsia_logger::wire::LogLevelFilter::kTrace,
      .tags = fidl::VectorView<fidl::StringView>::FromExternal(tags),
  };
  auto result = log->ListenSafe(
      std::move(listener),
      fidl::ObjectView<fuchsia_logger::wire::LogFilterOptions>::FromExternal(&options));
  if (!result.ok()) {
    printf("console: fuchsia.logger.Log/ListenSafe() = %s\n", result.FormatDescription().c_str());
    return result.status();
  }
  return ZX_OK;
}

}  // namespace

int main(int argc, const char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    return status;
  }

  zx::result boot_args = component::Connect<fuchsia_boot::Arguments>();
  if (boot_args.is_error()) {
    return boot_args.status_value();
  }
  const fidl::WireSyncClient boot_args_client{*std::move(boot_args)};

  Options opts;
  status = ParseArgs(console_config::Config::TakeFromStartupHandle(), boot_args_client, &opts);
  if (status != ZX_OK) {
    return status;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Provide a RxSource that grabs the data from the kernel serial connection
  Console::RxSource rx_source = [debug_resource = GetDebugResource()](uint8_t* byte) {
    size_t length = 0;
    zx_status_t status =
        zx_debug_read(debug_resource.get(), reinterpret_cast<char*>(byte), sizeof(*byte), &length);
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // Suppress the error print in this case.  No console on this machine.
      return status;
    } else if (status != ZX_OK) {
      printf("console: error %s, length %zu from zx_debug_read syscall, exiting.\n",
             zx_status_get_string(status), length);
      return status;
    }
    if (length != 1) {
      return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
  };
  Console::TxSink tx_sink = [](const uint8_t* buffer, size_t length) {
    return zx_debug_write(reinterpret_cast<const char*>(buffer), length);
  };
  fbl::RefPtr<Console> console;
  status = Console::Create(std::move(rx_source), std::move(tx_sink),
                           std::move(opts.denied_log_tags), &console);
  if (status != ZX_OK) {
    printf("console: Console::Create() = %s\n", zx_status_get_string(status));
    return status;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_logger::LogListenerSafe>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  status = ConnectListener(std::move(endpoints->client), std::move(opts.allowed_log_tags));
  if (status != ZX_OK) {
    return status;
  }

  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), console.get());

  svc::Outgoing outgoing(loop.dispatcher());
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return status;
  }

  using Vnode =
      fs_pty::TtyService<fs_pty::SimpleConsoleOps<fbl::RefPtr<Console>>, fbl::RefPtr<Console>>;
  outgoing.svc_dir()->AddEntry(fidl::DiscoverableProtocolName<fuchsia_hardware_pty::Device>,
                               fbl::AdoptRef(new Vnode(std::move(console))));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return status;
}
