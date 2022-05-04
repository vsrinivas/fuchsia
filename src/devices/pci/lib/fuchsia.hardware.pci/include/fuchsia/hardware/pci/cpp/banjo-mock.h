// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.pci banjo file

#ifndef SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_MOCK_H_
#define SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_MOCK_H_

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

#include <tuple>

namespace ddk {

// This class mocks a device by providing a pci_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockPci pci;
//
// /* Set some expectations on the device by calling pci.Expect... methods. */
//
// SomeDriver dut(pci.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(pci.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockPci : ddk::PciProtocol<MockPci> {
 public:
  MockPci() : proto_{&pci_protocol_ops_, this} {}

  virtual ~MockPci() {}

  const pci_protocol_t* GetProto() const { return &proto_; }

  virtual MockPci& ExpectGetDeviceInfo(zx_status_t out_s, pci_device_info_t out_info) {
    mock_get_device_info_.ExpectCall({out_s, out_info});
    return *this;
  }

  virtual MockPci& ExpectGetBar(zx_status_t out_s, uint32_t bar_id, pci_bar_t out_result) {
    mock_get_bar_.ExpectCall({out_s, out_result}, bar_id);
    return *this;
  }

  virtual MockPci& ExpectSetBusMastering(zx_status_t out_s, bool enabled) {
    mock_set_bus_mastering_.ExpectCall({out_s}, enabled);
    return *this;
  }

  virtual MockPci& ExpectResetDevice(zx_status_t out_s) {
    mock_reset_device_.ExpectCall({out_s});
    return *this;
  }

  virtual MockPci& ExpectAckInterrupt(zx_status_t out_s) {
    mock_ack_interrupt_.ExpectCall({out_s});
    return *this;
  }

  virtual MockPci& ExpectMapInterrupt(zx_status_t out_s, uint32_t which_irq,
                                      zx::interrupt out_interrupt) {
    mock_map_interrupt_.ExpectCall({out_s, std::move(out_interrupt)}, which_irq);
    return *this;
  }

  virtual MockPci& ExpectGetInterruptModes(pci_interrupt_modes_t out_modes) {
    mock_get_interrupt_modes_.ExpectCall({out_modes});
    return *this;
  }

  virtual MockPci& ExpectSetInterruptMode(zx_status_t out_s, pci_interrupt_mode_t mode,
                                          uint32_t requested_irq_count) {
    mock_set_interrupt_mode_.ExpectCall({out_s}, mode, requested_irq_count);
    return *this;
  }

  virtual MockPci& ExpectReadConfig8(zx_status_t out_s, uint16_t offset, uint8_t out_value) {
    mock_read_config8_.ExpectCall({out_s, out_value}, offset);
    return *this;
  }

  virtual MockPci& ExpectReadConfig16(zx_status_t out_s, uint16_t offset, uint16_t out_value) {
    mock_read_config16_.ExpectCall({out_s, out_value}, offset);
    return *this;
  }

  virtual MockPci& ExpectReadConfig32(zx_status_t out_s, uint16_t offset, uint32_t out_value) {
    mock_read_config32_.ExpectCall({out_s, out_value}, offset);
    return *this;
  }

  virtual MockPci& ExpectWriteConfig8(zx_status_t out_s, uint16_t offset, uint8_t value) {
    mock_write_config8_.ExpectCall({out_s}, offset, value);
    return *this;
  }

  virtual MockPci& ExpectWriteConfig16(zx_status_t out_s, uint16_t offset, uint16_t value) {
    mock_write_config16_.ExpectCall({out_s}, offset, value);
    return *this;
  }

  virtual MockPci& ExpectWriteConfig32(zx_status_t out_s, uint16_t offset, uint32_t value) {
    mock_write_config32_.ExpectCall({out_s}, offset, value);
    return *this;
  }

  virtual MockPci& ExpectGetFirstCapability(zx_status_t out_s, pci_capability_id_t id,
                                            uint8_t out_offset) {
    mock_get_first_capability_.ExpectCall({out_s, out_offset}, id);
    return *this;
  }

  virtual MockPci& ExpectGetNextCapability(zx_status_t out_s, pci_capability_id_t id,
                                           uint8_t start_offset, uint8_t out_offset) {
    mock_get_next_capability_.ExpectCall({out_s, out_offset}, id, start_offset);
    return *this;
  }

  virtual MockPci& ExpectGetFirstExtendedCapability(zx_status_t out_s,
                                                    pci_extended_capability_id_t id,
                                                    uint16_t out_offset) {
    mock_get_first_extended_capability_.ExpectCall({out_s, out_offset}, id);
    return *this;
  }

