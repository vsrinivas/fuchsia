// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>

#include <ddk/binding.h>
#include <zxtest/zxtest.h>

#include "multiple_device_test.h"
#include "package_resolver.h"

class FakePackageResolver : public internal::PackageResolverInterface {
 public:
  struct DriverInfo {
    std::string package_url;
    std::string libname;
    zx::vmo vmo;
  };

  // Saves the |libname| and |vmo| that will be returned when |FetchDriverVmo| is queried
  // with |package_url|.
  void Register(std::string package_url, std::string libname, zx::vmo vmo) {
    registered_drivers_.push_back(DriverInfo{package_url, libname, std::move(vmo)});
  }

  // PackageResolverInterface implementation.
  zx::status<internal::PackageResolver::FetchDriverVmoResult> FetchDriverVmo(
      const std::string& package_url) override {
    for (auto& driver_info : registered_drivers_) {
      if (driver_info.package_url == package_url) {
        return zx::ok(internal::PackageResolver::FetchDriverVmoResult{driver_info.libname,
                                                                      std::move(driver_info.vmo)});
      }
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }

 private:
  std::vector<DriverInfo> registered_drivers_;
};

class LoadDriverPackageTestCase : public MultipleDeviceTestCase {
 public:
  LoadDriverPackageTestCase() : MultipleDeviceTestCase(true /* enable_ephemeral */) {}
};

TEST_F(LoadDriverPackageTestCase, LoadRegisteredDriver) {
  auto& drivers = coordinator()->drivers();
  size_t num_drivers = drivers.size_slow();

  // Borrow a valid driver vmo that can be duplicated as executable.
  zx::vmo driver_vmo;
  ASSERT_OK(coordinator()->LibnameToVmo(coordinator()->fragment_driver()->libname, &driver_vmo));

  std::string package_url("test_driver_url");
  std::string libname("test_driver_libname");

  FakePackageResolver resolver;
  resolver.Register(package_url, libname, std::move(driver_vmo));

  ASSERT_OK(coordinator()->LoadEphemeralDriver(&resolver, package_url.c_str()));
  coordinator_loop()->RunUntilIdle();

  // A new driver should be added.
  ASSERT_EQ(drivers.size_slow(), num_drivers + 1);
  ASSERT_EQ(drivers.back().libname, libname);

  // Attempting to bind the fragment driver will create a proxy device, which we need to detach
  // from its parent to avoid a memory leak.
  coordinator()->root_device()->proxy()->DetachFromParent();
}

TEST_F(LoadDriverPackageTestCase, LoadUnregisteredDriver) {
  auto& drivers = coordinator()->drivers();
  size_t num_drivers = drivers.size_slow();

  FakePackageResolver resolver;
  std::string package_url("test_driver_url");
  ASSERT_NOT_OK(coordinator()->LoadEphemeralDriver(&resolver, package_url));

  coordinator_loop()->RunUntilIdle();
  // No new driver should be added.
  ASSERT_EQ(drivers.size_slow(), num_drivers);
}

// Test that loading drivers ephemerally can be disabled.
class EphemeralDisabledTestCase : public MultipleDeviceTestCase {
 public:
  EphemeralDisabledTestCase() : MultipleDeviceTestCase(false /* enable_ephemeral */) {}
};

TEST_F(EphemeralDisabledTestCase, LoadingDriverFails) {
  ASSERT_DEATH([&] {
    FakePackageResolver resolver;
    std::string package_url("test_driver_url");
    coordinator()->LoadEphemeralDriver(&resolver, package_url);
  });
}
