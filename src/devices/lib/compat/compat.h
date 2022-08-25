// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_COMPAT_H_
#define SRC_DEVICES_LIB_COMPAT_COMPAT_H_

#include <fidl/fuchsia.driver.compat/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/start_args.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/promise.h>
#include <lib/service/llcpp/service_handler.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <unordered_map>
#include <unordered_set>

#include "src/devices/lib/compat/symbols.h"

namespace compat {

using Metadata = std::vector<uint8_t>;
using MetadataMap = std::unordered_map<uint32_t, const Metadata>;
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

// The DeviceServer class vends the fuchsia_driver_compat::Device interface.
// It represents a single device.
class DeviceServer : public fidl::WireServer<fuchsia_driver_compat::Device> {
 public:
  DeviceServer(std::string name, uint32_t proto_id, std::string topological_path,
               MetadataMap metadata, std::optional<ServiceOffersV1> service_offers = std::nullopt)
      : name_(std::move(name)),
        topological_path_(std::move(topological_path)),
        proto_id_(proto_id),
        metadata_(std::move(metadata)),
        service_offers_(std::move(service_offers)) {}

  // Functions to implement the DFv1 device API.
  zx_status_t AddMetadata(uint32_t type, const void* data, size_t size);
  zx_status_t GetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual);
  zx_status_t GetMetadataSize(uint32_t type, size_t* out_size);

  zx_status_t Serve(async_dispatcher_t* dispatcher, component::OutgoingDirectory* outgoing);

  std::vector<fuchsia_component_decl::wire::Offer> CreateOffers(fidl::ArenaBase& arena);

  std::string_view name() const { return name_; }
  std::string_view topological_path() const { return topological_path_; }
  uint32_t proto_id() const { return proto_id_; }

 private:
  // fuchsia.driver.compat.Compat
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& completer) override;
  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer) override;
  void ConnectFidl(ConnectFidlRequestView request, ConnectFidlCompleter::Sync& completer) override;

  std::string name_;
  std::string topological_path_;
  uint32_t proto_id_;
  MetadataMap metadata_;
  std::optional<ServiceOffersV1> service_offers_;

  // This callback is called when the class is destructed and it will stop serving the protocol.
  fit::deferred_callback stop_serving_;
};

zx::status<fidl::WireSharedClient<fuchsia_driver_compat::Device>> ConnectToParentDevice(
    async_dispatcher_t* dispatcher, const driver::Namespace* ns, std::string_view name = "default");

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_COMPAT_H_
