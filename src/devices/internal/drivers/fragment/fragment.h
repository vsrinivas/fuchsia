// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_FRAGMENT_H_
#define SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_FRAGMENT_H_

#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <fuchsia/hardware/amlogiccanvas/cpp/banjo.h>
#include <fuchsia/hardware/audio/cpp/banjo.h>
#include <fuchsia/hardware/camera/sensor/cpp/banjo.h>
#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <fuchsia/hardware/dsi/cpp/banjo.h>
#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <fuchsia/hardware/ethernet/board/cpp/banjo.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/addressspace/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/sync/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/hdmi/cpp/banjo.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <fuchsia/hardware/mipicsi/cpp/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/power/cpp/banjo.h>
#include <fuchsia/hardware/power/sensor/cpp/banjo.h>
#include <fuchsia/hardware/powerimpl/cpp/banjo.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <fuchsia/hardware/scpi/cpp/banjo.h>
#include <fuchsia/hardware/sdio/cpp/banjo.h>
#include <fuchsia/hardware/shareddma/cpp/banjo.h>
#include <fuchsia/hardware/spi/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/hardware/tee/cpp/banjo.h>
#include <fuchsia/hardware/thermal/cpp/banjo.h>
#include <fuchsia/hardware/usb/modeswitch/cpp/banjo.h>
#include <fuchsia/hardware/usb/phy/cpp/banjo.h>
#include <fuchsia/hardware/vreg/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/fragment-device.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>

#include <ddktl/device.h>

namespace fragment {

template <typename ProtoClientType, typename ProtoType>
class ProtocolClient {
 public:
  ProtocolClient() { parent_ = nullptr; }
  ProtocolClient(zx_device_t* parent, uint32_t proto_id);
  ProtoClientType& proto_client() { return proto_client_; }
  ~ProtocolClient() {
    if (is_session_) {
      ZX_DEBUG_ASSERT(device_close_protocol_session_multibindable(parent_, proto_.ctx) == ZX_OK);
    }
  }

 private:
  bool is_session_ = false;
  ProtoType proto_;
  ProtoClientType proto_client_;
  zx_device_t* parent_;
};

class Fragment;
using FragmentBase = ddk::Device<Fragment, ddk::Rxrpcable, ddk::GetProtocolable>;

class Fragment : public FragmentBase {
 public:
  explicit Fragment(zx_device_t* parent)
      : FragmentBase(parent),
        acpi_client_(parent, ZX_PROTOCOL_ACPI),
        canvas_client_(parent, ZX_PROTOCOL_AMLOGIC_CANVAS),
        clock_client_(parent, ZX_PROTOCOL_CLOCK),
        eth_board_client_(parent, ZX_PROTOCOL_ETH_BOARD),
        goldfish_address_space_client_(parent, ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE),
        goldfish_pipe_client_(parent, ZX_PROTOCOL_GOLDFISH_PIPE),
        goldfish_sync_client_(parent, ZX_PROTOCOL_GOLDFISH_SYNC),
        gpio_client_(parent, ZX_PROTOCOL_GPIO),
        hdmi_client_(parent, ZX_PROTOCOL_HDMI),
        i2c_client_(parent, ZX_PROTOCOL_I2C),
        codec_client_(parent, ZX_PROTOCOL_CODEC),
        dai_client_(parent, ZX_PROTOCOL_DAI),
        pdev_client_(parent, ZX_PROTOCOL_PDEV),
        power_client_(parent, ZX_PROTOCOL_POWER),
        pwm_client_(parent, ZX_PROTOCOL_PWM),
        spi_client_(parent, ZX_PROTOCOL_SPI),
        sysmem_client_(parent, ZX_PROTOCOL_SYSMEM),
        tee_client_(parent, ZX_PROTOCOL_TEE),
        ums_client_(parent, ZX_PROTOCOL_USB_MODE_SWITCH),
        power_impl_client_(parent, ZX_PROTOCOL_POWER_IMPL),
        dsi_impl_client_(parent, ZX_PROTOCOL_DSI_IMPL),
        sdio_client_(parent, ZX_PROTOCOL_SDIO),
        thermal_client_(parent, ZX_PROTOCOL_THERMAL),
        isp_client_(parent, ZX_PROTOCOL_ISP),
        shared_dma_client_(parent, ZX_PROTOCOL_SHARED_DMA),
        usb_phy_client_(parent, ZX_PROTOCOL_USB_PHY),
        mipi_csi_client_(parent, ZX_PROTOCOL_MIPI_CSI),
        camera_sensor2_client_(parent, ZX_PROTOCOL_CAMERA_SENSOR2),
        gdc_client_(parent, ZX_PROTOCOL_GDC),
        ge2d_client_(parent, ZX_PROTOCOL_GE2D),
        scpi_client_(parent, ZX_PROTOCOL_SCPI),
        registers_client_(parent, ZX_PROTOCOL_REGISTERS),
        vreg_client_(parent, ZX_PROTOCOL_VREG),
        dsi_client_(parent, ZX_PROTOCOL_DSI),
        pci_client_(parent, ZX_PROTOCOL_PCI),
        power_sensor_client_(parent, ZX_PROTOCOL_POWER_SENSOR) {}

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  zx_status_t DdkRxrpc(zx_handle_t channel);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);

