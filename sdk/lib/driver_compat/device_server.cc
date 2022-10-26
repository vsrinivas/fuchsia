// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/node_add_args.h>
#include <lib/driver_compat/device_server.h>
#include <lib/fdio/directory.h>

namespace compat {

namespace fcd = fuchsia_component_decl;

zx_status_t DeviceServer::AddMetadata(uint32_t type, const void* data, size_t size) {
  Metadata metadata(size);
  auto begin = static_cast<const uint8_t*>(data);
  std::copy(begin, begin + size, metadata.begin());
  auto [_, inserted] = metadata_.emplace(type, std::move(metadata));
  if (!inserted) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  return ZX_OK;
}

zx_status_t DeviceServer::GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  auto it = metadata_.find(type);
  if (it == metadata_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  auto& [_, metadata] = *it;

  auto size = std::min(buflen, metadata.size());
  auto begin = metadata.begin();
  std::copy(begin, begin + size, static_cast<uint8_t*>(buf));

  *actual = metadata.size();
  return ZX_OK;
}

zx_status_t DeviceServer::GetMetadataSize(uint32_t type, size_t* out_size) {
  auto it = metadata_.find(type);
  if (it == metadata_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  auto& [_, metadata] = *it;
  *out_size = metadata.size();
  return ZX_OK;
}

zx_status_t DeviceServer::Serve(async_dispatcher_t* dispatcher,
                                component::OutgoingDirectory* outgoing) {
  component::ServiceInstanceHandler handler;
  fuchsia_driver_compat::Service::Handler compat_service(&handler);
  auto device = [this, dispatcher](
                    fidl::ServerEnd<fuchsia_driver_compat::Device> server_end) mutable -> void {
    fidl::BindServer<fidl::WireServer<fuchsia_driver_compat::Device>>(dispatcher,
                                                                      std::move(server_end), this);
  };
  zx::result<> status = compat_service.add_device(std::move(device));
  if (status.is_error()) {
    return status.error_value();
  }
  status = outgoing->AddService<fuchsia_driver_compat::Service>(std::move(handler), name());
  if (status.is_error()) {
    return status.error_value();
  }
  stop_serving_ = [this, outgoing]() {
    (void)outgoing->RemoveService<fuchsia_driver_compat::Service>(name_);
  };

  if (service_offers_) {
    return service_offers_->Serve(dispatcher, outgoing);
  }
  return ZX_OK;
}

zx_status_t DeviceServer::Serve(async_dispatcher_t* dispatcher,
                                driver::OutgoingDirectory* outgoing) {
  component::ServiceInstanceHandler handler;
  fuchsia_driver_compat::Service::Handler compat_service(&handler);
  auto device = [this, dispatcher](
                    fidl::ServerEnd<fuchsia_driver_compat::Device> server_end) mutable -> void {
    fidl::BindServer<fidl::WireServer<fuchsia_driver_compat::Device>>(dispatcher,
                                                                      std::move(server_end), this);
  };
  zx::result<> status = compat_service.add_device(std::move(device));
  if (status.is_error()) {
    return status.error_value();
  }
  status = outgoing->AddService<fuchsia_driver_compat::Service>(std::move(handler), name());
  if (status.is_error()) {
    return status.error_value();
  }
  stop_serving_ = [this, outgoing]() {
    (void)outgoing->RemoveService<fuchsia_driver_compat::Service>(name_);
  };

  if (service_offers_) {
    return service_offers_->Serve(dispatcher, outgoing);
  }
  return ZX_OK;
}

void DeviceServer::ExportToDevfs(const driver::DevfsExporter& exporter,
                                 std::string_view service_path,
                                 fit::callback<void(zx_status_t)> callback) const {
  exporter.Export(service_path, topological_path_, fuchsia_device_fs::ExportOptions(), proto_id_,
                  std::move(callback));
}

std::vector<fcd::wire::Offer> DeviceServer::CreateOffers(fidl::ArenaBase& arena) {
  std::vector<fcd::wire::Offer> offers;
  // Create the main fuchsia.driver.compat.Service offer.
  offers.push_back(driver::MakeOffer<fuchsia_driver_compat::Service>(arena, name()));

  if (service_offers_) {
    auto service_offers = service_offers_->CreateOffers(arena);
    offers.reserve(offers.size() + service_offers.size());
    offers.insert(offers.end(), service_offers.begin(), service_offers.end());
  }
  return offers;
}

std::vector<fcd::Offer> DeviceServer::CreateOffers() {
  std::vector<fcd::Offer> offers;
  // Create the main fuchsia.driver.compat.Service offer.
  offers.push_back(driver::MakeOffer<fuchsia_driver_compat::Service>(name()));

  if (service_offers_) {
    auto service_offers = service_offers_->CreateOffers();
    offers.reserve(offers.size() + service_offers.size());
    offers.insert(offers.end(), service_offers.begin(), service_offers.end());
  }
  return offers;
}

void DeviceServer::GetTopologicalPath(GetTopologicalPathCompleter::Sync& completer) {
  completer.Reply(fidl::StringView::FromExternal(topological_path_));
}

void DeviceServer::GetMetadata(GetMetadataCompleter::Sync& completer) {
  std::vector<fuchsia_driver_compat::wire::Metadata> metadata;
  metadata.reserve(metadata_.size());
  for (auto& [type, data] : metadata_) {
    fuchsia_driver_compat::wire::Metadata new_metadata;
    new_metadata.type = type;
    zx::vmo vmo;

    zx_status_t status = zx::vmo::create(data.size(), 0, &new_metadata.data);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    status = new_metadata.data.write(data.data(), 0, data.size());
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    size_t size = data.size();
    status = new_metadata.data.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }

    metadata.push_back(std::move(new_metadata));
  }
  completer.ReplySuccess(fidl::VectorView<fuchsia_driver_compat::wire::Metadata>::FromExternal(
      metadata.data(), metadata.size()));
}

void DeviceServer::ConnectFidl(ConnectFidlRequestView request,
                               ConnectFidlCompleter::Sync& completer) {
  if (service_offers_) {
    auto path = std::string("svc/").append(request->name.data(), request->name.size());
    fdio_service_connect_at(service_offers_->dir().channel()->get(), path.data(),
                            request->server.release());
  }
  completer.Reply();
}

}  // namespace compat
