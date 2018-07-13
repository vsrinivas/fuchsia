// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/lmp_feature_set.h"
#include "garnet/drivers/bluetooth/lib/sm/pairing_state.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gap {

class RemoteDeviceCache;

// Represents a remote Bluetooth device that is known to the current system due
// to discovery and/or connection and bonding procedures. These devices can be
// LE-only, Classic-only, or dual-mode.
//
// Instances should not be created directly and must be obtained via a
// RemoteDeviceCache.
class RemoteDevice final {
 public:
  // TODO(armansito): Probably keep separate states for LE and BR/EDR.
  enum class ConnectionState {
    // No link exists between the local adapter and this device.
    kNotConnected,

    // The device is currently establishing a link or performing service
    // discovery or encryption setup. In this state, a link may have been
    // established but it is not ready to use yet.
    kInitializing,

    // Link setup, service discovery, and any encryption setup has completed
    kConnected,

    // Bonding procedures are in progress
    kBonding,

    // Bonded
    kBonded
  };

  // 128-bit UUID that uniquely identifies this device on this system.
  const std::string& identifier() const { return identifier_; }

  // The Bluetooth technologies that are supported by this device.
  TechnologyType technology() const { return technology_; }

  // The known device address of this device.
  // TODO(armansito):
  //   - For paired devices this should return the identity address.
  //   - For temporary devices this is the address that was seen in the
  //     advertisement.
  //   - For classic devices this the BD_ADDR.
  const common::DeviceAddress& address() const { return address_; }

  // Returns true if this is a connectable device.
  bool connectable() const { return connectable_; }

  // Returns the advertising data for this device (including any scan response
  // data).
  const common::BufferView advertising_data() const {
    return advertising_data_buffer_.view(0, advertising_data_length_);
  }

  // Returns the most recently observed RSSI for this remote device. Returns
  // hci::kRSSIInvalid if the value is unknown.
  int8_t rssi() const { return rssi_; }

  // Updates the advertising and scan response data for this device.
  // |rssi| corresponds to the most recent advertisement RSSI.
  // |advertising_data| should include any scan response data.
  void SetLEAdvertisingData(int8_t rssi,
                            const common::ByteBuffer& advertising_data);

  // Updates the device based on extended inquiry response data.
  // |bytes| contains the data from an ExtendedInquiryResponse event.
  void SetExtendedInquiryResponse(const common::ByteBuffer& bytes);

  // Updates the device based on inquiry result data obtained through a
  // BR/EDR discovery procedure.
  void SetInquiryData(const hci::InquiryResult& result);
  void SetInquiryData(const hci::InquiryResultRSSI& result);
  void SetInquiryData(const hci::ExtendedInquiryResultEventParams& result);

  // Updates the name of this device.
  // If Advertising Data has been set, this must match any local name advertised
  // in that data. (Bluetooth 5.0, Vol 2 E 6.23)
  void SetName(const std::string& name);

  // Gets the user-friendly name of the device, if it's known.
  // This can be set by LE Advertising data as well as by SetName.
  const common::Optional<std::string>& name() const { return name_; }

  // Returns the most recently used connection parameters for this device.
  // Returns nullptr if these values are unknown.
  const hci::LEConnectionParameters* le_connection_params() const {
    return le_conn_params_.value();
  }
  void set_le_connection_params(const hci::LEConnectionParameters& params) {
    le_conn_params_ = params;
  }

  void set_ltk(const sm::LTK& key) { ltk_ = key; }

  const common::Optional<sm::LTK> ltk() const { return ltk_; }

  // Returns this device's preferred connection parameters, if known. LE
  // peripherals report their preferred connection parameters using one of the
  // GAP Connection Parameter Update procedures (e.g. L2CAP, Advertising, LL).
  const hci::LEPreferredConnectionParameters* le_preferred_connection_params()
      const {
    return le_preferred_conn_params_.value();
  }
  void set_le_preferred_connection_params(
      const hci::LEPreferredConnectionParameters& params) {
    le_preferred_conn_params_ = params;
  }

  // The current LE connection state of this RemoteDevice.
  ConnectionState le_connection_state() const { return le_connection_state_; }
  void SetLEConnectionState(ConnectionState state);

  // The current BR/EDR connection state of this RemoteDevice.
  ConnectionState bredr_connection_state() const {
    return bredr_connection_state_;
  }
  void SetBREDRConnectionState(ConnectionState state);

