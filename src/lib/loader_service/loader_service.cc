// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/loader_service/loader_service.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace loader {

namespace fio = ::llcpp::fuchsia::io;

LoaderServiceBase::~LoaderServiceBase() {}

const std::string& LoaderServiceBase::log_prefix() {
  if (log_prefix_.empty()) {
    log_prefix_ = fxl::StringPrintf("ldsvc (%s): ", name_.data());
  }
  return log_prefix_;
}

zx::status<zx::channel> LoaderServiceBase::Connect() {
  zx::channel c1, c2;
  auto status = zx::make_status(zx::channel::create(0, &c1, &c2));
  if (status.is_error()) {
    return status.take_error();
  }
  status = Bind(std::move(c1));
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(c2));
}

zx::status<> LoaderServiceBase::Bind(zx::channel channel) {
  // Each connection gets a strong (shared_ptr) reference to the server, which keeps the overall
  // service alive as long as there is one open connection even if the original reference is
  // dropped.
  auto conn = std::make_unique<LoaderConnection>(shared_from_this());
  // This returns a ServerBindingRef on success, but we don't need it since we don't currently need
  // a way to unbind connections from the server side. Dropping it does not automatically unbind.
  auto status = fidl::BindServer(dispatcher_, std::move(channel), std::move(conn));
  if (status.is_error()) {
    return zx::error(status.take_error());
  }
  return zx::ok();
}

void LoaderConnection::Done(DoneCompleter::Sync completer) { completer.Close(ZX_OK); }

void LoaderConnection::LoadObject(fidl::StringView object_name,
                                  LoadObjectCompleter::Sync completer) {
  std::string name(object_name.data(), object_name.size());

  auto reply = [this, &name, &completer](zx::status<zx::vmo> status) {
    // Generally we wouldn't want to log in a library, but these logs have proven to be useful in
    // past, and the new loader name in the prefix will make them moreso.
    if (status.status_value() == ZX_ERR_NOT_FOUND) {
      FX_LOGS(WARNING) << log_prefix() << "could not find '" << name << "'";
    }

    auto reply_status = zx::make_status(
        completer.Reply(status.status_value(), std::move(status).value_or(zx::vmo())));
    if (reply_status.is_error()) {
      FX_LOGS(WARNING) << log_prefix() << "failed to reply to LoadObject(" << name
                       << "): " << reply_status.status_string();
    }
  };

  // The fuchsia.ldsvc.Loader protocol doesn't require this to allow for future flexibility, but
  // filesystem-based implementations like this disallow object names that contain path separators.
  // This avoids security bugs since we rely on string path manipulation and because our handling of
  // ".." path components is currently somewhat inconsistent, depending on where it gets handled.
  if (name.find('/') != std::string::npos) {
    reply(zx::error(ZX_ERR_INVALID_ARGS));
    return;
  }

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

void LoaderConnection::Config(fidl::StringView config, ConfigCompleter::Sync completer) {
  // fidl::StringView is not null-terminated so must pass size to std::string constructor.
  std::string config_str(config.data(), config.size());

  auto reply = [this, &config_str, &completer](zx_status_t status) {
    auto reply_status = zx::make_status(completer.Reply(status));
    if (reply_status.is_error()) {
      FX_LOGS(WARNING) << log_prefix() << "failed to reply to Config(" << config_str
                       << "): " << reply_status.status_string();
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

void LoaderConnection::Clone(zx::channel loader, CloneCompleter::Sync completer) {
  completer.Reply(server_->Bind(std::move(loader)).status_value());
}

// static
std::shared_ptr<LoaderService> LoaderService::Create(async_dispatcher_t* dispatcher,
                                                     fbl::unique_fd lib_dir, std::string name) {
  // Can't use make_shared because constructor is protected
  return std::shared_ptr<LoaderService>(
      new LoaderService(dispatcher, std::move(lib_dir), std::move(name)));
}

zx::status<zx::vmo> LoaderService::LoadObjectImpl(std::string path) {
  const uint32_t kFlags =
      fio::OPEN_FLAG_NOT_DIRECTORY | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;

  fbl::unique_fd fd;
  zx_status_t status = fdio_open_fd_at(dir_.get(), path.data(), kFlags, fd.reset_and_get_address());
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
