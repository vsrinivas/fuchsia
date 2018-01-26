// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>

#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <zircon/device/input.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <stdlib.h>
#include <stdio.h>

#include <acpica/acpi.h>

#include "errors.h"

class AcpiTbmcDevice;
using DeviceType = ddk::Device<AcpiTbmcDevice>;

// An instance of a GOOG0006 Tablet Motion Control device.  It presents a HID
// interface with a single input, the state of the tablet mode switch.
class AcpiTbmcDevice : public DeviceType,
                       public ddk::HidBusProtocol<AcpiTbmcDevice> {
public:
    static zx_status_t Create(zx_device_t* parent, ACPI_HANDLE acpi_handle,
                              fbl::unique_ptr<AcpiTbmcDevice>* out);

    // hidbus protocol implementation
    zx_status_t HidBusQuery(uint32_t options, hid_info_t* info);
    zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy);
    void HidBusStop();
    zx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                size_t* out_len);
    zx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len);
    zx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidBusGetProtocol(uint8_t* protocol);
    zx_status_t HidBusSetProtocol(uint8_t protocol);

    void DdkRelease();
    ~AcpiTbmcDevice();
private:
    AcpiTbmcDevice(zx_device_t* parent, ACPI_HANDLE acpi_handle);
    DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiTbmcDevice);

    zx_status_t CallTbmcMethod();
    static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

    zx_status_t QueueHidReportLocked();

    const ACPI_HANDLE acpi_handle_;

    fbl::Mutex lock_;

    // Current state of the tablet mode switch
    bool tablet_mode_ = false;

    // Interface the driver is currently bound to
    ddk::HidBusIfcProxy proxy_;

    static const uint8_t kHidDescriptor[];
    static const size_t kHidDescriptorLen;
    static constexpr size_t kHidReportLen = 1;
};

// We encode the tablet mode switch events as a vendor-defined System Control.
// This is a bit hacky, but there is no tablet mode switch usage switch defined
// that we can find.  System Control collections are meant to be consumed by the
// operating system, not user applications.
const uint8_t AcpiTbmcDevice::kHidDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x80,        // Usage (Sys Control)
    0xA1, 0x01,        // Collection (Application)
    0x0B, 0x01, 0x00, 0x00, 0xFF,  //   Usage (0x0-FFFFFF) [Vendor Defined]
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x07,        //   Report Size (7)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
};

const size_t AcpiTbmcDevice::kHidDescriptorLen = sizeof(AcpiTbmcDevice::kHidDescriptor);

AcpiTbmcDevice::AcpiTbmcDevice(zx_device_t* parent, ACPI_HANDLE acpi_handle)
    : DeviceType(parent), acpi_handle_(acpi_handle) {
}

AcpiTbmcDevice::~AcpiTbmcDevice() {
    AcpiRemoveNotifyHandler(acpi_handle_, ACPI_DEVICE_NOTIFY, NotifyHandler);
}

