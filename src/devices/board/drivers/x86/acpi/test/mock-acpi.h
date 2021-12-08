// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_TEST_MOCK_ACPI_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_TEST_MOCK_ACPI_H_

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/test/device.h"

namespace acpi::test {

constexpr const char* kPciPnpID = "PNP0A03";
constexpr const char* kPciePnpID = "PNP0A08";

class MockAcpi : public Acpi {
 public:
  void SetDeviceRoot(std::unique_ptr<Device> new_root) { root_ = std::move(new_root); }
  Device* GetDeviceRoot() { return root_.get(); }

  acpi::status<> WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object, uint32_t max_depth,
                               NamespaceCallable cbk) override;

  acpi::status<> WalkResources(ACPI_HANDLE object, const char* resource_name,
                               ResourcesCallable cbk) override;

  // Set the value returned by |BufferToResource|.
  void SetResource(acpi::UniquePtr<ACPI_RESOURCE> ptr) { resource_ = std::move(ptr); }
  acpi::status<acpi::UniquePtr<ACPI_RESOURCE>> BufferToResource(
      cpp20::span<uint8_t> buffer) override {
    ZX_ASSERT_MSG(resource_.get() != nullptr, "Unexpected call to BufferToResource");
    return acpi::ok(std::move(resource_));
  }

  using DeviceCallable = std::function<acpi::status<>(ACPI_HANDLE device, uint32_t depth)>;
  acpi::status<> GetDevices(const char* hid, DeviceCallable cbk) override {
    return acpi::error(AE_NOT_IMPLEMENTED);
  }

  acpi::status<acpi::UniquePtr<ACPI_OBJECT>> EvaluateObject(
      ACPI_HANDLE object, const char* pathname,
      std::optional<std::vector<ACPI_OBJECT>> args) override;

  acpi::status<acpi::UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj) override;
  acpi::status<ACPI_HANDLE> GetParent(ACPI_HANDLE child) override {
    return acpi::ok(ToDevice(child)->parent());
  }

  acpi::status<ACPI_HANDLE> GetHandle(ACPI_HANDLE parent, const char* pathname) override;

  acpi::status<std::string> GetPath(ACPI_HANDLE object) override;
  acpi::status<> InstallNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                      NotifyHandlerCallable callable, void* context) override;
  acpi::status<> RemoveNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                     NotifyHandlerCallable callable) override;

  acpi::status<uint32_t> AcquireGlobalLock(uint16_t timeout) override __TA_ACQUIRE(global_lock_) {
    global_lock_.lock();
    return acpi::ok(0xd00dfeed);
  }
  acpi::status<> ReleaseGlobalLock(uint32_t handle) override __TA_RELEASE(global_lock_) {
    ZX_ASSERT_MSG(handle == 0xd00dfeed, "global lock did not match handle");
    global_lock_.unlock();
    return acpi::ok();
  }

 private:
  Device* ToDevice(ACPI_HANDLE hnd) {
    // NOLINTNEXTLINE - ACPI_ROOT_OBJECT makes clang-tidy complain.
    if (hnd == ACPI_ROOT_OBJECT) {
      return root_.get();
    }
    return static_cast<Device*>(hnd);
  }

  acpi::status<> WalkNamespaceInternal(ACPI_OBJECT_TYPE type, Device* start_object,
                                       uint32_t max_depth, uint32_t cur_depth,
                                       NamespaceCallable& cbk);
  std::unique_ptr<Device> root_;
  acpi::UniquePtr<ACPI_RESOURCE> resource_;
  std::mutex global_lock_;
};

}  // namespace acpi::test
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_TEST_MOCK_ACPI_H_