  virtual MockPci& ExpectGetNextExtendedCapability(zx_status_t out_s,
                                                   pci_extended_capability_id_t id,
                                                   uint16_t start_offset, uint16_t out_offset) {
    mock_get_next_extended_capability_.ExpectCall({out_s, out_offset}, id, start_offset);
    return *this;
  }

  virtual MockPci& ExpectGetBti(zx_status_t out_s, uint32_t index, zx::bti out_bti) {
    mock_get_bti_.ExpectCall({out_s, std::move(out_bti)}, index);
    return *this;
  }

  void VerifyAndClear() {
    mock_get_device_info_.VerifyAndClear();
    mock_get_bar_.VerifyAndClear();
    mock_set_bus_mastering_.VerifyAndClear();
    mock_reset_device_.VerifyAndClear();
    mock_ack_interrupt_.VerifyAndClear();
    mock_map_interrupt_.VerifyAndClear();
    mock_get_interrupt_modes_.VerifyAndClear();
    mock_set_interrupt_mode_.VerifyAndClear();
    mock_read_config8_.VerifyAndClear();
    mock_read_config16_.VerifyAndClear();
    mock_read_config32_.VerifyAndClear();
    mock_write_config8_.VerifyAndClear();
    mock_write_config16_.VerifyAndClear();
    mock_write_config32_.VerifyAndClear();
    mock_get_first_capability_.VerifyAndClear();
    mock_get_next_capability_.VerifyAndClear();
    mock_get_first_extended_capability_.VerifyAndClear();
    mock_get_next_extended_capability_.VerifyAndClear();
    mock_get_bti_.VerifyAndClear();
  }

