// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/i2c-channel.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/ispimpl.h>
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
                     public ddk::IspCallbacksProtocol<Imx227Device> {
public:
    // GPIO Indexes.
    enum {
        VANA_ENABLE,
        VDIG_ENABLE,
        CAM_SENSOR_RST,
        GPIO_COUNT,
    };

    static zx_status_t Create(zx_device_t* parent);
    Imx227Device(zx_device_t* device)
        : DeviceType(device), pdev_(device), i2c_(device),
          clk_(device), mipi_(device), ispimpl_(device) {}

    // Methods required by the ddk mixins.
    void DdkUnbind();
    void DdkRelease();

    // Methods for callbacks.
    zx_status_t IspCallbacksInit();
    void IspCallbacksDeInit();
    zx_status_t IspCallbacksSetMode(uint8_t mode);
    void IspCallbacksStartStreaming();
    void IspCallbacksStopStreaming();
    int32_t IspCallbacksSetAnalogGain(int32_t gain);
    int32_t IspCallbacksSetDigitalGain(int32_t gain);
    void IspCallbacksSetIntegrationTime(int32_t int_time, int32_t int_time_M, int32_t int_time_L);
    zx_status_t IspCallbacksUpdate();
    zx_status_t IspCallbacksGetInfo(sensor_info_t* out_info);
    zx_status_t IspCallbacksGetSupportedModes(sensor_mode_t* out_modes_list,
                                              size_t modes_count,
                                              size_t* out_modes_actual);

private:
    // Sensor Context
    sensor_context_t ctx_;

    // Protocols.
    ddk::PDev pdev_;
    ddk::I2cChannel i2c_;
    ddk::GpioProtocolClient gpios_[GPIO_COUNT];
    ddk::ClockProtocolClient clk_;
    ddk::MipiCsiProtocolClient mipi_;
    ddk::IspImplProtocolClient ispimpl_;

    // I2C Helpers.
    uint8_t ReadReg(uint16_t addr);
    void WriteReg(uint16_t addr, uint8_t val);

    zx_status_t InitPdev(zx_device_t* parent);
    zx_status_t InitSensor(uint8_t idx);
    void ShutDown();
    bool ValidateSensorID();
};

} // namespace camera
