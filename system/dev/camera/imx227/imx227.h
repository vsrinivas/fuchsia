// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clk.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddktl/protocol/mipicsi.h>
#include <zircon/camera/c/fidl.h>

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
    zircon_camera_SensorInfo param;
} sensor_context_t;

class Imx227Device;
using DeviceType = ddk::Device<Imx227Device,
                               ddk::Unbindable,
                               ddk::Ioctlable,
                               ddk::Messageable>;

class Imx227Device : public DeviceType,
                     public ddk::internal::base_protocol {
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
        : DeviceType(device) {
        ddk_proto_id_ = ZX_PROTOCOL_CAMERA;
    }

    // Methods required by the ddk mixins.
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    // Methods for IOCTLs.
    zx_status_t GetInfo(zircon_camera_SensorInfo* out_info);
    void GetSupportedModes(void* out_buf, size_t* out_actual);

    // Methods for FIDL Message.
    zx_status_t Init();
    void DeInit();
    zx_status_t SetMode(uint8_t mode);
    void StartStreaming();
    void StopStreaming();
    int32_t SetAnalogGain(int32_t gain);
    int32_t SetDigitalGain(int32_t gain);
    void SetIntegrationTime(int32_t int_time, int32_t int_time_M, int32_t int_time_L);
    void Update();

private:
    // Sensor Context
    sensor_context_t ctx_;

    // Protocols.
    pdev_protocol_t pdev_;
    i2c_protocol_t i2c_;
    gpio_protocol_t gpios_[GPIO_COUNT];
    clk_protocol_t clk_;
    mipi_csi_protocol_t mipi_;

    // I2C Helpers.
    uint8_t ReadReg(uint16_t addr);
    void WriteReg(uint16_t addr, uint8_t val);

    zx_status_t InitPdev(zx_device_t* parent);
    zx_status_t InitSensor(uint8_t idx);
    void ShutDown();
    bool ValidateSensorID();
};

} // namespace camera
