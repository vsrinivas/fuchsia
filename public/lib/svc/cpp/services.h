// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_SVC_SERVICES_H_
#define APPLICATION_LIB_SVC_SERVICES_H_

#include <mx/channel.h>

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace app {

// Services is a convenience frontend to a directory that contains services.
//
// Services holds an mx::channel that references the directory. Rather than
// calling mxio_service_connect_at, you can call Connect, which satisfies a
// fidl::InterfaceRequest using the directory.
class Services {
 public:
  Services();
  ~Services();

  Services(Services&& other);

  Services& operator=(Services&& other);

  // Creates a request for a directory and stores the other end of the channel
  // in this object for later use by |Connect|.
  //
  // The returned channel is suitable for use in PA_SERVICE_REQUEST.
  mx::channel NewRequest();

  void Bind(mx::channel directory);

  template <typename Interface>
  void Connect(fidl::InterfaceRequest<Interface> request,
               const std::string& service_name = Interface::Name_) {
    ConnectToService(request.PassChannel(), service_name);
  }

  void ConnectToService(const std::string& service_name, mx::channel request);

  const mx::channel& directory() const { return directory_; }

 private:
  mx::channel directory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Services);
};

}  // namespace app

#endif  // APPLICATION_LIB_SVC_SERVICES_H_
