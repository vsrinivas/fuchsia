// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_ACPI_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_ACPI_H_

#include <lib/stdcompat/span.h>

#include <functional>
#include <optional>
#include <vector>

#include <acpica/acpi.h>

#include "src/devices/board/lib/acpi/object.h"
#include "src/devices/board/lib/acpi/status.h"
#include "src/devices/board/lib/acpi/util.h"

namespace acpi {

enum WalkDirection {
  Descending,
  Ascending,
};

// Wrapper class used to interface with ACPICA (in the real system),
// or a mock ACPI implementation (in tests).
class Acpi {
 public:
  static constexpr uint32_t kMaxNamespaceDepth = 100;

  virtual ~Acpi() = default;
  // A utility function which can be used to invoke the ACPICA library's
  // AcpiWalkNamespace function, but with an arbitrary Callable instead of needing
  // to use C-style callbacks with context pointers.
  using NamespaceCallable =
      std::function<acpi::status<>(ACPI_HANDLE object, uint32_t level, WalkDirection dir)>;
  virtual acpi::status<> WalkNamespace(ACPI_OBJECT_TYPE type, ACPI_HANDLE start_object,
                                       uint32_t max_depth, NamespaceCallable cbk) = 0;
  // A utility function which can be used to invoke the ACPICA library's
  // AcpiWalkResources function, but with an arbitrary Callable instead of needing
  // to use C-style callbacks with context pointers.
  using ResourcesCallable = std::function<acpi::status<>(ACPI_RESOURCE* res)>;
  virtual acpi::status<> WalkResources(ACPI_HANDLE object, const char* resource_name,
                                       ResourcesCallable cbk) = 0;
  virtual acpi::status<acpi::UniquePtr<ACPI_RESOURCE>> BufferToResource(
      cpp20::span<uint8_t> buffer) = 0;

  using DeviceCallable = std::function<acpi::status<>(ACPI_HANDLE device, uint32_t depth)>;
  virtual acpi::status<> GetDevices(const char* hid, DeviceCallable cbk) = 0;

  virtual acpi::status<acpi::UniquePtr<ACPI_OBJECT>> EvaluateObject(
      ACPI_HANDLE object, const char* pathname, std::optional<std::vector<ACPI_OBJECT>> args) = 0;

  // Get the ACPI_DEVICE_INFO for the given object.
  virtual acpi::status<acpi::UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj) = 0;

  // Get the parent of the given child.
  virtual acpi::status<ACPI_HANDLE> GetParent(ACPI_HANDLE child) = 0;

  // Get the handle retrieved by resolving the given pathname from |parent|.
  virtual acpi::status<ACPI_HANDLE> GetHandle(ACPI_HANDLE parent, const char* pathname) = 0;

  // Get the absolute path to the given object.
  virtual acpi::status<std::string> GetPath(ACPI_HANDLE object) = 0;

  using NotifyHandlerCallable = ACPI_NOTIFY_HANDLER;
  virtual acpi::status<> InstallNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                              NotifyHandlerCallable callable, void* context) = 0;
  virtual acpi::status<> RemoveNotifyHandler(ACPI_HANDLE object, uint32_t mode,
                                             NotifyHandlerCallable callable) = 0;

  acpi::status<uint8_t> CallBbn(ACPI_HANDLE obj);
  acpi::status<uint16_t> CallSeg(ACPI_HANDLE obj);

  // Returns a handle which can be passed to ReleaseGlobalLock.
  virtual acpi::status<uint32_t> AcquireGlobalLock(uint16_t timeout) = 0;
  virtual acpi::status<> ReleaseGlobalLock(uint32_t handle) = 0;

  using AddressSpaceSetup = ACPI_ADR_SPACE_SETUP;
  using AddressSpaceHandler = ACPI_ADR_SPACE_HANDLER;
  virtual acpi::status<> InstallAddressSpaceHandler(ACPI_HANDLE object,
                                                    ACPI_ADR_SPACE_TYPE space_id,
                                                    AddressSpaceHandler handler,
                                                    AddressSpaceSetup setup, void* context) = 0;
  virtual acpi::status<> RemoveAddressSpaceHandler(ACPI_HANDLE object, ACPI_ADR_SPACE_TYPE space_id,
                                                   AddressSpaceHandler handler) = 0;

  // Initialise the ACPI subsystem.
  virtual acpi::status<> InitializeAcpi() = 0;

  // Set up the given GPE for wake.
  virtual acpi::status<> SetupGpeForWake(ACPI_HANDLE wake_dev, ACPI_HANDLE gpe_dev,
                                         uint32_t gpe_num) = 0;

  // These methods are called from InitialiseAcpi.
  // We define them in the common "acpi" base class so that we can
  // unit-test them against our mock ACPI implementation.

  // Switch interrupts to APIC mode, see ACPI v6.4 section 5.8.1.
  acpi::status<> SetApicIrqMode();

  // Walk _PRW methods, ensuring that ACPICA is aware of GPEs that can wake the system.
  acpi::status<> DiscoverWakeGpes();
};

}  // namespace acpi

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_ACPI_H_
