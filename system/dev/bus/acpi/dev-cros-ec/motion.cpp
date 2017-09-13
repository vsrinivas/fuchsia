// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <ddk/debug.h>

#include <chromiumos-platform-ec/ec_commands.h>
#include <zircon/device/input.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <stdlib.h>
#include <stdio.h>

#include <acpica/acpi.h>

#include "../errors.h"

AcpiCrOsEcMotionDevice::AcpiCrOsEcMotionDevice(fbl::RefPtr<AcpiCrOsEc> ec, zx_device_t* parent,
                                               ACPI_HANDLE acpi_handle)
    : DeviceType(parent), ec_(fbl::move(ec)), acpi_handle_(acpi_handle) {
}

AcpiCrOsEcMotionDevice::~AcpiCrOsEcMotionDevice() {
    AcpiRemoveNotifyHandler(acpi_handle_, ACPI_DEVICE_NOTIFY, NotifyHandler);
}

void AcpiCrOsEcMotionDevice::NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx) {
    auto dev = reinterpret_cast<AcpiCrOsEcMotionDevice*>(ctx);

    dprintf(TRACE, "acpi-cros-ec-motion: got event 0x%x\n", value);
    switch (value) {
    case 0x80:
        struct ec_response_motion_sensor_data data;
        zx_status_t status;
        while ((status = dev->FifoRead(&data)) == ZX_OK);
        // MKBP event
        break;
    }
}

