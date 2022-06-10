// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/driver_development_service.h"

#include <fidl/fuchsia.driver.framework/cpp/wire_types.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/service/llcpp/service.h>

#include <queue>
#include <unordered_set>

#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdd = fuchsia_driver_development;
namespace fdf = fuchsia_driver_framework;

namespace driver_manager {

DriverDevelopmentService::DriverDevelopmentService(DriverRunner& driver_runner,
                                                   async_dispatcher_t* dispatcher)
    : driver_runner_(driver_runner), dispatcher_(dispatcher) {}

zx::status<> DriverDevelopmentService::Publish(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](fidl::ServerEnd<fdd::DriverDevelopment> request) {
    fidl::BindServer<fidl::WireServer<fdd::DriverDevelopment>>(this->dispatcher_,
                                                               std::move(request), this);
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdd::DriverDevelopment>,
                                         fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s",
         fidl::DiscoverableProtocolName<fdd::DriverDevelopment>, zx_status_get_string(status));
  }
  return zx::make_status(status);
}

namespace {
class DeviceInfoIterator : public fidl::WireServer<fdd::DeviceInfoIterator> {
 public:
  explicit DeviceInfoIterator(std::unique_ptr<fidl::Arena<512>> arena,
                              std::vector<fdd::wire::DeviceInfo> list)
      : arena_(std::move(arena)), list_(std::move(list)) {}

  void GetNext(GetNextRequestView request, GetNextCompleter::Sync& completer) {
    constexpr size_t kMaxEntries = 50;
    auto result =
        cpp20::span(list_.begin() + offset_, std::min(kMaxEntries, list_.size() - offset_));
    offset_ += result.size();

    completer.Reply(
        fidl::VectorView<fdd::wire::DeviceInfo>::FromExternal(result.data(), result.size()));
  }

 private:
  size_t offset_ = 0;
  std::unique_ptr<fidl::Arena<512>> arena_;
  std::vector<fdd::wire::DeviceInfo> list_;
};
}  // namespace

zx::status<fdd::wire::DeviceInfo> CreateDeviceInfo(fidl::AnyArena& allocator, const Node* node) {
  fdd::wire::DeviceInfo device_info(allocator);

  device_info.set_id(allocator, reinterpret_cast<uint64_t>(node));

  const auto& children = node->children();
  fidl::VectorView<uint64_t> child_ids(allocator, children.size());
  size_t i = 0;
  for (const auto& child : children) {
    child_ids[i++] = reinterpret_cast<uint64_t>(child.get());
  }
  if (!child_ids.empty()) {
    device_info.set_child_ids(allocator, child_ids);
  }

  const auto& parents = node->parents();
  fidl::VectorView<uint64_t> parent_ids(allocator, parents.size());
  i = 0;
  for (const auto* parent : parents) {
    parent_ids[i++] = reinterpret_cast<uint64_t>(parent);
  }
  if (!parent_ids.empty()) {
    device_info.set_parent_ids(allocator, parent_ids);
  }

  device_info.set_moniker(allocator, fidl::StringView(allocator, node->TopoName()));

  const auto& driver_component = node->driver_component();
  if (driver_component.has_value()) {
    const auto* driver_component_ptr = driver_component.value();
    if (driver_component_ptr != nullptr) {
      device_info.set_bound_driver_url(allocator,
                                       fidl::StringView(allocator, driver_component_ptr->url()));
    }
  }

  auto properties = node->properties();
  if (!properties.empty()) {
    fidl::VectorView<fdf::wire::NodeProperty> node_properties(allocator, properties.size());
    for (size_t i = 0; i < properties.size(); ++i) {
      const auto& src = properties[i];
      auto& dst = node_properties[i];
      dst = fdf::wire::NodeProperty(allocator);
      if (src.has_key()) {
        dst.set_key(allocator, src.key());
      }
      if (src.has_value()) {
        dst.set_value(allocator, src.value());
      }
    }
    device_info.set_node_property_list(allocator, node_properties);
  }

  // TODO(fxbug.dev/90735): Get topological path

  auto driver_host = node->driver_host();
  if (driver_host) {
    auto result = driver_host->GetProcessKoid();
    if (result.is_error()) {
      LOGF(ERROR, "Failed to get the process KOID of a driver host: %s",
           zx_status_get_string(result.status_value()));
      return zx::error(result.status_value());
    }
    device_info.set_driver_host_koid(allocator, result.value());
  }

  return zx::ok(device_info);
}

void DriverDevelopmentService::GetDeviceInfo(GetDeviceInfoRequestView request,
                                             GetDeviceInfoCompleter::Sync& completer) {
  auto arena = std::make_unique<fidl::Arena<512>>();
  std::vector<fdd::wire::DeviceInfo> device_infos;

  std::unordered_set<const Node*> unique_nodes;
  std::queue<const Node*> remaining_nodes;
  remaining_nodes.push(driver_runner_.root_node().get());
  while (!remaining_nodes.empty()) {
    auto node = remaining_nodes.front();
    remaining_nodes.pop();
    auto [_, inserted] = unique_nodes.insert(node);
    if (!inserted) {
      // Only insert unique nodes from the DAG.
      continue;
    }
    const auto& children = node->children();
    for (const auto& child : children) {
      remaining_nodes.push(child.get());
    }

    auto topological_name = node->TopoName();
    if (!request->device_filter.empty()) {
      bool found = false;
      for (const auto& device_path : request->device_filter) {
        if (topological_name == device_path.get()) {
          found = true;
          break;
        }
      }
      if (!found) {
        continue;
      }
    }

    auto result = CreateDeviceInfo(*arena, node);
    if (result.is_error()) {
      return;
    }
    device_infos.push_back(std::move(result.value()));
  }
  auto iterator = std::make_unique<DeviceInfoIterator>(std::move(arena), std::move(device_infos));
  fidl::BindServer(this->dispatcher_, std::move(request->iterator), std::move(iterator));
}

void DriverDevelopmentService::GetDriverInfo(GetDriverInfoRequestView request,
                                             GetDriverInfoCompleter::Sync& completer) {
  auto driver_index_client = service::Connect<fdd::DriverIndex>();
  if (driver_index_client.is_error()) {
    LOGF(ERROR, "Failed to connect to service '%s': %s",
         fidl::DiscoverableProtocolName<fdd::DriverIndex>, driver_index_client.status_string());
    request->iterator.Close(driver_index_client.status_value());
    return;
  }

  auto driver_index = fidl::BindSyncClient(std::move(*driver_index_client));
  auto info_result =
      driver_index->GetDriverInfo(std::move(request->driver_filter), std::move(request->iterator));
  if (!info_result.ok()) {
    LOGF(ERROR, "Failed to call DriverIndex::GetDriverInfo: %s\n",
         info_result.error().FormatDescription().data());
  }
}

void DriverDevelopmentService::RestartDriverHosts(RestartDriverHostsRequestView request,
                                                  RestartDriverHostsCompleter::Sync& completer) {
  // TODO(fxbug.dev/90735): Implement RestartDriverHost
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DriverDevelopmentService::BindAllUnboundNodes(BindAllUnboundNodesRequestView request,
                                                   BindAllUnboundNodesCompleter::Sync& completer) {
  auto callback =
      [completer = completer.ToAsync()](
          fidl::VectorView<fuchsia_driver_development::wire::NodeBindingInfo> result) mutable {
        completer.ReplySuccess(result);
      };
  driver_runner_.TryBindAllOrphans(std::move(callback));
}

}  // namespace driver_manager
