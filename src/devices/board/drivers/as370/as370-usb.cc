// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>
#include <unistd.h>

#include <memory>

#include <ddk/usb-peripheral-config.h>
#include <soc/as370/as370-reset.h>
#include <soc/as370/as370-usb.h>
#include <usb/cdc.h>
#include <usb/dwc2/metadata.h>
#include <usb/peripheral-config.h>
#include <usb/peripheral.h>
#include <usb/usb.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_as370 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> dwc2_mmios{
    {{
        .base = as370::kUsb0Base,
        .length = as370::kUsb0Size,
    }},
};

static const std::vector<fpbus::Irq> dwc2_irqs{
    {{
        .irq = as370::kUsb0Irq,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Bti> usb_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_USB,
    }},
};

// Metadata for DWC2 driver.
constexpr dwc2_metadata_t dwc2_metadata = {
    .dma_burst_len = DWC2_DMA_BURST_INCR8,
    .usb_turnaround_time = 5,

    // Total fifo size is 2648 words, so we can afford to make our FIFO sizes
    // larger than the minimum requirements.
    .rx_fifo_size = 1024,   // for all OUT endpoints.
    .nptx_fifo_size = 256,  // for endpoint zero IN direction.
    .tx_fifo_sizes =
        {
            512,  // for CDC ethernet bulk IN.
            4,    // for CDC ethernet interrupt IN.
            512,  // for test function bulk IN.
            16,   // for test function interrupt IN.
        },
};

// Statically assigned dummy MAC address.
// TODO: Provide real MAC address via bootloader or some other mechanism.
constexpr uint8_t eth_mac_address[] = {
    0x02, 0x98, 0x8f, 0x3c, 0xd2, 0xaa,
};

using FunctionDescriptor = fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor;

static const std::vector<fpbus::Mmio> usb_phy_mmios{
    {{
        .base = as370::kUsbPhy0Base,
        .length = as370::kUsbPhy0Size,
    }},
    {{
        .base = as370::kResetBase,
        .length = as370::kResetSize,
    }},
};

static const fpbus::Node usb_phy_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "as370-usb-phy-v2";
  dev.vid() = PDEV_VID_SYNAPTICS;
  dev.pid() = PDEV_PID_SYNAPTICS_AS370;
  dev.did() = PDEV_DID_AS370_USB_PHY;
  dev.mmio() = usb_phy_mmios;
  dev.bti() = usb_btis;
  return dev;
}();

static const zx_bind_inst_t dwc2_pdev_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_INSTANCE_ID, 0),
};
constexpr device_fragment_part_t dwc2_pdev_fragment_part[] = {
    {std::size(dwc2_pdev_match), dwc2_pdev_match},
};
static const zx_bind_inst_t dwc2_phy_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
};
static const device_fragment_part_t dwc2_phy_fragment[] = {
    {std::size(dwc2_phy_match), dwc2_phy_match},
};
static const device_fragment_t dwc2_fragments[] = {
    {"pdev", std::size(dwc2_pdev_fragment_part), dwc2_pdev_fragment_part},
    {"dwc2-phy", std::size(dwc2_phy_fragment), dwc2_phy_fragment},
};

zx_status_t As370::UsbInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('USB_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, usb_phy_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Usb(usb_phy_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Usb(usb_phy_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  std::unique_ptr<usb::UsbPeripheralConfig> peripheral_config;
  auto status = usb::UsbPeripheralConfig::CreateFromBootArgs(parent_, &peripheral_config);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get usb config from boot args - %d", status);
    return status;
  }

  std::vector<fpbus::Metadata> usb_metadata{
      {{
          .type = DEVICE_METADATA_USB_CONFIG,
          .data = peripheral_config->config_data(),
      }},
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&dwc2_metadata),
              reinterpret_cast<const uint8_t*>(&dwc2_metadata) + sizeof(dwc2_metadata)),
      }},
      {{
          .type = DEVICE_METADATA_MAC_ADDRESS,
          .data = std::vector<uint8_t>(eth_mac_address, eth_mac_address + sizeof(eth_mac_address)),
      }},
  };

  fpbus::Node dwc2_dev;
  dwc2_dev.name() = "dwc2-usb";
  dwc2_dev.vid() = PDEV_VID_GENERIC;
  dwc2_dev.pid() = PDEV_PID_GENERIC;
  dwc2_dev.did() = PDEV_DID_USB_DWC2;
  dwc2_dev.mmio() = dwc2_mmios;
  dwc2_dev.irq() = dwc2_irqs;
  dwc2_dev.bti() = usb_btis;
  dwc2_dev.metadata() = std::move(usb_metadata);

  {
    auto result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, dwc2_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, dwc2_fragments,
                                                 std::size(dwc2_fragments)),
        "dwc2-phy");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(dwc2_dev) request failed: %s",
             __func__, result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(dwc2_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }

  return ZX_OK;
}

}  // namespace board_as370
