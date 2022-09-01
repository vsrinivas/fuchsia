// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_SERVICE_OFFERS_H_
#define SRC_DEVICES_LIB_COMPAT_SERVICE_OFFERS_H_

#include <fidl/fuchsia.component.decl/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

namespace compat {

using FidlServiceOffers = std::vector<std::string>;

class ServiceOffersV1 {
 public:
  ServiceOffersV1(std::string name, fidl::ClientEnd<fuchsia_io::Directory> dir,
                  FidlServiceOffers offers)
      : name_(std::move(name)), dir_(std::move(dir)), offers_(std::move(offers)) {}

  std::vector<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::ArenaBase& arena);

  zx_status_t Serve(async_dispatcher_t* dispatcher, component::OutgoingDirectory* outgoing);

  fidl::UnownedClientEnd<fuchsia_io::Directory> dir() const { return dir_; }

 private:
  std::string name_;
  fidl::ClientEnd<fuchsia_io::Directory> dir_;
  FidlServiceOffers offers_;

  // This callback is called when the class is destructed and it will stop serving the protocol.
  fit::deferred_callback stop_serving_;
};

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_SERVICE_OFFERS_H_