 private:
  struct I2cTransactContext {
    sync_completion_t completion;
    void* read_buf;
    size_t read_length;
    zx_status_t result;
  };
  struct CodecTransactContext {
    sync_completion_t completion;
    zx_status_t status;
    void* buffer;
    size_t size;
  };
  zx_status_t RpcAcpi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcCanvas(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                        uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                        zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcClock(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                       zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcEthBoard(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                          uint32_t* out_resp_size, zx::handle* req_handles,
                          uint32_t req_handle_count, zx::handle* resp_handles,
                          uint32_t* resp_handle_count);
  zx_status_t RpcGoldfishAddressSpace(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                                      uint32_t* out_resp_size, zx::handle* req_handles,
                                      uint32_t req_handle_count, zx::handle* resp_handles,
                                      uint32_t* resp_handle_count);
  zx_status_t RpcGoldfishPipe(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count);
  zx_status_t RpcGoldfishSync(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                              uint32_t* out_resp_size, zx::handle* req_handles,
                              uint32_t req_handle_count, zx::handle* resp_handles,
                              uint32_t* resp_handle_count);
  zx_status_t RpcGpio(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcHdmi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcI2c(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPdev(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPower(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                       zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPwm(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcSpi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcSysmem(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                        uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                        zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcTee(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcUms(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcCodec(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                       uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                       zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcDai(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcRegisters(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                           uint32_t* out_resp_size, zx::handle* req_handles,
                           uint32_t req_handle_count, zx::handle* resp_handles,
                           uint32_t* resp_handle_count);
  zx_status_t RpcVreg(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                      uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                      zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcDsi(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPci(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                     uint32_t* out_resp_size, zx::handle* req_handles, uint32_t req_handle_count,
                     zx::handle* resp_handles, uint32_t* resp_handle_count);
  zx_status_t RpcPowerSensor(const uint8_t* req_buf, uint32_t req_size, uint8_t* resp_buf,
                             uint32_t* out_resp_size, zx::handle* req_handles,
                             uint32_t req_handle_count, zx::handle* resp_handles,
                             uint32_t* resp_handle_count);

  static void I2cTransactCallback(void* cookie, zx_status_t status, const i2c_op_t* op_list,
                                  size_t op_count);

  ProtocolClient<ddk::AcpiProtocolClient, acpi_protocol_t> acpi_client_;
  ProtocolClient<ddk::AmlogicCanvasProtocolClient, amlogic_canvas_protocol_t> canvas_client_;
  ProtocolClient<ddk::ClockProtocolClient, clock_protocol_t> clock_client_;
  ProtocolClient<ddk::EthBoardProtocolClient, eth_board_protocol_t> eth_board_client_;
  ProtocolClient<ddk::GoldfishAddressSpaceProtocolClient, goldfish_address_space_protocol_t>
      goldfish_address_space_client_;
  ProtocolClient<ddk::GoldfishPipeProtocolClient, goldfish_pipe_protocol_t> goldfish_pipe_client_;
  ProtocolClient<ddk::GoldfishSyncProtocolClient, goldfish_sync_protocol_t> goldfish_sync_client_;
  ProtocolClient<ddk::GpioProtocolClient, gpio_protocol_t> gpio_client_;
  ProtocolClient<ddk::HdmiProtocolClient, hdmi_protocol_t> hdmi_client_;
  ProtocolClient<ddk::I2cProtocolClient, i2c_protocol_t> i2c_client_;
  ProtocolClient<ddk::CodecProtocolClient, codec_protocol_t> codec_client_;
  ProtocolClient<ddk::DaiProtocolClient, dai_protocol_t> dai_client_;
  ProtocolClient<ddk::PDevProtocolClient, pdev_protocol_t> pdev_client_;
  ProtocolClient<ddk::PowerProtocolClient, power_protocol_t> power_client_;
  ProtocolClient<ddk::PwmProtocolClient, pwm_protocol_t> pwm_client_;
  ProtocolClient<ddk::SpiProtocolClient, spi_protocol_t> spi_client_;
  ProtocolClient<ddk::SysmemProtocolClient, sysmem_protocol_t> sysmem_client_;
  ProtocolClient<ddk::TeeProtocolClient, tee_protocol_t> tee_client_;
  ProtocolClient<ddk::UsbModeSwitchProtocolClient, usb_mode_switch_protocol_t> ums_client_;
  ProtocolClient<ddk::PowerImplProtocolClient, power_impl_protocol_t> power_impl_client_;
  ProtocolClient<ddk::DsiImplProtocolClient, dsi_impl_protocol_t> dsi_impl_client_;
  ProtocolClient<ddk::SdioProtocolClient, sdio_protocol_t> sdio_client_;
  ProtocolClient<ddk::ThermalProtocolClient, thermal_protocol_t> thermal_client_;
  ProtocolClient<ddk::IspProtocolClient, isp_protocol_t> isp_client_;
  ProtocolClient<ddk::SharedDmaProtocolClient, shared_dma_protocol_t> shared_dma_client_;
  ProtocolClient<ddk::UsbPhyProtocolClient, usb_phy_protocol_t> usb_phy_client_;
  ProtocolClient<ddk::MipiCsiProtocolClient, mipi_csi_protocol_t> mipi_csi_client_;
  ProtocolClient<ddk::CameraSensor2ProtocolClient, camera_sensor2_protocol_t>
      camera_sensor2_client_;
  ProtocolClient<ddk::GdcProtocolClient, gdc_protocol_t> gdc_client_;
  ProtocolClient<ddk::Ge2dProtocolClient, ge2d_protocol_t> ge2d_client_;
  ProtocolClient<ddk::ScpiProtocolClient, scpi_protocol_t> scpi_client_;
  ProtocolClient<ddk::RegistersProtocolClient, registers_protocol_t> registers_client_;
  ProtocolClient<ddk::VregProtocolClient, vreg_protocol_t> vreg_client_;
  ProtocolClient<ddk::DsiProtocolClient, dsi_protocol_t> dsi_client_;
  ProtocolClient<ddk::PciProtocolClient, pci_protocol_t> pci_client_;
  ProtocolClient<ddk::PowerSensorProtocolClient, power_sensor_protocol_t> power_sensor_client_;
};

}  // namespace fragment

#endif  // SRC_DEVICES_INTERNAL_DRIVERS_FRAGMENT_FRAGMENT_H_
