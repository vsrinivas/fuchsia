// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_SERVICE_PROXY_H_
#define APPLICATION_SRC_MANAGER_SERVICE_PROXY_H_

#include <mx/channel.h>
#include <mxtl/ref_ptr.h>
#include <svcfs/svcfs.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "application/services/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/vfs/vfs_handler.h"

namespace app {

// ServiceProviderBridge is a bridge between a service provider and a service
// directory.
//
// The bridge takes a service provider to use as a backend and exposes both the
// service provider interface and the directory interface, which will make it
// easier to migrate clients to the directory interface.
class ServiceProviderBridge : public svcfs::ServiceProvider,
                              public app::ServiceProvider {
 public:
  ServiceProviderBridge();
  ~ServiceProviderBridge() override;

  void set_backend(app::ServiceProviderPtr backend) {
    backend_ = std::move(backend);
  }

  void AddBinding(fidl::InterfaceRequest<app::ServiceProvider> request);
  bool ServeDirectory(mx::channel channel);

  mx::channel OpenAsDirectory();
  int OpenAsFileDescriptor();

 private:
  // Overridden from |svcfs::ServiceProvider|:
  void Connect(const char* name, size_t len, mx::channel channel) override;

  // Overridden from |app::ServiceProvider|:
  void ConnectToService(const fidl::String& service_name,
                        mx::channel channel) override;

  mtl::VFSDispatcher dispatcher_;
  fidl::BindingSet<app::ServiceProvider> bindings_;
  mxtl::RefPtr<svcfs::VnodeProviderDir> directory_;
  app::ServiceProviderPtr backend_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderBridge);
};

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_SERVICE_PROXY_H_
