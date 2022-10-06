// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_loader.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/driver2/node_add_args.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/sys/component/cpp/service_client.h>
#include <sched.h>
#include <unistd.h>
#include <zircon/threads.h>

#include <thread>
#include <variant>

#include <fbl/unique_fd.h>

#include "coordinator.h"
#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/lib/log/log.h"

namespace {

// Go through each field in the DriverInfo and copy it with a given allocator.
fuchsia_driver_development::wire::DriverInfo CopyDriverInfo(
    fidl::AnyArena& allocator, fuchsia_driver_development::wire::DriverInfo& driver) {
  auto allocated = fuchsia_driver_development::wire::DriverInfo::Builder(allocator);
  if (driver.has_libname()) {
    allocated.libname(allocator, driver.libname().get());
  }
  if (driver.has_name()) {
    allocated.name(allocator, driver.name().get());
  }
  if (driver.has_url()) {
    allocated.url(allocator, driver.url().get());
  }
  if (driver.has_bind_rules()) {
    if (driver.bind_rules().is_bytecode_v1()) {
      auto vector = fidl::VectorView<fuchsia_device_manager::wire::BindInstruction>(
          allocator, driver.bind_rules().bytecode_v1().count());
      for (size_t i = 0; i < vector.count(); i++) {
        vector[i] = driver.bind_rules().bytecode_v1()[i];
      }
      allocated.bind_rules(
          fuchsia_driver_development::wire::BindRulesBytecode::WithBytecodeV1(allocator, vector));
    }
    if (driver.bind_rules().is_bytecode_v2()) {
      auto vector = fidl::VectorView<uint8_t>(allocator, driver.bind_rules().bytecode_v2().count());
      for (size_t i = 0; i < driver.bind_rules().bytecode_v2().count(); i++) {
        vector[i] = driver.bind_rules().bytecode_v2()[i];
      }
      allocated.bind_rules(fuchsia_driver_development::wire::BindRulesBytecode::WithBytecodeV2(
          allocator, std::move(vector)));
    }
  }
  if (driver.has_package_type()) {
    allocated.package_type(driver.package_type());
  }

  if (driver.has_device_categories()) {
    auto categories = fidl::VectorView<fuchsia_driver_index::wire::DeviceCategory>(
        allocator, driver.device_categories().count());
    for (size_t i = 0; i < categories.count(); i++) {
      auto category_builder = fuchsia_driver_index::wire::DeviceCategory::Builder(allocator);
      if (driver.device_categories()[i].has_category()) {
        category_builder.category(allocator, driver.device_categories()[i].category().get());
      }
      if (driver.device_categories()[i].has_subcategory()) {
        category_builder.subcategory(allocator, driver.device_categories()[i].subcategory().get());
      }
      categories[i] = category_builder.Build();
    }
    allocated.device_categories(categories);
  }

  return allocated.Build();
}

zx::status<fdi::wire::MatchedDriverInfo> GetFidlMatchedDriverInfo(fdi::wire::MatchedDriver driver) {
  if (driver.is_device_group_node()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  if (driver.is_composite_driver()) {
    if (!driver.composite_driver().has_driver_info()) {
      return zx::error(ZX_ERR_NOT_FOUND);
    }

    return zx::ok(driver.composite_driver().driver_info());
  }

  return zx::ok(driver.driver());
}

MatchedCompositeDevice CreateMatchedCompositeDevice(
    fuchsia_driver_index::wire::MatchedCompositeInfo composite_info) {
  MatchedCompositeDevice composite = {};
  if (composite_info.has_num_nodes()) {
    composite.num_nodes = composite_info.num_nodes();
  }

  if (composite_info.has_node_index()) {
    composite.node = composite_info.node_index();
  }

  if (composite_info.has_composite_name()) {
    composite.name =
        std::string(composite_info.composite_name().data(), composite_info.composite_name().size());
  }

  if (composite_info.has_node_names()) {
    std::vector<std::string> names;
    for (auto& name : composite_info.node_names()) {
      names.push_back(std::string(name.data(), name.size()));
    }
    composite.node_names = std::move(names);
  }

  return composite;
}

bool VerifyMatchedDeviceGroupNodeInfo(fdi::wire::MatchedDeviceGroupNodeInfo info) {
  if (!info.has_device_groups() || info.device_groups().empty()) {
    return false;
  }

  for (auto& device_group : info.device_groups()) {
    if (!device_group.has_topological_path() || device_group.topological_path().empty() ||
        !device_group.has_node_index()) {
      return false;
    }
  }

  return true;
}

bool ShouldUseUniversalResolver(fdi::wire::DriverPackageType package_type) {
  return package_type == fdi::wire::DriverPackageType::kUniverse ||
         package_type == fdi::wire::DriverPackageType::kCached;
}

}  // namespace

DriverLoader::~DriverLoader() {
  if (system_loading_thread_) {
    system_loading_thread_->join();
  }
}

void DriverLoader::StartSystemLoadingThread(Coordinator* coordinator) {
  if (system_loading_thread_) {
    LOGF(ERROR, "DriverLoader: StartLoadingThread cannot be called twice!\n");
    return;
  }

  system_loading_thread_ = std::thread([coordinator]() {
    fbl::unique_fd fd(open("/system", O_RDONLY));
    if (fd.get() < 0) {
      LOGF(WARNING, "Unable to open '/system', system drivers are disabled");
      return;
    }

    fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers;

    auto driver_added = [&drivers](Driver* drv, const char* version) {
      std::unique_ptr<Driver> driver(drv);
      LOGF(INFO, "Adding driver '%s' '%s'", driver->name.data(), driver->libname.data());
      if (load_vmo(driver->libname.data(), &driver->dso_vmo)) {
        LOGF(ERROR, "Driver '%s' '%s' could not cache DSO", driver->name.data(),
             driver->libname.data());
      }
      // De-prioritize drivers that are "fallback".
      if (driver->fallback) {
        drivers.push_back(std::move(driver));
      } else {
        drivers.push_front(std::move(driver));
      }
    };

    find_loadable_drivers(coordinator->boot_args(), "/system/driver", driver_added);

    async::PostTask(coordinator->dispatcher(),
                    [coordinator = coordinator, drivers = std::move(drivers)]() mutable {
                      coordinator->AddAndBindDrivers(std::move(drivers));
                      coordinator->BindFallbackDrivers();
                    });
  });

  constexpr char name[] = "driver-loader-thread";
  zx_object_set_property(native_thread_get_zx_handle(system_loading_thread_->native_handle()),
                         ZX_PROP_NAME, name, sizeof(name));
}

const Driver* DriverLoader::LibnameToDriver(std::string_view libname) const {
  for (const auto& drv : driver_index_drivers_) {
    if (libname.compare(drv.libname) == 0) {
      return &drv;
    }
  }
  return nullptr;
}

void DriverLoader::WaitForBaseDrivers(fit::callback<void()> callback) {
  // TODO(dgilhooley): Change this back to an ERROR once DriverIndex is used in all tests.
  if (!driver_index_.is_valid()) {
    LOGF(INFO, "%s: DriverIndex is not initialized", __func__);
    return;
  }

  driver_index_->WaitForBaseDrivers().Then(
      [this, callback = std::move(callback)](
          fidl::WireUnownedResult<fdi::DriverIndex::WaitForBaseDrivers>& result) mutable {
        if (!result.ok()) {
          // Since IsolatedDevmgr doesn't use the ComponentFramework, DriverIndex can be
          // closed before DriverManager during tests, which would mean we would see
          // a ZX_ERR_PEER_CLOSED.
          if (result.status() == ZX_ERR_PEER_CLOSED) {
            LOGF(WARNING, "Connection to DriverIndex closed during WaitForBaseDrivers.");
          } else {
            LOGF(ERROR, "Failed to connect to DriverIndex: %s",
                 result.error().FormatDescription().c_str());
          }

          return;
        }
        include_fallback_drivers_ = true;
        callback();
      });
}

const Driver* DriverLoader::LoadDriverUrl(const std::string& driver_url,
                                          bool use_universe_resolver) {
  // Check if we've already loaded this driver. If we have then return it.
  auto driver = LibnameToDriver(driver_url);
  if (driver != nullptr) {
    return driver;
  }

  // Pick the correct package resolver to use.
  auto resolver = base_resolver_;
  if (use_universe_resolver && universe_resolver_) {
    resolver = universe_resolver_;
  }

  // We've never seen the driver before so add it, then return it.
  auto fetched_driver = resolver->FetchDriver(driver_url);
  if (fetched_driver.is_error()) {
    LOGF(ERROR, "Error fetching driver: %s: %d", driver_url.data(), fetched_driver.error_value());
    return nullptr;
  }
  // It's possible the driver is nullptr if it was disabled.
  if (!fetched_driver.value()) {
    return nullptr;
  }

  // Success. Return driver.
  driver_index_drivers_.push_back(std::move(fetched_driver.value()));
  return &driver_index_drivers_.back();
}

const Driver* DriverLoader::LoadDriverUrl(fdi::wire::MatchedDriverInfo driver_info) {
  if (!driver_info.has_driver_url()) {
    LOGF(ERROR, "Driver info is missing the driver URL");
    return nullptr;
  }

  std::string driver_url(driver_info.driver_url().get());
  bool use_universe_resolver =
      driver_info.has_package_type() && ShouldUseUniversalResolver(driver_info.package_type());
  return LoadDriverUrl(driver_url, use_universe_resolver);
}

bool DriverLoader::MatchesLibnameDriverIndex(const std::string& driver_url,
                                             std::string_view libname) {
  if (libname.compare(driver_url) == 0) {
    return true;
  }

  auto result = GetPathFromUrl(driver_url);
  if (result.is_error()) {
    return false;
  }
  auto driver_path = result.value();

  // If `libname` is a relative path then check if `driver_path` ends with
  // `libname`.
  if (!libname.empty() && libname[0] != '/' && libname.length() <= driver_path.length()) {
    return !driver_path.compare(driver_path.length() - libname.length(), libname.length(), libname);
  }

  return driver_path == libname;
}

void DriverLoader::AddDeviceGroup(fuchsia_driver_framework::wire::DeviceGroup group,
                                  AddToIndexCallback callback) {
  auto result = driver_index_.sync()->AddDeviceGroup(group);
  if (!result.ok()) {
    LOGF(ERROR, "DriverIndex::AddDeviceGroup failed %d", result.status());
    callback(zx::error(result.status()));
    return;
  }
  if (result->is_error()) {
    callback(result->take_error());
    return;
  }

  callback(zx::ok(fidl::ToNatural(*result->value())));
}

const std::vector<MatchedDriver> DriverLoader::MatchDeviceDriverIndex(
    const fbl::RefPtr<Device>& dev, const MatchDeviceConfig& config) {
  if (!driver_index_.is_valid()) {
    return std::vector<MatchedDriver>();
  }

  bool autobind = config.libname.empty();

  fidl::Arena allocator;
  auto& props = dev->props();
  auto& str_props = dev->str_props();
  size_t size = props.size() + str_props.size() + 2;
  if (!autobind) {
    size += 1;
  }
  fidl::VectorView<fdf::wire::NodeProperty> fidl_props(allocator, size);

  size_t index = 0;
  fidl_props[index++] = driver::MakeProperty(allocator, BIND_PROTOCOL, dev->protocol_id());
  fidl_props[index++] = driver::MakeProperty(allocator, BIND_AUTOBIND, autobind);
  // If we are looking for a specific driver, we add a property to the device with the
  // name of the driver we are looking for. Drivers can then bind to this.
  if (!autobind) {
    fidl_props[index++] = driver::MakeProperty(allocator, "fuchsia.compat.LIBNAME", config.libname);
  }

  for (size_t i = 0; i < props.size(); i++) {
    fidl_props[index++] = driver::MakeProperty(allocator, props[i].id, props[i].value);
  }

  for (size_t i = 0; i < str_props.size(); i++) {
    switch (str_props[i].value.index()) {
      case StrPropValueType::Integer: {
        fidl_props[index++] = driver::MakeProperty(
            allocator, str_props[i].key, std::get<StrPropValueType::Integer>(str_props[i].value));
        break;
      }
      case StrPropValueType::String: {
        fidl_props[index++] = driver::MakeProperty(
            allocator, str_props[i].key, std::get<StrPropValueType::String>(str_props[i].value));
        break;
      }
      case StrPropValueType::Bool: {
        fidl_props[index++] = driver::MakeProperty(
            allocator, str_props[i].key, std::get<StrPropValueType::Bool>(str_props[i].value));
        break;
      }
      case StrPropValueType::Enum: {
        fidl_props[index++] = driver::MakeEnumProperty(
            allocator, str_props[i].key, std::get<StrPropValueType::Enum>(str_props[i].value));
        break;
      }
    }
  }

  return MatchPropertiesDriverIndex(fidl_props, config);
}

const std::vector<MatchedDriver> DriverLoader::MatchPropertiesDriverIndex(
    fidl::VectorView<fdf::wire::NodeProperty> props, const MatchDeviceConfig& config) {
  std::vector<MatchedDriver> matched_drivers;
  std::vector<MatchedDriver> matched_fallback_drivers;
  if (!driver_index_.is_valid()) {
    return matched_drivers;
  }

  fidl::Arena allocator;
  fdf::wire::NodeAddArgs args(allocator);
  args.set_properties(allocator, std::move(props));

  auto result = driver_index_.sync()->MatchDriversV1(std::move(args));
  if (!result.ok()) {
    if (result.status() != ZX_OK) {
      LOGF(ERROR, "DriverIndex::MatchDriversV1 failed: %d", result.status());
      return matched_drivers;
    }
  }
  // If there's no driver to match then DriverIndex will return ZX_ERR_NOT_FOUND.
  if (result->is_error()) {
    if (result->error_value() != ZX_ERR_NOT_FOUND) {
      LOGF(ERROR, "DriverIndex: MatchDriversV1 returned error: %d", result->error_value());
    }
    return matched_drivers;
  }

  const auto& drivers = result->value()->drivers;

  for (auto driver : drivers) {
    if (driver.is_device_group_node()) {
      if (!VerifyMatchedDeviceGroupNodeInfo(driver.device_group_node())) {
        LOGF(ERROR,
             "DriverIndex: MatchDriverV1 response is missing fields in MatchedDeviceGroupInfo");
        continue;
      }

      matched_drivers.push_back(fidl::ToNatural(driver.device_group_node()));
      continue;
    }

    auto fidl_driver_info = GetFidlMatchedDriverInfo(driver);
    if (fidl_driver_info.is_error()) {
      LOGF(ERROR, "DriverIndex: MatchedDriversV1 response is missing MatchedDriverInfo");
      continue;
    }

    if (!fidl_driver_info->has_is_fallback()) {
      LOGF(ERROR, "DriverIndex: MatchDriversV1 response is missing is_fallback");
      continue;
    }

    MatchedDriverInfo matched_driver_info = {};
    if (fidl_driver_info->has_colocate()) {
      matched_driver_info.colocate = fidl_driver_info->colocate();
    }

    // If we have a driver_url we are a DFv1 driver. Otherwise are are DFv2.
    if (fidl_driver_info->has_driver_url()) {
      auto loaded_driver = LoadDriverUrl(fidl_driver_info.value());
      if (!loaded_driver) {
        continue;
      }
      matched_driver_info.driver = loaded_driver;
    } else if (fidl_driver_info->has_url()) {
      matched_driver_info.driver = Dfv2Driver{
          .url = std::string(fidl_driver_info->url().get()),
          .package_type = fidl_driver_info->package_type(),
      };
    } else {
      LOGF(ERROR, "DriverIndex: MatchDriversV1 response is missing url");
      continue;
    }

    auto driver_url = std::string(matched_driver_info.name());
    if (!fidl_driver_info->is_fallback() && config.only_return_base_and_fallback_drivers &&
        IsFuchsiaBootScheme(driver_url)) {
      continue;
    }

    MatchedDriver matched_driver;
    if (driver.is_composite_driver()) {
      matched_driver = MatchedCompositeDriverInfo{
          .composite = CreateMatchedCompositeDevice(driver.composite_driver()),
          .driver_info = matched_driver_info,
      };
    } else {
      matched_driver = matched_driver_info;
    }

    if (config.libname.empty() || MatchesLibnameDriverIndex(driver_url, config.libname)) {
      if (fidl_driver_info->is_fallback()) {
        if (include_fallback_drivers_ || !config.libname.empty()) {
          matched_fallback_drivers.push_back(matched_driver);
        }
      } else {
        matched_drivers.push_back(matched_driver);
      }
    }
  }

  // Fallback drivers need to be at the end of the matched drivers.
  matched_drivers.insert(matched_drivers.end(), matched_fallback_drivers.begin(),
                         matched_fallback_drivers.end());

  return matched_drivers;
}

zx::status<std::vector<fuchsia_driver_development::wire::DriverInfo>> DriverLoader::GetDriverInfo(
    fidl::AnyArena& allocator, fidl::VectorView<fidl::StringView> filter) {
  std::vector<fuchsia_driver_development::wire::DriverInfo> info;

  auto driver_index_client = component::Connect<fuchsia_driver_development::DriverIndex>();
  if (driver_index_client.is_error()) {
    LOGF(WARNING, "Failed to connect to fuchsia_driver_development::DriverIndex\n");
    return driver_index_client.take_error();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_driver_development::DriverInfoIterator>();
  if (endpoints.is_error()) {
    LOGF(ERROR, "fidl::CreateEndpoints failed: %s\n", endpoints.status_string());
    return endpoints.take_error();
  }

  fidl::WireSyncClient driver_index{std::move(*driver_index_client)};
  auto info_result = driver_index->GetDriverInfo(filter, std::move(endpoints->server));

  // There are still some environments where we can't connect to DriverIndex.
  if (info_result.status() != ZX_OK) {
    LOGF(INFO, "DriverIndex:GetDriverInfo failed: %d\n", info_result.status());
    return zx::error(info_result.status());
  }

  fidl::WireSyncClient iterator{std::move(endpoints->client)};
  for (;;) {
    auto next_result = iterator->GetNext();
    if (!next_result.ok()) {
      // This is likely a pipelined error from the GetDriverInfo call above. We unfortunately
      // cannot read the epitaph without using an async call.
      LOGF(ERROR, "DriverInfoIterator.GetNext failed: %s\n",
           next_result.FormatDescription().c_str());
      break;
    }
    if (next_result.value().drivers.count() == 0) {
      // When we receive 0 responses, we are done iterating.
      break;
    }
    // Go through each driver info and create a copy.
    for (auto& driver : next_result.value().drivers) {
      info.push_back(CopyDriverInfo(allocator, driver));
    }
  }

  return zx::ok(std::move(info));
}

std::vector<const Driver*> DriverLoader::GetAllDriverIndexDrivers() {
  std::vector<const Driver*> drivers;

  auto driver_index_client = component::Connect<fuchsia_driver_development::DriverIndex>();
  if (driver_index_client.is_error()) {
    LOGF(WARNING, "Failed to connect to fuchsia_driver_development::DriverIndex\n");
    return drivers;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_driver_development::DriverInfoIterator>();
  if (endpoints.is_error()) {
    LOGF(ERROR, "fidl::CreateEndpoints failed: %s\n", endpoints.status_string());
    return drivers;
  }

  fidl::WireSyncClient driver_index{std::move(*driver_index_client)};
  auto info_result = driver_index->GetDriverInfo(fidl::VectorView<fidl::StringView>(),
                                                 std::move(endpoints->server));

  // There are still some environments where we can't connect to DriverIndex.
  if (info_result.status() != ZX_OK) {
    LOGF(INFO, "DriverIndex:GetDriverInfo failed: %d\n", info_result.status());
    return drivers;
  }

  fidl::WireSyncClient iterator{std::move(endpoints->client)};
  for (;;) {
    auto next_result = iterator->GetNext();
    if (!next_result.ok()) {
      // This is likely a pipelined error from the GetDriverInfo call above. We unfortunately
      // cannot read the epitaph without using an async call.
      LOGF(ERROR, "DriverInfoIterator.GetNext failed: %s\n",
           next_result.FormatDescription().c_str());
      break;
    }
    if (next_result.value().drivers.count() == 0) {
      // When we receive 0 responses, we are done iterating.
      break;
    }
    for (auto driver : next_result.value().drivers) {
      if (!driver.has_libname()) {
        continue;
      }

      std::string url(driver.libname().data(), driver.libname().size());
      bool use_universe_resolver = false;
      if (driver.has_package_type()) {
        use_universe_resolver = ShouldUseUniversalResolver(driver.package_type());
      }

      const Driver* drv = LoadDriverUrl(url, use_universe_resolver);
      if (drv) {
        drivers.push_back(drv);
      }
    }
  }

  return drivers;
}
