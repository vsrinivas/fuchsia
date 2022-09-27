// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/fidl.h>
#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/binding.h>
#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <variant>

#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>

struct Driver : public fbl::DoublyLinkedListable<std::unique_ptr<Driver>> {
  Driver() = default;

  fbl::String name;

  uint32_t bytecode_version = 0;

  // Unlike the old bytecode format, the instructions in the new format are not
  // represented by three uint32 integers. To support both formats
  // simultaneously, zx_bind_inst_t values are used to represent the old bytecode
  // instructions while uint8_t values are used to represent the new bytecode.
  std::variant<std::unique_ptr<zx_bind_inst_t[]>, std::unique_ptr<uint8_t[]>> binding;

  // Number of bytes in the bind rules.
  uint32_t binding_size = 0;

  // If this is true, this driver should only be bound after /system/ comes up.
  bool fallback = false;

  uint32_t flags = 0;
  zx::vmo dso_vmo;

  fbl::String libname;

  // If this is valid, it's the root directory of the Driver's package.
  fbl::unique_fd package_dir;

  // If true, this driver never tries to match against new devices.
  bool never_autoselect = false;
};

struct Dfv2Driver {
  std::string url;
  fuchsia_driver_index::DriverPackageType package_type;
};

struct MatchedCompositeDevice {
  uint32_t node;
  uint32_t num_nodes;
  std::string name;
  std::vector<std::string> node_names;
};

struct MatchedDriverInfo {
  std::variant<const Driver*, Dfv2Driver> driver;
  bool colocate = false;

  bool is_v1() const { return std::holds_alternative<const Driver*>(driver); }

  const Driver* v1() const { return std::get<const Driver*>(driver); }

  const Dfv2Driver& v2() const { return std::get<Dfv2Driver>(driver); }

  const char* name() const {
    if (is_v1()) {
      return v1()->libname.c_str();
    }
    return v2().url.c_str();
  }
};

struct MatchedCompositeDriverInfo {
  MatchedCompositeDevice composite;
  MatchedDriverInfo driver_info;
};

using MatchedDriver = std::variant<MatchedDriverInfo, MatchedCompositeDriverInfo,
                                   fuchsia_driver_index::MatchedDeviceGroupNodeInfo>;

#define DRIVER_NAME_LEN_MAX 64

using DriverLoadCallback = fit::function<void(Driver* driver, const char* version)>;

void load_driver(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args, const char* path,
                 DriverLoadCallback func);
zx_status_t load_driver_vmo(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args,
                            std::string_view libname, zx::vmo vmo, DriverLoadCallback func);
zx_status_t load_vmo(std::string_view libname, zx::vmo* out_vmo);
void find_loadable_drivers(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args,
                           const std::string& path, DriverLoadCallback func);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_H_
