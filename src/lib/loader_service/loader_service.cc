// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/loader_service/loader_service.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace loader {

namespace fio = fuchsia_io;

LoaderServiceBase::~LoaderServiceBase() = default;

const std::string& LoaderServiceBase::log_prefix() {
  if (log_prefix_.empty()) {
    log_prefix_ = fxl::StringPrintf("ldsvc (%s): ", name_.data());
  }
  return log_prefix_;
}

zx::result<fidl::ClientEnd<fuchsia_ldsvc::Loader>> LoaderServiceBase::Connect() {
  auto endpoints = fidl::CreateEndpoints<fuchsia_ldsvc::Loader>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto [client, server] = *std::move(endpoints);
  Bind(std::move(server));
  return zx::ok(std::move(client));
}

void LoaderServiceBase::Bind(fidl::ServerEnd<fuchsia_ldsvc::Loader> channel) {
  // Each connection gets a strong (shared_ptr) reference to the server, which keeps the overall
  // service alive as long as there is one open connection even if the original reference is
  // dropped.
  auto conn = std::make_unique<LoaderConnection>(shared_from_this());
  // This returns a ServerBindingRef, but we don't need it since we don't currently need a way
  // to unbind connections from the server side. Dropping it does not automatically unbind.
  fidl::BindServer(
      dispatcher_, std::move(channel), std::move(conn),
      [](LoaderConnection* connection, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_ldsvc::Loader> server_end) {
        if (info.is_peer_closed() || info.is_user_initiated() || info.is_dispatcher_shutdown()) {
          // Client broke away or server shutting down.
          return;
        }
        FX_LOGS(ERROR) << "Loader connection error: " << info;
      });
}

void LoaderConnection::Done(DoneCompleter::Sync& completer) { completer.Close(ZX_OK); }

void LoaderConnection::LoadObject(LoadObjectRequestView request,
                                  LoadObjectCompleter::Sync& completer) {
  std::string name(request->object_name.data(), request->object_name.size());

  auto reply = [this, &name, &completer](zx::result<zx::vmo> status) {
    // Generally we wouldn't want to log in a library, but these logs have proven to be useful in
    // past, and the new loader name in the prefix will make them moreso.
    if (status.status_value() == ZX_ERR_NOT_FOUND) {
      FX_LOGS(WARNING) << log_prefix() << "could not find '" << name << "'";
    }

    completer.Reply(status.status_value(), std::move(status).value_or(zx::vmo()));
    fidl::Status result = completer.result_of_reply();
    if (!result.ok()) {
      FX_LOGS(WARNING) << log_prefix() << "failed to reply to LoadObject(" << name
                       << "): " << result.error();
    }
  };

  if (config_.subdir.empty()) {
    reply(server_->LoadObjectImpl(name));
    return;
  }

  // If subdir is non-empty, the loader should search this subdirectory for the object first. If
  // exclusive is also true, only subdir should be searched.
  std::string prefixed_name = files::JoinPath(config_.subdir, name);
  auto status = server_->LoadObjectImpl(prefixed_name);
  if (status.is_error() && !config_.exclusive) {
    reply(server_->LoadObjectImpl(name));
    return;
  }
  reply(std::move(status));
}

void LoaderConnection::Config(ConfigRequestView request, ConfigCompleter::Sync& completer) {
  // fidl::StringView is not null-terminated so must pass size to std::string constructor.
  std::string config_str(request->config.data(), request->config.size());

  auto reply = [this, &config_str, &completer](zx_status_t status) {
    completer.Reply(status);
    fidl::Status result = completer.result_of_reply();
    if (!result.ok()) {
      FX_LOGS(WARNING) << log_prefix() << "failed to reply to Config(" << config_str
                       << "): " << result.error();
    }
  };

  // Config strings must not contain path separators.
  if (config_str.find('/') != std::string::npos) {
    reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  // The config string is a single subdirectory name to be searched for objects first, optionally
  // followed by a '!' character, which indicates that only the subdirectory should be searched.
  bool exclusive = false;
  if (!config_str.empty() && config_str.back() == '!') {
    exclusive = true;
    config_str.pop_back();

    // Make sure config wasn't "!" (though just "" is ok to reset config)
    if (config_str.empty()) {
      reply(ZX_ERR_INVALID_ARGS);
      return;
    }
  }
  config_ = LoadConfig{.subdir = config_str, .exclusive = exclusive};
  reply(ZX_OK);
}

void LoaderConnection::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  server_->Bind(std::move(request->loader));
  completer.Reply(ZX_OK);
}

// static
std::shared_ptr<LoaderService> LoaderService::Create(async_dispatcher_t* dispatcher,
                                                     fbl::unique_fd lib_dir, std::string name) {
  // Can't use make_shared because constructor is protected
  return std::shared_ptr<LoaderService>(
      new LoaderService(dispatcher, std::move(lib_dir), std::move(name)));
}

zx::result<zx::vmo> LoaderService::LoadObjectImpl(std::string path) {
  const fio::wire::OpenFlags kFlags = fio::wire::OpenFlags::kNotDirectory |
                                      fio::wire::OpenFlags::kRightReadable |
                                      fio::wire::OpenFlags::kRightExecutable;

  fbl::unique_fd fd;
  zx_status_t status = fdio_open_fd_at(dir_.get(), path.data(), static_cast<uint32_t>(kFlags),
                                       fd.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  zx::vmo vmo;
  status = fdio_get_vmo_exec(fd.get(), vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(vmo));
}

}  // namespace loader
