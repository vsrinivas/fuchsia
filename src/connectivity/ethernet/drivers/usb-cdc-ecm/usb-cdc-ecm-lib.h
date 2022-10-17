// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_

#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/descriptor/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <threads.h>

#include <memory>

#include <fbl/auto_lock.h>
#include <src/lib/listnode/listnode.h>
#include <usb/cdc.h>
#include <usb/usb.h>

namespace usb_cdc_ecm {

class EcmEndpoint {
 public:
  explicit EcmEndpoint(const usb_endpoint_descriptor_t* desc)
      : addr(desc->b_endpoint_address), max_packet_size(desc->w_max_packet_size) {}

  uint8_t addr;
  uint16_t max_packet_size;
};

class EcmInterface {
 public:
  explicit EcmInterface(const usb_interface_descriptor_t* desc)
      : number(desc->b_interface_number), alternate_setting(desc->b_alternate_setting) {}
  uint8_t number;
  uint8_t alternate_setting;
};

using MacAddress = std::array<uint8_t, ETH_MAC_SIZE>;

class UsbCdcDescriptorParser {
 public:
  static zx::result<UsbCdcDescriptorParser> Parse(usb::UsbDevice& usb);

  EcmEndpoint GetInterruptEndpoint() const { return int_ep_; }
  EcmEndpoint GetTxEndpoint() const { return tx_ep_; }
  EcmEndpoint GetRxEndpoint() const { return rx_ep_; }

  EcmInterface GetDefaultInterface() const { return default_ifc_; }
  EcmInterface GetDataInterface() const { return data_ifc_; }

  uint16_t GetMtu() const { return mtu_; }
  MacAddress GetMacAddress() const { return mac_addr_; }

 private:
  explicit UsbCdcDescriptorParser(EcmEndpoint int_ep, EcmEndpoint tx_ep, EcmEndpoint rx_ep,
                                  EcmInterface default_ifc, EcmInterface data_ifc, uint16_t mtu,
                                  MacAddress mac_addr)
      : int_ep_(int_ep),
        tx_ep_(tx_ep),
        rx_ep_(rx_ep),
        default_ifc_(default_ifc),
        data_ifc_(data_ifc),
        mtu_(mtu),
        mac_addr_(mac_addr) {}

  static const uint16_t kCdcSupportedVersion = 0x0110; /* 1.10 */

  // MAC address is stored in a string descriptor in UTF-16 format, so we get one byte of
  // address for each 32 bits of text.
  static const size_t kExpectedStringSize =
      sizeof(usb_string_descriptor_t) + ETH_MAC_SIZE * sizeof(uint32_t);

  static zx::result<MacAddress> ParseMacAddress(usb::UsbDevice& usb,
                                                const usb_cs_ethernet_interface_descriptor_t* desc);

  EcmEndpoint int_ep_;
  EcmEndpoint tx_ep_;
  EcmEndpoint rx_ep_;

  EcmInterface default_ifc_;
  EcmInterface data_ifc_;

  uint16_t mtu_;
  MacAddress mac_addr_;
};

}  // namespace usb_cdc_ecm

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_USB_CDC_ECM_USB_CDC_ECM_LIB_H_
