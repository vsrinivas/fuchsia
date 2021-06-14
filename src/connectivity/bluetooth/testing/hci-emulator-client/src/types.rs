// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_test as fidl;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct LegacyAdvertisingState {
    pub enabled: bool,
    pub type_: Option<fidl::LegacyAdvertisingType>,
    // TODO(armansito): Add library types for FIDL Address and AddressType.
    pub address_type: Option<fidl_fuchsia_bluetooth::AddressType>,
    pub interval_min: Option<u16>,
    pub interval_max: Option<u16>,
    pub advertising_data: Option<Vec<u8>>,
    pub scan_response: Option<Vec<u8>>,
}

impl From<fidl::LegacyAdvertisingState> for LegacyAdvertisingState {
    fn from(src: fidl::LegacyAdvertisingState) -> LegacyAdvertisingState {
        LegacyAdvertisingState {
            enabled: src.enabled.unwrap_or(false),
            type_: src.type_,
            address_type: src.address_type,
            interval_min: src.interval_min,
            interval_max: src.interval_max,
            advertising_data: src.advertising_data,
            scan_response: src.scan_response,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ControllerParameters {
    pub local_name: Option<String>,
    pub device_class: Option<fidl_fuchsia_bluetooth::DeviceClass>,
}

impl From<fidl::ControllerParameters> for ControllerParameters {
    fn from(src: fidl::ControllerParameters) -> ControllerParameters {
        ControllerParameters { local_name: src.local_name, device_class: src.device_class }
    }
}
