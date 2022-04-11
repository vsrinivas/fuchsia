// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/acpi.h"

#include <lib/ddk/debug.h>

#include <vector>

namespace acpi {
namespace {

constexpr const char kGpeHidString[] = "ACPI0006";

// Returns true if the device described by |info| is a GPE device.
bool IsGpeDevice(acpi::UniquePtr<ACPI_DEVICE_INFO> info) {
  // These length fields count the trailing NUL.
  if ((info->Valid & ACPI_VALID_HID) && info->HardwareId.Length == sizeof(kGpeHidString)) {
    if (!strcmp(info->HardwareId.String, kGpeHidString)) {
      return true;
    }
  }
  if (info->Valid & ACPI_VALID_CID) {
    for (size_t i = 0; i < info->CompatibleIdList.Count; i++) {
      ACPI_PNP_DEVICE_ID* id = &info->CompatibleIdList.Ids[i];
      if (!strncmp(id->String, kGpeHidString, sizeof(kGpeHidString))) {
        return true;
      }
    }
  }
  return false;
}
}  // namespace

acpi::status<uint8_t> Acpi::CallBbn(ACPI_HANDLE obj) {
  auto ret = EvaluateObject(obj, "_BBN", std::nullopt);
  if (ret.is_error()) {
    return ret.take_error();
  }

  if (ret->Type != ACPI_TYPE_INTEGER) {
    return acpi::error(AE_TYPE);
  }

  if (ret->Integer.Value > std::numeric_limits<uint8_t>::max()) {
    return acpi::error(AE_BAD_VALUE);
  }

  return acpi::ok(static_cast<uint8_t>(ret->Integer.Value));
}

acpi::status<uint16_t> Acpi::CallSeg(ACPI_HANDLE obj) {
  auto ret = EvaluateObject(obj, "SEG", std::nullopt);

  if (ret.is_error()) {
    return ret.take_error();
  }

  if (ret->Type != ACPI_TYPE_INTEGER) {
    return acpi::error(AE_TYPE);
  }

  // Lower 8 bits of _SEG returned integer is the PCI segment group.
  return acpi::ok(static_cast<uint16_t>(ret->Integer.Value & 0xff));
}

acpi::status<> Acpi::SetApicIrqMode() {
  ACPI_OBJECT selector = acpi::MakeAcpiObject(1);

  auto result = EvaluateObject(nullptr, "\\_PIC", std::vector({selector}));
  if (result.is_error()) {
    return result.take_error();
  }
  return acpi::ok();
}

acpi::status<> Acpi::DiscoverWakeGpes() {
  return WalkNamespace(
      ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, UINT32_MAX,
      [this](ACPI_HANDLE obj, uint32_t depth, acpi::WalkDirection dir) {
        if (dir == acpi::WalkDirection::Ascending) {
          return acpi::ok();
        }

        auto status = EvaluateObject(obj, "_PRW", std::nullopt);
        if (status.is_error()) {
          return acpi::ok();
        }
        acpi::UniquePtr<ACPI_OBJECT> prw_res = std::move(status.value());

        // _PRW returns a package with >= 2 entries. The first entry indicates what
        // type of event it is. If it's a GPE event, the first entry is either an
        // integer indicating the bit within the FADT GPE enable register or it is
        // a package containing a handle to a GPE block device and the bit index on
        // that device. There are other event types with (handle, int) packages, so
        // check that the handle is a GPE device by checking against the CID/HID
        // required by the ACPI spec.
        if (prw_res->Type != ACPI_TYPE_PACKAGE || prw_res->Package.Count < 2) {
          return acpi::ok();  // Keep walking the tree
        }

        ACPI_HANDLE gpe_block;
        uint32_t gpe_bit;
        ACPI_OBJECT* event_info = &prw_res->Package.Elements[0];
        if (event_info->Type == ACPI_TYPE_INTEGER) {
          gpe_block = nullptr;
          gpe_bit = static_cast<UINT32>(prw_res->Package.Elements[0].Integer.Value);
        } else if (event_info->Type == ACPI_TYPE_PACKAGE) {
          if (event_info->Package.Count != 2) {
            return acpi::ok();
          }
          ACPI_OBJECT* handle_obj = &event_info->Package.Elements[0];
          ACPI_OBJECT* gpe_num_obj = &event_info->Package.Elements[1];
          if (handle_obj->Type != ACPI_TYPE_LOCAL_REFERENCE) {
            return acpi::ok();
          }
          auto info = GetObjectInfo(handle_obj->Reference.Handle);
          if (info.is_error() || !IsGpeDevice(std::move(info.value()))) {
            return acpi::ok();
          }
          if (gpe_num_obj->Type != ACPI_TYPE_INTEGER) {
            return acpi::ok();
          }
          gpe_block = handle_obj->Reference.Handle;
          gpe_bit = static_cast<UINT32>(gpe_num_obj->Integer.Value);
        } else {
          return acpi::ok();
        }
        auto wake_status = SetupGpeForWake(obj, gpe_block, gpe_bit);
        if (wake_status.is_error()) {
          zxlogf(ERROR, "ACPI failed to setup wake GPE: %d", wake_status.status_value());
        }
        return acpi::ok();
      });
  return acpi::ok();
}
}  // namespace acpi
