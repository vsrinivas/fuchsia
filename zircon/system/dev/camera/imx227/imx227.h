// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/i2c-channel.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/camerasensor.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/mipicsi.h>

namespace camera {

typedef struct sensor_context {
    // TODO(braval): Add details for each one of these
    //               and also remove unused ones.
    uint32_t again_limit;
    uint32_t int_max;
    uint32_t dgain_limit;
    uint32_t wdr_mode;
    uint32_t gain_cnt;
    uint32_t t_height;
    uint32_t int_time_limit;
    uint32_t t_height_old;
    uint16_t int_time;
    uint16_t VMAX;
    uint16_t HMAX;
    uint16_t dgain_old;
    uint16_t int_time_min;
    uint16_t again_old;
    uint16_t dgain[2];
    uint16_t again[2];
    uint8_t seq_width;
    uint8_t streaming_flag;
    uint8_t again_delay;
    uint8_t again_change;
    uint8_t dgain_delay;
    uint8_t dgain_change;
    uint8_t change_flag;
    uint8_t hdr_flag;
    sensor_info_t param;
} sensor_context_t;

class Imx227Device;
using DeviceType = ddk::Device<Imx227Device, ddk::Unbindable>;

class Imx227Device : public DeviceType,
                     public ddk::CameraSensorProtocol<Imx227Device, ddk::base_protocol> {
public:
    enum {
        COMPONENT_I2C,
        COMPONENT_GPIO_VANA,
        COMPONENT_GPIO_VDIG,
        COMPONENT_GPIO_CAM_RST,
        COMPONENT_CLK24,
        COMPONENT_MIPICSI,
        COMPONENT_COUNT,
    };

    static zx_status_t Create(void* ctx, zx_device_t* parent);
    Imx227Device(zx_device_t* device,
                 zx_device_t* i2c,
                 zx_device_t* gpio_vana,
                 zx_device_t* gpio_vdig,
                 zx_device_t* gpio_cam_rst,
                 zx_device_t* clk24,
                 zx_device_t* mipicsi)
        : DeviceType(device), i2c_(i2c), gpio_vana_enable_(gpio_vana),
          gpio_vdig_enable_(gpio_vdig), gpio_cam_rst_(gpio_cam_rst),
          clk24_(clk24), mipi_(mipicsi) {}

    // Methods required by the ddk mixins.
    void DdkUnbind();
    void DdkRelease();

    // Methods for ZX_PROTOCOL_CAMERA_SENSOR.
    zx_status_t CameraSensorInit();
    void CameraSensorDeInit();
    zx_status_t CameraSensorSetMode(uint8_t mode);
    void CameraSensorStartStreaming();
    void CameraSensorStopStreaming();
    int32_t CameraSensorSetAnalogGain(int32_t gain);
    int32_t CameraSensorSetDigitalGain(int32_t gain);
    void CameraSensorSetIntegrationTime(int32_t int_time, int32_t int_time_M, int32_t int_time_L);
    zx_status_t CameraSensorUpdate();
    zx_status_t CameraSensorGetInfo(sensor_info_t* out_info);
    zx_status_t CameraSensorGetSupportedModes(sensor_mode_t* out_modes_list,
                                              size_t modes_count,
                                              size_t* out_modes_actual);

private:
    // Sensor Context
    sensor_context_t ctx_;

    // Protocols.
    ddk::I2cChannel i2c_;
    ddk::GpioProtocolClient gpio_vana_enable_;
    ddk::GpioProtocolClient gpio_vdig_enable_;
    ddk::GpioProtocolClient gpio_cam_rst_;
    ddk::ClockProtocolClient clk24_;
    ddk::MipiCsiProtocolClient mipi_;

    // I2C Helpers.
    uint8_t ReadReg(uint16_t addr);
    void WriteReg(uint16_t addr, uint8_t val);

    zx_status_t InitPdev(zx_device_t* parent);
    zx_status_t InitSensor(uint8_t idx);
    void ShutDown();
    bool ValidateSensorID();
};

} // namespace camera
