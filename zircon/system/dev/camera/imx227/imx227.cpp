// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx227.h"
#include "imx227-seq.h"
#include "imx227-test.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <ddk/protocol/i2c-lib.h>
#include <endian.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <memory>
#include <stdint.h>
#include <threads.h>
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

    // I2c for communicating with the sensor.
    if (!i2c_.is_valid()) {
        zxlogf(ERROR, "%s; I2C not available\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    // Clk for gating clocks for sensor.
    if (!clk24_.is_valid()) {
        zxlogf(ERROR, "%s; clk24_ not available\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    // Mipi for init and de-init.
    if (!mipi_.is_valid()) {
        zxlogf(ERROR, "%s; mipi_ not available\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    // GPIOs
    if (!gpio_vana_enable_.is_valid()) {
        zxlogf(ERROR, "%s; gpio_vana_enable_ not available\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    if (!gpio_vdig_enable_.is_valid()) {
        zxlogf(ERROR, "%s; gpio_vdig_enable_ not available\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    if (!gpio_cam_rst_.is_valid()) {
        zxlogf(ERROR, "%s; gpio_cam_rst_ not available\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    // Set the GPIO to output and set them to their initial values
    // before the power up sequence.
    gpio_cam_rst_.ConfigOut(1);
    gpio_vana_enable_.ConfigOut(0);
    gpio_vdig_enable_.ConfigOut(0);
    return ZX_OK;
}

uint8_t Imx227Device::ReadReg(uint16_t addr) {
    // Convert the address to Big Endian format.
    // The camera sensor expects in this format.
    uint16_t buf = htobe16(addr);
    uint8_t val = 0;
    zx_status_t status = i2c_.WriteReadSync(reinterpret_cast<uint8_t*>(&buf), sizeof(buf),
                                            &val, sizeof(val));
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

    zx_status_t status = i2c_.WriteSync(buf, 3);
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

zx_status_t Imx227Device::CameraSensorInit() {
    // Power up sequence. Reference: Page 51- IMX227-0AQH5-C datasheet.
    gpio_vana_enable_.Write(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    gpio_vdig_enable_.Write(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    // Enable 24M clock for sensor.
    clk24_.Enable();
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    gpio_cam_rst_.Write(0);
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

    initialized_ = true;
    zxlogf(INFO, "%s IMX227 Camera Sensor Brought out of reset\n", __func__);
    return ZX_OK;
}

void Imx227Device::CameraSensorDeInit() {
    mipi_.DeInit();
    // Enable 24M clock for sensor.
    clk24_.Disable();
    // Reference code has it, mostly likely needed for the clock to
    // stabalize. No other way of knowing for sure if sensor is now off.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    initialized_ = false;
}

zx_status_t Imx227Device::CameraSensorGetInfo(sensor_info_t* out_info) {
    if (out_info == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(out_info, &ctx_.param, sizeof(sensor_info_t));
    return ZX_OK;
}

zx_status_t Imx227Device::CameraSensorGetSupportedModes(sensor_mode_t* out_modes_list,
                                                        size_t modes_count,
                                                        size_t* out_modes_actual) {
    if (out_modes_list == nullptr || out_modes_actual == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (modes_count > countof(supported_modes)) {
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(out_modes_list, &supported_modes, sizeof(sensor_mode_t) * countof(supported_modes));
    *out_modes_actual = countof(supported_modes);
    return ZX_OK;
}

zx_status_t Imx227Device::CameraSensorSetMode(uint8_t mode) {
    zxlogf(INFO, "%s IMX227 Camera Sensor Mode Set request to %d\n", __func__, mode);

    // Get Sensor ID to see if sensor is initialized.
    if (!IsSensorInitialized() || !ValidateSensorID()) {
        return ZX_ERR_INTERNAL;
    }

    if (mode >= countof(supported_modes)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (supported_modes[mode].wdr_mode) {
    case WDR_MODE_LINEAR: {

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
    ctx_.int_max = 0x0ADE; // Max allowed for 30fps = 2782 (dec), 0x0ADE (hex)
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
    adap_info.mode = MIPI_MODES_DIR_MODE;
    return mipi_.Init(&mipi_info, &adap_info);
}

zx_status_t Imx227Device::CameraSensorStartStreaming() {
    if (!IsSensorInitialized() || ctx_.streaming_flag) {
        return ZX_ERR_BAD_STATE;
    }
    zxlogf(INFO, "%s Camera Sensor Start Streaming\n", __func__);
    ctx_.streaming_flag = 1;
    WriteReg(0x0100, 0x01);
    return ZX_OK;
}

zx_status_t Imx227Device::CameraSensorStopStreaming() {
    if (!IsSensorInitialized() || !ctx_.streaming_flag) {
        return ZX_ERR_BAD_STATE;
    }
    ctx_.streaming_flag = 0;
    WriteReg(0x0100, 0x00);
    return ZX_OK;
}

int32_t Imx227Device::CameraSensorSetAnalogGain(int32_t gain) {
    return ZX_ERR_NOT_SUPPORTED;
}

int32_t Imx227Device::CameraSensorSetDigitalGain(int32_t gain) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensorSetIntegrationTime(int32_t int_time) {
    // TODO(braval): Add support for this.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensorUpdate() {
    return ZX_ERR_NOT_SUPPORTED;
}

// static
zx_status_t Imx227Device::Setup(void* ctx,
                                zx_device_t* parent,
                                std::unique_ptr<Imx227Device>* out) {
    ddk::CompositeProtocolClient composite(parent);
    if (!composite.is_valid()) {
        zxlogf(ERROR, "%s could not get composite protocoln", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_device_t* components[COMPONENT_COUNT];
    size_t actual;
    composite.GetComponents(components, COMPONENT_COUNT, &actual);
    if (actual != COMPONENT_COUNT) {
        zxlogf(ERROR, "%s Could not get components\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    auto sensor_device = std::unique_ptr<Imx227Device>(
        new (&ac) Imx227Device(parent,
                               components[COMPONENT_I2C],
                               components[COMPONENT_GPIO_VANA],
                               components[COMPONENT_GPIO_VDIG],
                               components[COMPONENT_GPIO_CAM_RST],
                               components[COMPONENT_CLK24],
                               components[COMPONENT_MIPICSI]));
    if (!ac.check()) {
        zxlogf(ERROR, "%s Could not create Imx227Device device\n", __func__);
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = sensor_device->InitPdev(parent);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s InitPdev failed\n", __func__);
        return status;
    }
    *out = std::move(sensor_device);
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

zx_status_t imx227_bind(void* ctx, zx_device_t* device) {
    std::unique_ptr<Imx227Device> sensor_device;
    zx_status_t status = camera::Imx227Device::Setup(ctx, device, &sensor_device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "imx227: Could not setup imx227 sensor device: %d\n", status);
        return status;
    }
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_SONY},
        {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_SONY_IMX227},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_CAMERA_SENSOR},
    };

    // Run the unit tests for this device
    // TODO(braval): CAM-44 (Run only when build flag enabled)
    // This needs to be replaced with run unittests hooks when
    // the framework is available.
    status = camera::Imx227DeviceTester::RunTests(sensor_device.get());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Device Unit Tests Failed \n", __func__);
        return status;
    }

    status = sensor_device->DdkAdd("imx227", 0, props, countof(props));
    if (status != ZX_OK) {
        zxlogf(ERROR, "imx227: Could not add imx227 sensor device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "imx227 driver added\n");
    }

    // sensor_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto* dev = sensor_device.release();
    return ZX_OK;
}

static zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = imx227_bind;
    return ops;
}();

} // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(imx227, camera::driver_ops, "imx227", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SONY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_SONY_IMX227),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_CAMERA_SENSOR),
ZIRCON_DRIVER_END(imx227)
