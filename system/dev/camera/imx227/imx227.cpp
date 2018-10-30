// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx227.h"
#include "imx227-seq.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <ddk/protocol/i2c-lib.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/device/camera.h>
#include <zircon/types.h>

namespace camera {

namespace {

constexpr uint16_t kSensorId = 0x0227;
constexpr uint32_t kAGainPrecision = 12;
constexpr uint32_t kDGainPrecision = 8;
constexpr int32_t kLog2GainShift = 18;
constexpr int32_t kSensorExpNumber = 1;
constexpr uint32_t kMasterClock = 288000000;

} // namespace

zx_status_t Imx227Device::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    for (uint32_t i = 0; i < countof(gpios_); i++) {
        size_t actual;
        status = pdev_get_protocol(&pdev_, ZX_PROTOCOL_GPIO, i, &gpios_[i], sizeof(gpios_[i]),
                                   &actual);
        if (status != ZX_OK) {
            return status;
        }
        // Set the GPIO to output and set initial value to 0.
        ddk::GpioProtocolProxy gpio(&gpios_[i]);
        gpio.ConfigOut(0);
    }

    // I2c for communicating with the sensor.
    status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c_);
    if (status != ZX_OK) {
        return status;
    }

    // Clk for gating clocks for sensor.
    status = device_get_protocol(parent, ZX_PROTOCOL_CLK, &clk_);
    if (status != ZX_OK) {
        return status;
    }

    // Mipi for init and de-init.
    status = device_get_protocol(parent, ZX_PROTOCOL_MIPI_CSI, &mipi_);
    if (status != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

uint8_t Imx227Device::ReadReg(uint16_t addr) {
    // Convert the address to Big Endian format.
    // The camera sensor expects in this format.
    uint16_t buf = htobe16(addr);
    uint8_t val = 0;
    zx_status_t status = i2c_write_read_sync(&i2c_, &buf, sizeof(buf), &val, sizeof(val));
    if (status != ZX_OK) {
        zxlogf(ERROR, "Imx227Device: could not read reg addr: 0x%08x  status: %d\n", addr, status);
        return -1;
    }
    return val;
}

void Imx227Device::WriteReg(uint16_t addr, uint8_t val) {
    // Convert the address to Big Endian format.
    // The camera sensor expects in this format.
    // First two bytes are the address, third one is the value to be written.
    uint8_t buf[3];
    buf[1] = static_cast<uint8_t>(addr & 0xFF);
    buf[0] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    buf[2] = val;

    zx_status_t status = i2c_write_sync(&i2c_, buf, 3);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Imx227Device: could not write reg addr/val: 0x%08x/0x%08x status: %d\n",
               addr, val, status);
    }
}

bool Imx227Device::ValidateSensorID() {
    uint16_t sensor_id = static_cast<uint16_t>((ReadReg(0x0016) << 8) | ReadReg(0x0017));
    if (sensor_id != kSensorId) {
        zxlogf(ERROR, "Imx227Device: Invalid sensor ID\n");
        return false;
    }
    return true;
}

