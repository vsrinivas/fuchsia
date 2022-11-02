// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPAT_CPP_SERVICE_OFFERS_H_
#define LIB_DRIVER_COMPAT_CPP_SERVICE_OFFERS_H_

#include <fidl/fuchsia.component.decl/cpp/fidl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/driver/component/cpp/outgoing_directory.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

namespace compat {

using FidlServiceOffers = std::vector<std::string>;

// The SerivceOffers class supports the services that a DFv1 driver exports.
// DFv1 drivers export their services via a directory, with a list of names
// of the services hosted in that directory.
class ServiceOffersV1 {
 public:
  ServiceOffersV1(std::string name, fidl::ClientEnd<fuchsia_io::Directory> dir,
                  FidlServiceOffers offers)
      : name_(std::move(name)), dir_(std::move(dir)), offers_(std::move(offers)) {}

  // Service this interface in an outgoing directory.
  zx_status_t Serve(async_dispatcher_t* dispatcher, component::OutgoingDirectory* outgoing);
  zx_status_t Serve(async_dispatcher_t* dispatcher, driver::OutgoingDirectory* outgoing);

  // Create offers to offer these services to another component.
  std::vector<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::ArenaBase& arena);
  std::vector<fuchsia_component_decl::Offer> CreateOffers();

  fidl::UnownedClientEnd<fuchsia_io::Directory> dir() const { return dir_; }

 private:
  std::string name_;
  fidl::ClientEnd<fuchsia_io::Directory> dir_;
  FidlServiceOffers offers_;

  // This callback is called when the class is destructed and it will stop serving the protocol.
  fit::deferred_callback stop_serving_;
};

}  // namespace compat

#endif  // LIB_DRIVER_COMPAT_CPP_SERVICE_OFFERS_H_