zx_status_t AcpiCrOsEcMotionDevice::QueueHidReportLocked() {
    if (proxy_.is_valid()) {
        dprintf(TRACE, "acpi-cros-ec-motion: queueing report\n");
    }
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusQuery(uint32_t options, hid_info_t* info) {
    dprintf(TRACE, "acpi-cros-ec-motion: hid bus query\n");

    info->dev_num = 0;
    info->dev_class = HID_DEV_CLASS_OTHER;
    info->boot_device = false;
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusStart(ddk::HidBusIfcProxy proxy) {
    dprintf(TRACE, "acpi-cros-ec-motion: hid bus start\n");

    fbl::AutoLock guard(&hid_lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    }

    proxy_ = proxy;
    FifoInterruptEnable(true);

    return ZX_OK;
}

void AcpiCrOsEcMotionDevice::HidBusStop() {
    dprintf(TRACE, "acpi-cros-ec-motion: hid bus stop\n");

    fbl::AutoLock guard(&hid_lock_);

    proxy_.clear();
    FifoInterruptEnable(false);
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    dprintf(TRACE, "acpi-cros-ec-motion: hid bus get descriptor\n");

    if (data == nullptr || len == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ZX_ERR_NOT_FOUND;
    }

    *data = malloc(hid_descriptor_.size());
    if (*data == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    *len = hid_descriptor_.size();
    memcpy(*data, hid_descriptor_.get(), hid_descriptor_.size());
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                            size_t len) {
    if (rpt_type != HID_REPORT_TYPE_INPUT || rpt_id != 0) {
        return ZX_ERR_NOT_FOUND;
    }

    if (len < hid_report_len_) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    fbl::AutoLock guard(&hid_lock_);
    //uint8_t report = tablet_mode_;
    //static_assert(sizeof(report) == kHidReportLen, "");
    //memcpy(data, &report, kHidReportLen);

    // This API returns the length written
    return (zx_status_t)hid_report_len_;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                            size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AcpiCrOsEcMotionDevice::HidBusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}


void AcpiCrOsEcMotionDevice::DdkRelease() {
    dprintf(INFO, "acpi-cros-ec-motion: release\n");
    delete this;
}

zx_status_t AcpiCrOsEcMotionDevice::QueryNumSensors(uint8_t* count) {
    dprintf(TRACE, "acpi-cros-ec-motion: QueryNumSensors\n");
    struct ec_params_motion_sense cmd;
    struct ec_response_motion_sense rsp;
    cmd.cmd = MOTIONSENSE_CMD_DUMP;
    cmd.dump.max_sensor_count = 0; // We only care about the number of sensors.

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp.dump, sizeof(rsp.dump), &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != sizeof(rsp.dump)) {
        return ZX_ERR_IO;
    }

    *count = rsp.dump.sensor_count;
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::QuerySensorInfo(uint8_t sensor_num, SensorInfo* info) {
    dprintf(TRACE, "acpi-cros-ec-motion: QuerySensorInfo %d\n", sensor_num);

    struct ec_params_motion_sense cmd;
    struct ec_response_motion_sense rsp;
    cmd.cmd = MOTIONSENSE_CMD_INFO;
    cmd.info_3.sensor_num = sensor_num;

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp.info_3, sizeof(rsp.info_3), &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != sizeof(rsp.info_3)) {
        return ZX_ERR_IO;
    }

    if (rsp.info_3.type >= MOTIONSENSE_TYPE_MAX || rsp.info_3.location >= MOTIONSENSE_LOC_MAX) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    info->type = static_cast<motionsensor_type>(rsp.info_3.type);
    info->loc = static_cast<motionsensor_location>(rsp.info_3.location);
    info->min_sampling_freq = rsp.info_3.min_frequency;
    info->max_sampling_freq = rsp.info_3.max_frequency;
    info->fifo_max_event_count = rsp.info_3.fifo_max_event_count;
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::FifoInterruptEnable(bool enable) {
    dprintf(TRACE, "acpi-cros-ec-motion: FifoInterruptEnable %d\n", enable);

    struct ec_params_motion_sense cmd;
    struct ec_response_motion_sense rsp;
    cmd.cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE;
    cmd.fifo_int_enable.enable = enable;

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp.fifo_int_enable, sizeof(rsp.fifo_int_enable),
                                           &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != sizeof(rsp.fifo_int_enable)) {
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::SetSensorOutputDataRate(uint8_t sensor_num,
                                                            uint32_t freq_millihertz) {
    dprintf(TRACE, "acpi-cros-ec-motion: SetSensorOutputDataRate %d %u\n", sensor_num,
            freq_millihertz);

    struct ec_params_motion_sense cmd;
    struct ec_response_motion_sense rsp;
    cmd.cmd = MOTIONSENSE_CMD_SENSOR_ODR;
    cmd.sensor_odr.sensor_num = sensor_num;
    cmd.sensor_odr.roundup = 0;
    cmd.sensor_odr.data = static_cast<int32_t>(freq_millihertz);

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp.sensor_odr, sizeof(rsp.sensor_odr),
                                           &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != sizeof(rsp.sensor_odr)) {
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::SetEcSamplingRate(uint8_t sensor_num, uint32_t milliseconds) {
    dprintf(TRACE, "acpi-cros-ec-motion: SetEcSamplingRate %d %u\n", sensor_num, milliseconds);

    struct ec_params_motion_sense cmd;
    struct ec_response_motion_sense rsp;
    cmd.cmd = MOTIONSENSE_CMD_EC_RATE;
    cmd.ec_rate.sensor_num = sensor_num;
    cmd.ec_rate.roundup = 0;
    cmd.ec_rate.data = static_cast<int32_t>(milliseconds);

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp.ec_rate, sizeof(rsp.ec_rate),
                                           &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != sizeof(rsp.ec_rate)) {
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::GetSensorRange(uint8_t sensor_num, int32_t* range) {
    dprintf(TRACE, "acpi-cros-ec-motion: GetSensorRange %d\n", sensor_num);

    struct ec_params_motion_sense cmd;
    struct ec_response_motion_sense rsp;
    cmd.cmd = MOTIONSENSE_CMD_SENSOR_RANGE;
    cmd.sensor_range.sensor_num = sensor_num;
    cmd.sensor_range.roundup = 0;
    cmd.sensor_range.data = EC_MOTION_SENSE_NO_VALUE;

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp.sensor_range, sizeof(rsp.sensor_range),
                                           &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != sizeof(rsp.sensor_range)) {
        return ZX_ERR_IO;
    }

    *range = rsp.sensor_range.ret;
    dprintf(SPEW, "acpi-cros-ec-motion: sensor range %d: %d\n", sensor_num, *range);
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::FifoRead(struct ec_response_motion_sensor_data* data) {
    dprintf(TRACE, "acpi-cros-ec-motion: FifoRead\n");

    struct ec_params_motion_sense cmd;
    struct __packed {
        uint32_t count;
        struct ec_response_motion_sensor_data data;
    } rsp;
    cmd.cmd = MOTIONSENSE_CMD_FIFO_READ;
    cmd.fifo_read.max_data_vector = 1;

    size_t actual;
    zx_status_t status = ec_->IssueCommand(EC_CMD_MOTION_SENSE_CMD, 3, &cmd, sizeof(cmd),
                                           &rsp, sizeof(rsp), &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual < sizeof(uint32_t)) {
        return ZX_ERR_IO;
    }
    if (rsp.count != 1) {
        dprintf(TRACE, "acpi-cros-ec-motion: FifoRead found no reports\n");
        return ZX_ERR_SHOULD_WAIT;
    }
    if (actual != sizeof(rsp)) {
        return ZX_ERR_IO;
    }

    dprintf(TRACE, "acpi-cros-ec-motion: FifoRead received report\n");
    dprintf(SPEW, "acpi-cros-ec-motion: sensor=%u flags=%#x val=(%d, %d, %d)\n",
            rsp.data.sensor_num, rsp.data.flags, rsp.data.data[0], rsp.data.data[1], rsp.data.data[2]);
    memcpy(data, &rsp.data, sizeof(*data));
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::Create(fbl::RefPtr<AcpiCrOsEc> ec, zx_device_t* parent,
                                           ACPI_HANDLE acpi_handle,
                                           fbl::unique_ptr<AcpiCrOsEcMotionDevice>* out) {

    if (!ec->supports_motion_sense() || !ec->supports_motion_sense_fifo()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<AcpiCrOsEcMotionDevice> dev(
            new (&ac) AcpiCrOsEcMotionDevice(fbl::move(ec), parent, acpi_handle));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = dev->ProbeSensors();
    if (status != ZX_OK) {
        return status;
    }

    status = dev->BuildHidDescriptor();
    if (status != ZX_OK) {
        dprintf(ERROR, "acpi-cros-ec-motion: failed to construct hid desc: %d\n", status);
        return status;
    }

    // Install acpi event handler
    ACPI_STATUS acpi_status = AcpiInstallNotifyHandler(acpi_handle, ACPI_DEVICE_NOTIFY,
                                                       NotifyHandler, dev.get());
    if (acpi_status != AE_OK) {
        dprintf(ERROR, "acpi-cros-ec-motion: could not install notify handler\n");
        return acpi_to_zx_status(acpi_status);
    }

    *out = fbl::move(dev);
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::ProbeSensors() {
    uint8_t num_sensors;
    zx_status_t status = QueryNumSensors(&num_sensors);
    if (status != ZX_OK) {
        dprintf(ERROR, "acpi-cros-ec-motion: num sensors query failed: %d\n", status);
        return status;
    }
    dprintf(TRACE, "acpi-cros-ec-motion: found %u sensors\n", num_sensors);

    fbl::AllocChecker ac;
    sensors_.reserve(num_sensors, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (uint8_t i = 0; i < num_sensors; ++i) {
        SensorInfo info;
        status = QuerySensorInfo(i, &info);
        if (status != ZX_OK) {
            dprintf(ERROR, "acpi-cros-ec-motion: sensor info query %u failed: %d\n", i, status);
            info.valid = false;
            sensors_.push_back(info);
            continue;
        }

        status = GetSensorRange(i, &info.range);
        if (status != ZX_OK) {
            info.range = -1;
            dprintf(ERROR, "acpi-cros-ec-motion: sensor range query %u failed: %d\n", i, status);
        }

        dprintf(SPEW,
                "acpi-cros-ec-motion: sensor %d: type=%u loc=%u freq=[%u,%u] evt_count=%u\n",
                i, info.type, info.loc, info.min_sampling_freq, info.max_sampling_freq,
                info.fifo_max_event_count);

        info.valid = true;
        sensors_.push_back(info);
    }
    return ZX_OK;
}

zx_status_t AcpiCrOsEcMotionDevice::BuildHidDescriptor() {
    hid_descriptor_.reset();
    return ZX_OK;
}
