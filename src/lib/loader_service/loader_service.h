// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LOADER_SERVICE_LOADER_SERVICE_H_
#define SRC_LIB_LOADER_SERVICE_LOADER_SERVICE_H_

#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>
#include <fbl/unique_fd.h>

namespace loader {

// Pure virtual base class for a fuchsia.ldsvc.Loader FIDL server. See concrete LoaderService
// implementation below, which should fit most use cases, or subclass to customize the behavior of
// LoadObjectImpl for your use case.
//
// Connections to the loader service stay alive as long as the client keeps the connection open (and
// other obvious things, like the async dispatcher is not shutdown and the hosting process is
// alive), even if the creator of the service drops any copies of this object.
class LoaderServiceBase : public std::enable_shared_from_this<LoaderServiceBase> {
 public:
  virtual ~LoaderServiceBase();

  // Bind and Connect create a new connection to the loader service. Connect is identical to
  // Bind but creates the channel for the caller.
  zx::status<> Bind(zx::channel channel);
  zx::status<zx::channel> Connect();

 protected:
  LoaderServiceBase(async_dispatcher_t* dispatcher, std::string name)
      : dispatcher_(dispatcher), name_(std::move(name)) {}

 private:
  // Pure virtual methods to be implemented by subclasses.
  //
  // LoadObjectImpl shall return a VMO with the contents of the specified path. The interpretation
  // of the path is defined by the concrete subclass; it may simply open the path from a given
  // directory, or may do something more complex.
  //
  // The returned VMO shall have the ZX_RIGHT_EXECUTE right, i.e. the file must be opened executable
  // or this call shall fail with ZX_ERR_ACCESS_DENIED if the file cannot be opened executable.
  //
  // The path parameter may contain one or more path components. The base class handles applying
  // loader config as requested by the client. For example, a `Config("asan!")` call followed by a
  // `LoadObject("libfoo.so")` call will result in the base class calling this with
  // "asan/libfoo.so".
  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) = 0;

  const std::string& log_prefix();

  async_dispatcher_t* dispatcher_;
  // This name is only used when logging to provide useful context for which loader service is
  // logging, since processes which host loaders sometimes host many of them.
  std::string name_;
  std::string log_prefix_;

  friend class LoaderConnection;

  DISALLOW_COPY_ASSIGN_AND_MOVE(LoaderServiceBase);
};

// Represents a single client connection to the loader service, including per-connection state. Used
// internally by LoaderServiceBase and not intended to be used directly.
//
// Connections have a strong reference to the server object (through std::shared_ptr), which keeps
// the loader service alive as long as any open client connections exist.
class LoaderConnection : public llcpp::fuchsia::ldsvc::Loader::Interface {
 public:
  LoaderConnection(std::shared_ptr<LoaderServiceBase> server) : server_(server) {}

  // llcpp::fuchsia::ldsvc::Loader::Interface implementation
  virtual void Done(DoneCompleter::Sync completer) override;
  virtual void LoadObject(fidl::StringView object_name,
                          LoadObjectCompleter::Sync completer) override;
  virtual void Config(fidl::StringView config, ConfigCompleter::Sync completer) override;
  virtual void Clone(zx::channel loader, CloneCompleter::Sync completer) override;

 private:
  const std::string& log_prefix() { return server_->log_prefix(); }

  // Wraps loader configuration set through the fuchsia.ldsvc.Loader.Config FIDL method.
  struct LoadConfig {
    std::string subdir;
    bool exclusive;
  };

  std::shared_ptr<LoaderServiceBase> server_;
  LoadConfig config_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(LoaderConnection);
};

// Concrete implementation of a fuchsia.ldsvc.Loader FIDL server that serves libraries from a single
// directory, e.g. from a component's specific "/pkg/lib/" directory.
class LoaderService : public LoaderServiceBase {
 public:
  // This takes ownership of the 'lib_dir` fd and will close it automatically once all connections
  // to the loader service are closed and copies of this object are destroyed. `name` is used to
  // provide context when logging.
  static std::shared_ptr<LoaderService> Create(async_dispatcher_t* dispatcher,
                                               fbl::unique_fd lib_dir, std::string name);

 protected:
  LoaderService(async_dispatcher_t* dispatcher, fbl::unique_fd lib_dir, std::string name)
      : LoaderServiceBase(dispatcher, std::move(name)), dir_(std::move(lib_dir)) {}
  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) override;

 private:
  fbl::unique_fd dir_;
};

}  // namespace loader

#endif  // SRC_LIB_LOADER_SERVICE_LOADER_SERVICE_H_
