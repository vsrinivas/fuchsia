// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_set>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace testing {

// FakeDevice is used to emulate remote Bluetooth devices.
class FakeDevice {
 public:
  static constexpr int64_t kDefaultConnectResponseTimeMs = 100;

  // NOTE: Setting |connectable| to true will result in a "Connectable and
  // Scannable Advertisement" (i.e. ADV_IND) even if |scannable| is set to
  // false. This is OK since we use |scannable| to drive the receipt of Scan
  // Response PDUs: we use this to test the condition in which the advertisement
  // is scannable but the host never receives a scan response.
  explicit FakeDevice(const common::DeviceAddress& address,
                      bool connectable = true,
                      bool scannable = true);

  void SetAdvertisingData(const common::ByteBuffer& data);

  // |should_batch_reports| indicates to the FakeController that the SCAN_IND
  // report should be included in the same HCI LE Advertising Report Event
  // payload that includes the original advertising data (see comments for
  // should_batch_reports()).
  void SetScanResponse(bool should_batch_reports,
                       const common::ByteBuffer& data);

  // Generates and returns a LE Advertising Report Event payload. If
  // |include_scan_rsp| is true, then the returned PDU will contain two reports
  // including the SCAN_IND report.
  common::DynamicByteBuffer CreateAdvertisingReportEvent(
      bool include_scan_rsp) const;

  // Generates a LE Advertising Report Event payload containing the scan
  // response.
  common::DynamicByteBuffer CreateScanResponseReportEvent() const;

  const common::DeviceAddress& address() const { return address_; }

  // Indicates whether or not this device should include the scan response and
  // the advertising data in the same HCI LE Advertising Report Event. This is
  // used to test that the host stack can correctly consolidate advertising
  // reports when the payloads are spread across events and when they are
  // batched together in the same event.
  //
  // This isn't used by FakeDevice directly to generated batched reports. Rather
  // it is a hint to the corresponding FakeController which decides how the
  // reports should be generated.
  bool should_batch_reports() const { return should_batch_reports_; }

  // Returns true if this device is scannable. We use this to tell
  // FakeController whether or not it should send scan response PDUs.
  bool scannable() const { return scannable_; }

  bool connectable() const { return connectable_; }

  bool connected() const { return connected_; }
  void set_connected(bool connected) { connected_ = connected; }

  const hci::Connection::LowEnergyParameters& le_params() const {
    return le_params_;
  }
  void set_le_params(const hci::Connection::LowEnergyParameters& value) {
    le_params_ = value;
  }

  // The response status that will be returned when this device receives a LE
  // Create Connection command.
  hci::Status connect_response() const { return connect_response_; }
  void set_connect_response(hci::Status response) {
    connect_response_ = response;
  }

  // The status that will be returned in the Command Status event in response to
  // a LE Create Connection command. If this is set to anything other than
  // hci::Status::kSuccess, then connect_response() will have no effect.
  hci::Status connect_status() const { return connect_status_; }
  void set_connect_status(hci::Status status) { connect_status_ = status; }

  int64_t connect_response_period_ms() const { return connect_rsp_ms_; }
  void set_connect_response_period_ms(int64_t value) {
    connect_rsp_ms_ = value;
  }

  void AddLink(hci::ConnectionHandle handle);
  void RemoveLink(hci::ConnectionHandle handle);
  bool HasLink(hci::ConnectionHandle handle) const;

  using HandleSet = std::unordered_set<hci::ConnectionHandle>;

  // Marks this device as disconnected. Clears and returns all logical link
  // handles.
  HandleSet Disconnect();

 private:
  void WriteScanResponseReport(hci::LEAdvertisingReportData* report) const;

  common::DeviceAddress address_;
  bool connected_;
  bool connectable_;
  bool scannable_;

  hci::Status connect_status_;
  hci::Status connect_response_;
  int64_t connect_rsp_ms_;

  hci::Connection::LowEnergyParameters le_params_;

  bool should_batch_reports_;
  common::DynamicByteBuffer adv_data_;
  common::DynamicByteBuffer scan_rsp_;

  // Open connection handles.
  HandleSet logical_links_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeDevice);
};

}  // namespace testing
}  // namespace bluetooth