  virtual zx_status_t PciGetDeviceInfo(pci_device_info_t* out_info) {
    std::tuple<zx_status_t, pci_device_info_t> ret = mock_get_device_info_.Call();
    *out_info = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_result) {
    std::tuple<zx_status_t, pci_bar_t> ret = mock_get_bar_.Call(bar_id);
    *out_result = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciSetBusMastering(bool enabled) {
    std::tuple<zx_status_t> ret = mock_set_bus_mastering_.Call(enabled);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciResetDevice() {
    std::tuple<zx_status_t> ret = mock_reset_device_.Call();
    return std::get<0>(ret);
  }

  virtual zx_status_t PciAckInterrupt() {
    std::tuple<zx_status_t> ret = mock_ack_interrupt_.Call();
    return std::get<0>(ret);
  }

  virtual zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt) {
    std::tuple<zx_status_t, zx::interrupt> ret = mock_map_interrupt_.Call(which_irq);
    *out_interrupt = std::move(std::get<1>(ret));
    return std::get<0>(ret);
  }

  virtual void PciGetInterruptModes(pci_interrupt_modes_t* out_modes) {
    std::tuple<pci_interrupt_modes_t> ret = mock_get_interrupt_modes_.Call();
    *out_modes = std::get<0>(ret);
  }

  virtual zx_status_t PciSetInterruptMode(pci_interrupt_mode_t mode, uint32_t requested_irq_count) {
    std::tuple<zx_status_t> ret = mock_set_interrupt_mode_.Call(mode, requested_irq_count);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciReadConfig8(uint16_t offset, uint8_t* out_value) {
    std::tuple<zx_status_t, uint8_t> ret = mock_read_config8_.Call(offset);
    *out_value = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciReadConfig16(uint16_t offset, uint16_t* out_value) {
    std::tuple<zx_status_t, uint16_t> ret = mock_read_config16_.Call(offset);
    *out_value = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciReadConfig32(uint16_t offset, uint32_t* out_value) {
    std::tuple<zx_status_t, uint32_t> ret = mock_read_config32_.Call(offset);
    *out_value = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciWriteConfig8(uint16_t offset, uint8_t value) {
    std::tuple<zx_status_t> ret = mock_write_config8_.Call(offset, value);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciWriteConfig16(uint16_t offset, uint16_t value) {
    std::tuple<zx_status_t> ret = mock_write_config16_.Call(offset, value);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciWriteConfig32(uint16_t offset, uint32_t value) {
    std::tuple<zx_status_t> ret = mock_write_config32_.Call(offset, value);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciGetFirstCapability(pci_capability_id_t id, uint8_t* out_offset) {
    std::tuple<zx_status_t, uint8_t> ret = mock_get_first_capability_.Call(id);
    *out_offset = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciGetNextCapability(pci_capability_id_t id, uint8_t start_offset,
                                           uint8_t* out_offset) {
    std::tuple<zx_status_t, uint8_t> ret = mock_get_next_capability_.Call(id, start_offset);
    *out_offset = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciGetFirstExtendedCapability(pci_extended_capability_id_t id,
                                                    uint16_t* out_offset) {
    std::tuple<zx_status_t, uint16_t> ret = mock_get_first_extended_capability_.Call(id);
    *out_offset = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciGetNextExtendedCapability(pci_extended_capability_id_t id,
                                                   uint16_t start_offset, uint16_t* out_offset) {
    std::tuple<zx_status_t, uint16_t> ret =
        mock_get_next_extended_capability_.Call(id, start_offset);
    *out_offset = std::get<1>(ret);
    return std::get<0>(ret);
  }

  virtual zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti) {
    std::tuple<zx_status_t, zx::bti> ret = mock_get_bti_.Call(index);
    *out_bti = std::move(std::get<1>(ret));
    return std::get<0>(ret);
  }

  mock_function::MockFunction<std::tuple<zx_status_t, pci_device_info_t>>& mock_get_device_info() {
    return mock_get_device_info_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, pci_bar_t>, uint32_t>& mock_get_bar() {
    return mock_get_bar_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>, bool>& mock_set_bus_mastering() {
    return mock_set_bus_mastering_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>>& mock_reset_device() {
    return mock_reset_device_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>>& mock_ack_interrupt() {
    return mock_ack_interrupt_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, zx::interrupt>, uint32_t>&
  mock_map_interrupt() {
    return mock_map_interrupt_;
  }
  mock_function::MockFunction<std::tuple<pci_interrupt_modes_t>>& mock_get_interrupt_modes() {
    return mock_get_interrupt_modes_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>, pci_interrupt_mode_t, uint32_t>&
  mock_set_interrupt_mode() {
    return mock_set_interrupt_mode_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, uint16_t>& mock_read_config8() {
    return mock_read_config8_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint16_t>, uint16_t>& mock_read_config16() {
    return mock_read_config16_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint32_t>, uint16_t>& mock_read_config32() {
    return mock_read_config32_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>, uint16_t, uint8_t>& mock_write_config8() {
    return mock_write_config8_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>, uint16_t, uint16_t>& mock_write_config16() {
    return mock_write_config16_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t>, uint16_t, uint32_t>& mock_write_config32() {
    return mock_write_config32_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, pci_capability_id_t>&
  mock_get_first_capability() {
    return mock_get_first_capability_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, pci_capability_id_t, uint8_t>&
  mock_get_next_capability() {
    return mock_get_next_capability_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint16_t>, pci_extended_capability_id_t>&
  mock_get_first_extended_capability() {
    return mock_get_first_extended_capability_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, uint16_t>, pci_extended_capability_id_t,
                              uint16_t>&
  mock_get_next_extended_capability() {
    return mock_get_next_extended_capability_;
  }
  mock_function::MockFunction<std::tuple<zx_status_t, zx::bti>, uint32_t>& mock_get_bti() {
    return mock_get_bti_;
  }

 protected:
  mock_function::MockFunction<std::tuple<zx_status_t, pci_device_info_t>> mock_get_device_info_;
  mock_function::MockFunction<std::tuple<zx_status_t, pci_bar_t>, uint32_t> mock_get_bar_;
  mock_function::MockFunction<std::tuple<zx_status_t>, bool> mock_set_bus_mastering_;
  mock_function::MockFunction<std::tuple<zx_status_t>> mock_reset_device_;
  mock_function::MockFunction<std::tuple<zx_status_t>> mock_ack_interrupt_;
  mock_function::MockFunction<std::tuple<zx_status_t, zx::interrupt>, uint32_t> mock_map_interrupt_;
  mock_function::MockFunction<std::tuple<pci_interrupt_modes_t>> mock_get_interrupt_modes_;
  mock_function::MockFunction<std::tuple<zx_status_t>, pci_interrupt_mode_t, uint32_t>
      mock_set_interrupt_mode_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, uint16_t> mock_read_config8_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint16_t>, uint16_t> mock_read_config16_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint32_t>, uint16_t> mock_read_config32_;
  mock_function::MockFunction<std::tuple<zx_status_t>, uint16_t, uint8_t> mock_write_config8_;
  mock_function::MockFunction<std::tuple<zx_status_t>, uint16_t, uint16_t> mock_write_config16_;
  mock_function::MockFunction<std::tuple<zx_status_t>, uint16_t, uint32_t> mock_write_config32_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, pci_capability_id_t>
      mock_get_first_capability_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, pci_capability_id_t, uint8_t>
      mock_get_next_capability_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint16_t>, pci_extended_capability_id_t>
      mock_get_first_extended_capability_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint16_t>, pci_extended_capability_id_t,
                              uint16_t>
      mock_get_next_extended_capability_;
  mock_function::MockFunction<std::tuple<zx_status_t, zx::bti>, uint32_t> mock_get_bti_;

 private:
  const pci_protocol_t proto_;
};

}  // namespace ddk

#endif  // SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_MOCK_H_
