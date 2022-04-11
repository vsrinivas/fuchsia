// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compat.h"

#include "lib/fpromise/promise.h"
#include "src/devices/lib/driver2/devfs_exporter.h"

namespace compat {

namespace fdf = fuchsia_driver_framework;
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

void DeviceServer::GetTopologicalPath(GetTopologicalPathRequestView request,
                                      GetTopologicalPathCompleter::Sync& completer) {
  completer.Reply(fidl::StringView::FromExternal(topological_path_));
}

void DeviceServer::GetMetadata(GetMetadataRequestView request,
                               GetMetadataCompleter::Sync& completer) {
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

zx::status<Interop> Interop::Create(async_dispatcher_t* dispatcher, const driver::Namespace* ns,
                                    service::OutgoingDirectory* outgoing) {
  Interop interop;
  interop.dispatcher_ = dispatcher;
  interop.ns_ = ns;
  interop.outgoing_ = outgoing;

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  // Serve a connection to `svc_dir_`.
  zx_status_t status =
      interop.outgoing_->vfs().Serve(interop.outgoing_->svc_dir(), endpoints->server.TakeChannel(),
                                     fs::VnodeConnectionOptions::ReadWrite());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto exporter = driver::DevfsExporter::Create(
      *interop.ns_, interop.dispatcher_,
      fidl::WireSharedClient(std::move(endpoints->client), dispatcher));
  if (exporter.is_error()) {
    return zx::error(exporter.error_value());
  }
  interop.exporter_ = std::move(*exporter);

  interop.compat_service_ = fbl::MakeRefCounted<fs::PseudoDir>();
  interop.outgoing_->root_dir()->AddEntry("fuchsia.driver.compat.Service", interop.compat_service_);

  return zx::ok(std::move(interop));
}

fpromise::promise<void, zx_status_t> Interop::ConnectToParentCompatService() {
  auto result = ns_->OpenService<fuchsia_driver_compat::Service>("default");
  if (result.is_error()) {
    return fpromise::make_error_promise(result.status_value());
  }
  auto connection = result.value().connect_device();
  if (connection.is_error()) {
    return fpromise::make_error_promise(connection.status_value());
  }
  device_client_ = fidl::WireSharedClient<fuchsia_driver_compat::Device>(
      std::move(connection.value()), dispatcher_);

  return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
}

fpromise::promise<void, zx_status_t> Interop::ExportChild(Child* child,
                                                          fbl::RefPtr<fs::Vnode> dev_node) {
  // Expose the fuchsia.driver.compat.Service instance.
  service::ServiceHandler handler;
  fuchsia_driver_compat::Service::Handler compat_service(&handler);
  auto device = [this,
                 child](fidl::ServerEnd<fuchsia_driver_compat::Device> server_end) mutable -> void {
    fidl::BindServer<fidl::WireServer<fuchsia_driver_compat::Device>>(
        dispatcher_, std::move(server_end), &child->compat_device());
  };
  zx::status<> status = compat_service.add_device(std::move(device));
  if (status.is_error()) {
    return fpromise::make_error_promise(status.error_value());
  }
  compat_service_->AddEntry(child->name(), handler.TakeDirectory());
  ServiceInstanceOffer instance_offer;
  instance_offer.service_name = "fuchsia.driver.compat.Service";
  instance_offer.instance_name = child->name();
  instance_offer.renamed_instance_name = "default";
  instance_offer.remove_service_callback = std::make_shared<fit::deferred_callback>(
      [service = this->compat_service_, name = std::string(child->name())]() {
        service->RemoveEntry(name);
      });
  child->offers().AddServiceInstance(std::move(instance_offer));

  // Expose the child in /dev/.
  if (!dev_node) {
    return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
  }
  zx_status_t add_status = outgoing_->svc_dir()->AddEntry(child->name(), dev_node);
  if (add_status != ZX_OK) {
    return fpromise::make_error_promise<zx_status_t>(add_status);
  }
  // If the child goes out of scope, we should close the devfs connection.
  child->AddCallback(std::make_shared<fit::deferred_callback>(
      [this, name = std::string(child->name()), dev_node]() {
        outgoing_->vfs().CloseAllConnectionsForVnode(*dev_node, {});
        outgoing_->svc_dir()->RemoveEntry(name);
      }));

  return exporter_.Export(child->name(), child->topological_path(), child->proto_id());
}

std::vector<fuchsia_component_decl::wire::Offer> Child::CreateOffers(fidl::ArenaBase& arena) {
  return offers_.CreateOffers(arena);
}

std::vector<fuchsia_component_decl::wire::Offer> ChildOffers::CreateOffers(fidl::ArenaBase& arena) {
  std::vector<fuchsia_component_decl::wire::Offer> offers;
  for (auto& instance : instance_offers_) {
    auto dir_offer = fcd::wire::OfferDirectory::Builder(arena);
    dir_offer.source_name(arena, instance.service_name);
    if (instance.renamed_instance_name) {
      dir_offer.target_name(arena, instance.service_name + "-" + *instance.renamed_instance_name);
    }
    dir_offer.rights(fuchsia_io::wire::kRwStarDir);
    dir_offer.subdir(arena, instance.instance_name);
    dir_offer.dependency_type(fcd::wire::DependencyType::kStrong);

    offers.push_back(fcd::wire::Offer::WithDirectory(arena, dir_offer.Build()));
  }
  // XXX: Do protocols.
  return offers;
}

}  // namespace compat