zx_status_t AcpiTbmcDevice::CallTbmcMethod() {
    ACPI_OBJECT obj = { };
    ACPI_BUFFER buffer = {
        .Length = sizeof(obj),
        .Pointer = &obj,
    };
    ACPI_STATUS acpi_status = AcpiEvaluateObjectTyped(acpi_handle_, const_cast<char*>("TBMC"),
                                                      nullptr, &buffer, ACPI_TYPE_INTEGER);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-tbmc: TBMC failed: %d\n", acpi_status);
        return acpi_to_zx_status(acpi_status);
    }

    zxlogf(TRACE, "acpi-tbmc: TMBC returned 0x%llx\n", obj.Integer.Value);

    fbl::AutoLock guard(&lock_);

    bool old_mode = tablet_mode_;
    tablet_mode_ = obj.Integer.Value;
    if (tablet_mode_ != old_mode) {
        zx_status_t status = QueueHidReportLocked();
        if (status != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

void AcpiTbmcDevice::NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    auto dev = reinterpret_cast<AcpiTbmcDevice*>(ctx);

    zxlogf(TRACE, "acpi-tbmc: got event 0x%x\n", value);
    switch (value) {
    case 0x80:
        // Tablet mode has changed
        dev->CallTbmcMethod();
        break;
    }
}

zx_status_t AcpiTbmcDevice::QueueHidReportLocked() {
    if (proxy_.is_valid()) {
        zxlogf(TRACE, "acpi-tbmc:  queueing report\n");
        uint8_t report = tablet_mode_;
        proxy_.IoQueue(&report, sizeof(report));
    }
    return ZX_OK;
}

zx_status_t AcpiTbmcDevice::HidBusQuery(uint32_t options, hid_info_t* info) {
    zxlogf(TRACE, "acpi-tbmc: hid bus query\n");

    info->dev_num = 0;
    info->dev_class = HID_DEV_CLASS_OTHER;
    info->boot_device = false;
    return ZX_OK;
}

zx_status_t AcpiTbmcDevice::HidBusStart(ddk::HidBusIfcProxy proxy) {
    zxlogf(TRACE, "acpi-tbmc: hid bus start\n");

    fbl::AutoLock guard(&lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    proxy_ = proxy;
    return ZX_OK;
}

void AcpiTbmcDevice::HidBusStop() {
    zxlogf(TRACE, "acpi-tbmc: hid bus stop\n");

    fbl::AutoLock guard(&lock_);
    proxy_.clear();
}

zx_status_t AcpiTbmcDevice::HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    zxlogf(TRACE, "acpi-tbmc: hid bus get descriptor\n");

    if (data == nullptr || len == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ZX_ERR_NOT_FOUND;
    }

    *data = malloc(kHidDescriptorLen);
    if (*data == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    *len = kHidDescriptorLen;
    memcpy(*data, kHidDescriptor, kHidDescriptorLen);
    return ZX_OK;
}

zx_status_t AcpiTbmcDevice::HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                            size_t len, size_t* out_len) {
    if (out_len == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (rpt_type != HID_REPORT_TYPE_INPUT || rpt_id != 0) {
        return ZX_ERR_NOT_FOUND;
    }

    if (len < kHidReportLen) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    fbl::AutoLock guard(&lock_);
    uint8_t report = tablet_mode_;
    static_assert(sizeof(report) == kHidReportLen, "");
    memcpy(data, &report, kHidReportLen);

    *out_len = kHidReportLen;
    return ZX_OK;
}

zx_status_t AcpiTbmcDevice::HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                            size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiTbmcDevice::HidBusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiTbmcDevice::HidBusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_OK;
}

zx_status_t AcpiTbmcDevice::HidBusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiTbmcDevice::HidBusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}


void AcpiTbmcDevice::DdkRelease() {
    zxlogf(INFO, "acpi-tbmc: release\n");
    delete this;
}

zx_status_t AcpiTbmcDevice::Create(zx_device_t* parent, ACPI_HANDLE acpi_handle,
                                   fbl::unique_ptr<AcpiTbmcDevice>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<AcpiTbmcDevice> dev(new (&ac) AcpiTbmcDevice(parent, acpi_handle));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize tracked state
    dev->CallTbmcMethod();

    // Install acpi event handler
    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
                                                       NotifyHandler, dev.get());
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi-tbmc: could not install notify handler\n");
        return acpi_to_zx_status(acpi_status);
    }

    *out = fbl::move(dev);
    return ZX_OK;
}

zx_status_t tbmc_init(zx_device_t* parent, ACPI_HANDLE acpi_handle) {
    zxlogf(TRACE, "acpi-tbmc: init\n");

    fbl::unique_ptr<AcpiTbmcDevice> dev;
    zx_status_t status = AcpiTbmcDevice::Create(parent, acpi_handle, &dev);
    if (status != ZX_OK) {
        return status;
    }

    status = dev->DdkAdd("acpi-tbmc");
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();

    zxlogf(INFO, "acpi-tbmc: initialized\n");
    return ZX_OK;
}
