// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <stdio.h>
#include <zircon/hw/i2c.h>

#include <algorithm>
#include <vector>

#include <acpica/acpi.h>
#include <lib/ddk/metadata.h>

#include "acpi-private.h"
#include "dev.h"
#include "util.h"

bool AddDeviveProperty(acpi_i2c_device_t& dev, uint16_t vid, uint16_t did,
                       const ACPI_DEVICE_INFO& info, const ACPI_DEVICE_INFO& i2c_bus_info) {
  if (dev.propcount + 2 > std::size(dev.props)) {
    zxlogf(WARNING,
           "Insufficient space to store I2C class in devprops for ACPI I2C device "
           "\"%s\" on bus \"%s\"\n",
           fourcc_to_string(info.Name).str, fourcc_to_string(i2c_bus_info.Name).str);
    return false;
  }

  dev.props[dev.propcount].id = BIND_I2C_VID;
  dev.props[dev.propcount++].value = vid;
  dev.props[dev.propcount].id = BIND_I2C_DID;
  dev.props[dev.propcount++].value = did;
  return true;
}

zx_status_t I2cBusPublishMetadata(zx_device_t* dev, uint8_t pci_bus_num, uint64_t adr,
                                  const ACPI_DEVICE_INFO& i2c_bus_info,
                                  ACPI_HANDLE i2c_bus_object) {
  std::vector<acpi_i2c_device_t> found_devices;

  // Enumerate the device children who are direct descendants of the bus object.
  acpi::WalkNamespace(
      ACPI_TYPE_DEVICE, i2c_bus_object, 1,
      [&found_devices, &i2c_bus_info](ACPI_HANDLE object, uint32_t level,
                                      acpi::WalkDirection dir) -> ACPI_STATUS {
        // We only have work to do when descending into a node.  Skip the node when
        // we are ascending back through it.
        if (dir == acpi::WalkDirection::Ascending) {
          return AE_OK;
        }

        // Attempt to fetch the info for this device.  Don't stop enumerating if
        // we fail for this device, simply log a warning and keep going.
        acpi::UniquePtr<ACPI_DEVICE_INFO> info;
        if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
          zxlogf(WARNING, "Failed to fetch object info for device on I2C bus \"%s\"",
                 fourcc_to_string(i2c_bus_info.Name).str);
          return AE_OK;
        } else {
          info = std::move(res.value());
        }

        acpi_i2c_device_t new_dev;
        new_dev.protocol_id = ZX_PROTOCOL_I2C;

        // Extract the vendor specific HW ID into our devprops if it is present.
        zx_status_t status = acpi::ExtractHidToDevProps(*info, new_dev.props, new_dev.propcount);
        if (status != ZX_OK) {
          zxlogf(WARNING,
                 "Failed to extract HID info ACPI I2C device \"%s\" on bus \"%s\" (status %d)\n",
                 fourcc_to_string(info->Name).str, fourcc_to_string(i2c_bus_info.Name).str, status);
          return AE_OK;
        }

        // If we have a CID, and it matches the I2C HID CID, then add the
        // I2C HID class to our devprops if we can.
        //
        // TODO(fxbug.dev/56832): This is a very focused hack to support binding to I2C
        // HID Touch controllers on some older hardware.  Once the bug has been
        // resolved, driver clients will be able to access their relevant ACPI info
        // on their own and all of this can go away.
        if ((info->Valid & ACPI_VALID_CID) && (info->CompatibleIdList.Count > 0) &&
            (info->CompatibleIdList.Ids[0].Length > 0)) {
          if (!strcmp(info->CompatibleIdList.Ids[0].String, I2C_HID_CID_STRING)) {
            if (new_dev.propcount >= std::size(new_dev.props)) {
              zxlogf(
                  WARNING,
                  "Insufficient space to store I2C class in devprops for ACPI I2C device \"%s\" on "
                  "bus \"%s\"\n",
                  fourcc_to_string(info->Name).str, fourcc_to_string(i2c_bus_info.Name).str);
              return AE_OK;
            }

            new_dev.props[new_dev.propcount].id = BIND_I2C_CLASS;
            new_dev.props[new_dev.propcount++].value = I2C_CLASS_HID;
          }

          // Extract the first compatible ID into our devprops if it is present.
          status = acpi::ExtractCidToDevProps(*info, new_dev.props, new_dev.propcount);
          if (status != ZX_OK) {
            zxlogf(WARNING,
                   "Failed to extract CID info ACPI I2C device \"%s\" on bus \"%s\" (status %d)\n",
                   fourcc_to_string(info->Name).str, fourcc_to_string(i2c_bus_info.Name).str,
                   status);
            return AE_OK;
          }
        }

        // Invoke the "Current Resource Settings" method (_CRS) on our device object
        // and go looking for the resource which describes the I2C specific details
        // of this device (its address, expected speed, and so on).
        ACPI_STATUS acpi_status;
        acpi_status = acpi::WalkResources(
            object, "_CRS",
            [&new_dev, &info, &i2c_bus_info](ACPI_RESOURCE* resource) -> ACPI_STATUS {
              if (resource->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
                return AE_NOT_FOUND;
              }
              if (resource->Data.I2cSerialBus.Type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
                return AE_NOT_FOUND;
              }

              ACPI_RESOURCE_I2C_SERIALBUS* i2c = &resource->Data.I2cSerialBus;
              new_dev.is_bus_controller = i2c->SlaveMode;
              new_dev.ten_bit = i2c->AccessMode;
              new_dev.address = i2c->SlaveAddress;
              new_dev.bus_speed = i2c->ConnectionSpeed;

              // TODO(fxbug.dev/56832): This is a very focused hack to support binding to I2C
              // on specific hardware.  Once the bug has been resolved, driver clients will be able
              // to access their relevant ACPI info on their own and all of this can go away.
              if (!strcmp(info->HardwareId.String, ALC5663_HID_STRING)) {
                if (!AddDeviveProperty(new_dev, PDEV_VID_REALTEK, PDEV_DID_ALC5663, *info,
                                       i2c_bus_info)) {
                  return AE_OK;
                }
              } else if (!strcmp(info->HardwareId.String, ALC5514_HID_STRING)) {
                if (!AddDeviveProperty(new_dev, PDEV_VID_REALTEK, PDEV_DID_ALC5514, *info,
                                       i2c_bus_info)) {
                  return AE_OK;
                }
              } else if (!strcmp(info->HardwareId.String, MAX98927_HID_STRING)) {
                if (!AddDeviveProperty(new_dev, PDEV_VID_MAXIM, PDEV_DID_MAXIM_MAX98927, *info,
                                       i2c_bus_info)) {
                  return AE_OK;
                }
              }
              return AE_CTRL_TERMINATE;
            });

        if (acpi_status != AE_OK) {
          zxlogf(
              WARNING,
              "Failed to find ACPI CRS for I2C device \"%s\" on bus \"%s\" (status %d).  Skipping "
              "device.\n",
              fourcc_to_string(info->Name).str, fourcc_to_string(i2c_bus_info.Name).str,
              acpi_status);
          return AE_OK;
        }

        // Looks like we got all of the info we were looking for.  Go ahead and add
        // this device to our list.
        found_devices.emplace_back(new_dev);
        return AE_OK;
      });

  // If we didn't find any devices, then we are done.
  if (found_devices.empty()) {
    return ZX_ERR_NOT_FOUND;
  }

  // Publish the I2C device details as metadata on the future PCI device node.
  // The canonical path to the PCI device is /dev/sys/pci/<b:d.f>
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/dev/sys/pci/%02x:%02x.%01x", static_cast<uint32_t>(pci_bus_num),
           static_cast<uint32_t>((adr >> 16) & 0xFFFF), static_cast<uint32_t>(adr & 0xFFFF));

  zx_status_t status =
      device_publish_metadata(dev, path, DEVICE_METADATA_ACPI_I2C_DEVICES, found_devices.data(),
                              found_devices.size() * sizeof(decltype(found_devices)::value_type));

  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to publish \"%s\" metadata (res %d)",
           fourcc_to_string(i2c_bus_info.Name).str, status);
  } else {
    zxlogf(INFO, "acpi: Published I2C metadata for %zu device%s on bus %s to \"%s\"",
           found_devices.size(), found_devices.size() == 1 ? "" : "s",
           fourcc_to_string(i2c_bus_info.Name).str, path);
  }

  return status;
}
