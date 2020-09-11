// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/logger/llcpp/fidl.h>
#include <lib/cmdline/args_parser.h>
#include <lib/fdio/directory.h>
#include <lib/fs-pty/service.h>
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
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return {};
  }
  auto path = fbl::StringPrintf("/svc/%s", llcpp::fuchsia::boot::RootResource::Name);
  status = fdio_service_connect(path.data(), remote.release());
  if (status != ZX_OK) {
    printf("console: Could not connect to RootResource service: %s\n",
           zx_status_get_string(status));
    return {};
  }

  llcpp::fuchsia::boot::RootResource::SyncClient client(std::move(local));
  auto result = client.Get();
  if (result.status() != ZX_OK) {
    printf("console: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return {};
  }
  return std::move(result.Unwrap()->resource);
}

zx_status_t ConnectListener(zx::channel listener, std::vector<std::string> allowed_log_tags) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }

  auto path = fbl::StringPrintf("/svc/%s", llcpp::fuchsia::logger::Log::Name);
  status = fdio_service_connect(path.data(), server.release());
  if (status != ZX_OK) {
    printf("console: fdio_service_connect() = %s\n", zx_status_get_string(status));
    return status;
  }
  llcpp::fuchsia::logger::Log::SyncClient log(std::move(client));
  std::vector<fidl::StringView> tags;
  for (auto& tag : allowed_log_tags) {
    tags.emplace_back(fidl::unowned_str(tag));
  }
  llcpp::fuchsia::logger::LogFilterOptions options{.tags = fidl::unowned_vec(tags)};
  auto result = log.ListenSafe(std::move(listener), fidl::unowned_ptr(&options));
  if (!result.ok()) {
    printf("console: fuchsia.logger.Log/ListenSafe() = %s\n", result.error());
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

  zx::channel client, server;
  status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  status = ConnectListener(std::move(client), std::move(opts.allowed_log_tags));
  if (status != ZX_OK) {
    return status;
  }
  auto result = fidl::BindServer(loop.dispatcher(), std::move(server), console.get());
  if (result.is_error()) {
    printf("console: fidl::BindServer() = %s\n", zx_status_get_string(result.error()));
    return result.error();
  }

  svc::Outgoing outgoing(loop.dispatcher());
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return status;
  }

  using Vnode =
      fs_pty::TtyService<fs_pty::SimpleConsoleOps<fbl::RefPtr<Console>>, fbl::RefPtr<Console>>;
  outgoing.svc_dir()->AddEntry("fuchsia.hardware.pty.Device",
                               fbl::AdoptRef(new Vnode(std::move(console))));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return status;
}
