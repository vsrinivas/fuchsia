// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/cmdline/args_parser.h>
#include <lib/fs-pty/service.h>
#include <lib/service/llcpp/service.h>
#include <lib/svc/outgoing.h>

#include <fbl/string_printf.h>

#include "console.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

struct Options {
  std::vector<std::string> allowed_log_tags;
  std::vector<std::string> denied_log_tags;
};

zx_status_t ParseArgs(int argc, const char** argv, Options* opts) {
  cmdline::ArgsParser<Options> parser;
  parser.AddSwitch("allow-log-tag", 'a',
                   "Add a tag to the allow list. Log entries with matching tags will be output to "
                   "the console. If no tags are specified, all log entries will be printed.",
                   &Options::allowed_log_tags);
  parser.AddSwitch("deny-log-tag", 'd',
                   "Add a tag to the deny list. Log entries with matching tags will be prevented "
                   "from being output to the console. This takes precedence over the allow list.",
                   &Options::denied_log_tags);
  std::vector<std::string> params;
  auto status = parser.Parse(argc, argv, opts, &params);
  if (status.has_error()) {
    printf("console: ArgsParser::Parse() = %s\n", status.error_message().data());
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx::resource GetRootResource() {
  auto client_end = service::Connect<fuchsia_boot::RootResource>();
  if (client_end.is_error()) {
    printf("console: Could not connect to RootResource service: %s\n", client_end.status_string());
    return {};
  }

  auto client = fidl::BindSyncClient(std::move(client_end.value()));
  auto result = client.Get();
  if (result.status() != ZX_OK) {
    printf("console: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return {};
  }
  return std::move(result.Unwrap()->resource);
}

zx_status_t ConnectListener(fidl::ClientEnd<fuchsia_logger::LogListenerSafe> listener,
                            std::vector<std::string> allowed_log_tags) {
  auto client_end = service::Connect<fuchsia_logger::Log>();
  if (client_end.is_error()) {
    printf("console: fdio_service_connect() = %s\n", client_end.status_string());
    return client_end.status_value();
  }

  auto log = fidl::BindSyncClient(std::move(client_end.value()));
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
  auto result = log.ListenSafe(
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

  Options opts;
  status = ParseArgs(argc, argv, &opts);
  if (status != ZX_OK) {
    return status;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Provide a RxSource that grabs the data from the kernel serial connection
  Console::RxSource rx_source = [root_resource = GetRootResource()](uint8_t* byte) {
    size_t length = 0;
    zx_status_t status =
        zx_debug_read(root_resource.get(), reinterpret_cast<char*>(byte), sizeof(*byte), &length);
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