zx_status_t Imx227Device::InitSensor(uint8_t idx) {
    if (idx >= countof(kSEQUENCE_TABLE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    const init_seq_fmt_t* sequence = kSEQUENCE_TABLE[idx];
    bool init_command = true;

    while (init_command) {
        uint16_t address = sequence->address;
        uint8_t value = sequence->value;

        switch (address) {
        case 0x0000: {
            if (sequence->value == 0 && sequence->len == 0) {
                init_command = false;
            } else {
                WriteReg(address, value);
            }
            break;
        }
        default:
            WriteReg(address, value);
            break;
        }
        sequence++;
    }
    return ZX_OK;
}

zx_status_t Imx227Device::Init() {

    // Power up sequence. Reference: Page 51- IMX227-0AQH5-C datasheet.
    ddk::GpioProtocolProxy gpio_vana(&gpios_[VANA_ENABLE]);
    gpio_vana.ConfigOut(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    ddk::GpioProtocolProxy gpio_vdig(&gpios_[VDIG_ENABLE]);
    gpio_vdig.ConfigOut(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    // Enable 24M clock for sensor.
    ddk::ClkProtocolProxy clk(&clk_);
    clk.Enable(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    ddk::GpioProtocolProxy gpio_rst(&gpios_[CAM_SENSOR_RST]);
    gpio_rst.ConfigOut(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    // Get Sensor ID to validate initialization sequence.
    if (!ValidateSensorID()) {
        return ZX_ERR_INTERNAL;
    }

    // Initialize Sensor Context.
    ctx_.seq_width = 1;
    ctx_.streaming_flag = 0;
    ctx_.again_old = 0;
    ctx_.change_flag = 0;
    ctx_.again_limit = 8 << kAGainPrecision;
    ctx_.dgain_limit = 15 << kDGainPrecision;

    // Initialize Sensor Parameters.
    ctx_.param.again_accuracy = 1 << kLog2GainShift;
    ctx_.param.sensor_exp_number = kSensorExpNumber;
    ctx_.param.again_log2_max = 3 << kLog2GainShift;
    ctx_.param.dgain_log2_max = 3 << kLog2GainShift;
    ctx_.param.integration_time_apply_delay = 2;
    ctx_.param.isp_exposure_channel_delay = 0;

    return ZX_OK;
}

void Imx227Device::DeInit() {
    ddk::MipiCsiProtocolProxy mipi(&mipi_);
    mipi.DeInit();
}

zx_status_t Imx227Device::GetInfo(zircon_camera_SensorInfo* out_info) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::SetMode(uint8_t mode) {
    // Get Sensor ID to see if sensor is initialized.
    if (!ValidateSensorID()) {
        return ZX_ERR_INTERNAL;
    }

    if (mode >= countof(supported_modes)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (supported_modes[mode].wdr_mode) {
    case kWDR_MODE_LINEAR: {

        InitSensor(supported_modes[mode].idx);

        ctx_.again_delay = 0;
        ctx_.dgain_delay = 0;
        ctx_.param.integration_time_apply_delay = 2;
        ctx_.param.isp_exposure_channel_delay = 0;
        ctx_.hdr_flag = 0;
        break;
    }
    // TODO(braval) : Support other modes.
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    ctx_.param.active.width = supported_modes[mode].resolution.width;
    ctx_.param.active.height = supported_modes[mode].resolution.height;
    ctx_.HMAX = static_cast<uint16_t>(ReadReg(0x342) << 8 | ReadReg(0x343));
    ctx_.VMAX = static_cast<uint16_t>(ReadReg(0x340) << 8 | ReadReg(0x341));
    ctx_.int_max = 0x0A8C; // Max allowed for 30fps = 2782 (dec), 0x0A8E (hex)
    ctx_.int_time_min = 1;
    ctx_.int_time_limit = ctx_.int_max;
    ctx_.param.total.height = ctx_.VMAX;
    ctx_.param.total.width = ctx_.HMAX;
    ctx_.param.pixels_per_line = ctx_.param.total.width;

    uint32_t master_clock = kMasterClock;
    ctx_.param.lines_per_second = master_clock / ctx_.HMAX;

    ctx_.param.integration_time_min = ctx_.int_time_min;
    ctx_.param.integration_time_limit = ctx_.int_time_limit;
    ctx_.param.integration_time_max = ctx_.int_time_limit;
    ctx_.param.integration_time_long_max = ctx_.int_time_limit;
    ctx_.param.mode = mode;
    ctx_.param.bayer = supported_modes[mode].bayer;
    ctx_.wdr_mode = supported_modes[mode].wdr_mode;

    ddk::MipiCsiProtocolProxy mipi(&mipi_);
    mipi_info_t mipi_info;
    mipi_adap_info_t adap_info;

    mipi_info.lanes = supported_modes[mode].lanes;
    mipi_info.ui_value = 1000 / supported_modes[mode].mbps;
    if ((1000 % supported_modes[mode].mbps) != 0) {
        mipi_info.ui_value += 1;
    }

    switch (supported_modes[mode].bits) {
    case 10:
        adap_info.format = IMAGE_FORMAT_AM_RAW10;
        break;
    case 12:
        adap_info.format = IMAGE_FORMAT_AM_RAW12;
        break;
    default:
        adap_info.format = IMAGE_FORMAT_AM_RAW10;
        break;
    }

    adap_info.resolution.width = supported_modes[mode].resolution.width;
    adap_info.resolution.height = supported_modes[mode].resolution.height;
    adap_info.path = MIPI_PATH_PATH0;
    adap_info.mode = MIPI_MODES_DDR_MODE;
    return mipi.Init(&mipi_info, &adap_info);
}

void Imx227Device::StartStreaming() {
    ctx_.streaming_flag = 1;
    WriteReg(0x0100, 0x01);
}

void Imx227Device::StopStreaming() {
    ctx_.streaming_flag = 0;
    WriteReg(0x0100, 0x00);
}

int32_t Imx227Device::SetAnalogGain(int32_t gain) {
    return ZX_ERR_NOT_SUPPORTED;
}

int32_t Imx227Device::SetDigitalGain(int32_t gain) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Imx227Device::SetIntegrationTime(int32_t int_time,
                                      int32_t int_time_M,
                                      int32_t int_time_L) {
}

void Imx227Device::Update() {
}

zx_status_t Imx227Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case CAMERA_IOCTL_GET_SUPPORTED_MODES: {
        if (out_len < sizeof(zircon_camera_SensorMode) * MAX_SUPPORTED_MODES) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(out_buf, &supported_modes, sizeof(supported_modes));
        *out_actual = sizeof(supported_modes);
        return ZX_OK;
    }

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t Init(void* ctx, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    zx_status_t status = self.Init();
    return zircon_camera_CameraSensorInit_reply(txn, status);
}

static zx_status_t DeInit(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.DeInit();
    return ZX_OK;
}

static zx_status_t SetMode(void* ctx, uint8_t mode, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    zx_status_t status = self.SetMode(mode);
    return zircon_camera_CameraSensorSetMode_reply(txn, status);
}

static zx_status_t StartStreaming(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.StartStreaming();
    return ZX_OK;
}

static zx_status_t StopStreaming(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.StopStreaming();
    return ZX_OK;
}

static zx_status_t SetAnalogGain(void* ctx, int32_t gain, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    int32_t actual_gain = self.SetAnalogGain(gain);
    return zircon_camera_CameraSensorSetAnalogGain_reply(txn, actual_gain);
}

static zx_status_t SetDigitalGain(void* ctx, int32_t gain, fidl_txn_t* txn) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    int32_t actual_gain = self.SetDigitalGain(gain);
    return zircon_camera_CameraSensorSetDigitalGain_reply(txn, actual_gain);
}

static zx_status_t SetIntegrationTime(void* ctx,
                                      int32_t int_time,
                                      int32_t int_time_M,
                                      int32_t int_time_L) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.SetIntegrationTime(int_time, int_time_M, int_time_L);
    return ZX_OK;
}

static zx_status_t Update(void* ctx) {
    auto& self = *static_cast<Imx227Device*>(ctx);
    self.Update();
    return ZX_OK;
}

zircon_camera_CameraSensor_ops_t fidl_ops = {
    .Init = Init,
    .DeInit = DeInit,
    .SetMode = SetMode,
    .StartStreaming = StartStreaming,
    .StopStreaming = StopStreaming,
    .SetAnalogGain = SetAnalogGain,
    .SetDigitalGain = SetDigitalGain,
    .SetIntegrationTime = SetIntegrationTime,
    .Update = Update,
};

zx_status_t Imx227Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return zircon_camera_CameraSensor_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t Imx227Device::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto sensor_device = fbl::make_unique_checked<Imx227Device>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = sensor_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    status = sensor_device->DdkAdd("imx227");

    // sensor_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = sensor_device.release();

    return status;
}

void Imx227Device::ShutDown() {
}

void Imx227Device::DdkUnbind() {
    DdkRemove();
}

void Imx227Device::DdkRelease() {
    ShutDown();
    delete this;
}

} // namespace camera

extern "C" zx_status_t imx227_bind(void* ctx, zx_device_t* device) {
    return camera::Imx227Device::Create(device);
}
