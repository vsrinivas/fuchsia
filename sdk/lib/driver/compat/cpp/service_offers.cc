// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver/compat/cpp/service_offers.h>
#include <lib/driver2/node_add_args.h>
#include <lib/sys/component/cpp/service_client.h>

namespace compat {

namespace fcd = fuchsia_component_decl;

std::vector<fcd::wire::Offer> ServiceOffersV1::CreateOffers(fidl::ArenaBase& arena) {
  std::vector<fcd::wire::Offer> offers;
  for (const auto& service_name : offers_) {
    offers.push_back(driver::MakeOffer(arena, service_name, name_));
  }
  return offers;
}

std::vector<fcd::Offer> ServiceOffersV1::CreateOffers() {
  std::vector<fcd::Offer> offers;
  for (const auto& service_name : offers_) {
    offers.push_back(driver::MakeOffer(service_name, name_));
  }
  return offers;
}

zx_status_t ServiceOffersV1::Serve(async_dispatcher_t* dispatcher,
                                   component::OutgoingDirectory* outgoing) {
  // Add each service in the device as an service in our outgoing directory.
  // We rename each instance from "default" into the child name, and then rename it back to default
  // via the offer.
  for (const auto& service_name : offers_) {
    const auto instance_path = std::string("svc/").append(service_name).append("/default");
    auto client = component::ConnectAt<fuchsia_io::Directory>(dir_, instance_path.c_str());
    if (client.is_error()) {
      return client.status_value();
    }

    const auto path = std::string("svc/").append(service_name);
    auto result = outgoing->AddDirectoryAt(std::move(*client), path, name_);
    if (result.is_error()) {
      return result.error_value();
    }
    stop_serving_ = [this, outgoing, path]() { (void)outgoing->RemoveDirectoryAt(path, name_); };
  }
  return ZX_OK;
}

zx_status_t ServiceOffersV1::Serve(async_dispatcher_t* dispatcher,
                                   driver::OutgoingDirectory* outgoing) {
  // Add each service in the device as an service in our outgoing directory.
  // We rename each instance from "default" into the child name, and then rename it back to default
  // via the offer.
  for (const auto& service_name : offers_) {
    const auto instance_path = std::string("svc/").append(service_name).append("/default");
    auto client = component::ConnectAt<fuchsia_io::Directory>(dir_, instance_path.c_str());
    if (client.is_error()) {
      return client.status_value();
    }

    const auto path = std::string("svc/").append(service_name);
    auto result = outgoing->AddDirectoryAt(std::move(*client), path, name_);
    if (result.is_error()) {
      return result.error_value();
    }
    stop_serving_ = [this, outgoing, path]() { (void)outgoing->RemoveDirectoryAt(path, name_); };
  }
  return ZX_OK;
}

}  // namespace compat