  // A temporary device is one that is never persisted, such as
  //
  //   1. A device that has never been connected to;
  //   2. A device that was connected but uses a Non-resolvable Private Address.
  //   3. A device that was connected, uses a Resolvable Private Address, but
  //      the local host has no Identity Resolving Key for it.
  //
  // All other devices can be considered bonded.
  bool temporary() const { return temporary_; }

  // Marks this device as non-temporary. This operation may fail due to one of
  // the conditions described above the |temporary()| method.
  //
  // TODO(armansito): Replace this with something more sophisticated when we
  // implement bonding procedures. This method is here to remind us that these
  // conditions are subtle and not fully supported yet.
  bool TryMakeNonTemporary();

  // Returns a string representation of this device.
  std::string ToString() const;

  // Returns the device class of this device, if it is known.
  const common::Optional<common::DeviceClass>& device_class() const {
    return device_class_;
  }

  // Returns the page scan repettion mode of this device, if known.
  const common::Optional<hci::PageScanRepetitionMode>&
  page_scan_repetition_mode() const {
    return page_scan_repetition_mode_;
  }

  // Returns the clock offset reported by the device, if known and valid.
  // The clock offset will have the highest-order bit set, and the rest
  // represent bits 16-2 of CLKNslave-CLK.
  const common::Optional<uint16_t>& clock_offset() const {
    return clock_offset_;
  }

  // Returns the set of features of this device.
  const hci::LMPFeatureSet& features() const { return lmp_features_; }

  void SetFeaturePage(size_t page, uint64_t features) {
    lmp_features_.SetPage(page, features);
  }

  void set_version(hci::HCIVersion version, uint16_t manufacturer,
                   uint16_t subversion) {
    lmp_version_ = version;
    lmp_manufacturer_ = manufacturer;
    lmp_subversion_ = subversion;
  }

  const common::Optional<hci::HCIVersion>& version() const {
    return lmp_version_;
  }

 private:
  friend class RemoteDeviceCache;
  using DeviceCallback = fit::function<void(const RemoteDevice&)>;

  // TODO(armansito): Add constructor from persistent storage format.

  // Caller must ensure that both callbacks are non-empty.
  // Note that the ctor is only intended for use by RemoteDeviceCache.
  // Expanding access would a) violate the constraint that all RemoteDevices
  // are created through a RemoteDeviceCache, and b) introduce lifetime issues
  // (do the callbacks outlive |this|?).
  RemoteDevice(DeviceCallback notify_listeners_callback,
               DeviceCallback update_expiry_callback,
               const std::string& identifier,
               const common::DeviceAddress& address, bool connectable);

  DeviceCallback notify_listeners_callback_;
  DeviceCallback update_expiry_callback_;
  const std::string identifier_;
  const common::DeviceAddress address_;
  const TechnologyType technology_;
  ConnectionState le_connection_state_;
  ConnectionState bredr_connection_state_;
  common::Optional<sm::LTK> ltk_;
  common::Optional<std::string> name_;
  bool connectable_;
  bool temporary_;
  int8_t rssi_;

  common::Optional<common::DeviceClass> device_class_;
  common::Optional<hci::PageScanRepetitionMode> page_scan_repetition_mode_;
  common::Optional<uint16_t> clock_offset_;
  common::Optional<hci::HCIVersion> lmp_version_;
  uint16_t lmp_manufacturer_;
  uint16_t lmp_subversion_;
  hci::LMPFeatureSet lmp_features_;

  // TODO(armansito): Store device name and remote features.
  // TODO(armansito): Store discovered service UUIDs.
  // TODO(armansito): Store an AdvertisingData structure rather than the raw
  // payload.
  size_t advertising_data_length_;
  common::DynamicByteBuffer advertising_data_buffer_;

  // TODO(jamuraa): Parse more of the Extended Inquiry Response fields
  common::DynamicByteBuffer extended_inquiry_response_;

  // Most recently used LE connection parameters. Has no value if this device
  // has never been connected.
  common::Optional<hci::LEConnectionParameters> le_conn_params_;

  // Preferred LE connection parameters as reported by this device. Has no value
  // if this parameter is unknown.
  // TODO(armansito): Add a method for storing the preferred parameters.
  common::Optional<hci::LEPreferredConnectionParameters>
      le_preferred_conn_params_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDevice);
};

}  // namespace gap
}  // namespace btlib
