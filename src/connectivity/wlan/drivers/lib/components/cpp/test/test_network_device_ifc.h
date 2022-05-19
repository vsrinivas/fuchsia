// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_TEST_TEST_NETWORK_DEVICE_IFC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_TEST_TEST_NETWORK_DEVICE_IFC_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

#include <optional>

namespace wlan::drivers::components::test {

// Test implementation of network_device_ifc_protocol_t that contains mock calls useful for
// mocking and veriyfing interactions with a network device.
class TestNetworkDeviceIfc : public ::ddk::NetworkDeviceIfcProtocol<TestNetworkDeviceIfc> {
 public:
  TestNetworkDeviceIfc() : proto_{&network_device_ifc_protocol_ops_, this} {}

  const network_device_ifc_protocol_t& GetProto() const { return proto_; }

  // NetworkDeviceIfc methods
  void NetworkDeviceIfcPortStatusChanged(uint8_t id, const port_status_t* new_status) {
    if (port_status_changed_.HasExpectations()) {
      port_status_changed_.Call(id, new_status);
    }
  }
  void NetworkDeviceIfcAddPort(uint8_t id, const network_port_protocol_t* port) {
    if (port) {
      port_proto_ = *port;
    }
    if (add_port_.HasExpectations()) {
      add_port_.Call(id, port);
    }
  }
  void NetworkDeviceIfcRemovePort(uint8_t id) {
    if (remove_port_.HasExpectations()) {
      remove_port_.Call(id);
    }
    if (port_proto_.has_value()) {
      network_port_removed(&port_proto_.value());
    }
  }
  void NetworkDeviceIfcCompleteRx(const rx_buffer_t* rx_list, size_t rx_count) {
    if (complete_rx_.HasExpectations()) {
      complete_rx_.Call(rx_list, rx_count);
    }
  }
  void NetworkDeviceIfcCompleteTx(const tx_result_t* tx_list, size_t tx_count) {
    if (complete_tx_.HasExpectations()) {
      complete_tx_.Call(tx_list, tx_count);
    }
  }
  void NetworkDeviceIfcSnoop(const rx_buffer_t* rx_list, size_t rx_count) {
    if (snoop_.HasExpectations()) {
      snoop_.Call(rx_list, rx_count);
    }
  }

  mock_function::MockFunction<void, uint8_t, const port_status_t*> port_status_changed_;
  mock_function::MockFunction<void, uint8_t, const network_port_protocol_t*> add_port_;
  mock_function::MockFunction<void, uint8_t> remove_port_;
  mock_function::MockFunction<void, const rx_buffer_t*, size_t> complete_rx_;
  mock_function::MockFunction<void, const tx_result_t*, size_t> complete_tx_;
  mock_function::MockFunction<void, const rx_buffer_t*, size_t> snoop_;

 private:
  network_device_ifc_protocol_t proto_;
  std::optional<network_port_protocol_t> port_proto_;
};

}  // namespace wlan::drivers::components::test

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_TEST_TEST_NETWORK_DEVICE_IFC_H_
