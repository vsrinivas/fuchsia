// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_control as fidl_control,
    fuchsia_bluetooth::{
        assigned_numbers::find_service_uuid,
        types::Bool,
        util::{clone_bt_fidl_bool, clone_host_state},
    },
    std::fmt::{self, Write},
};

pub struct AdapterInfo(fidl_control::AdapterInfo);

impl From<fidl_control::AdapterInfo> for AdapterInfo {
    fn from(b: fidl_control::AdapterInfo) -> AdapterInfo {
        AdapterInfo(b)
    }
}
impl Into<fidl_control::AdapterInfo> for AdapterInfo {
    fn into(self) -> fidl_control::AdapterInfo {
        self.0
    }
}

impl fmt::Display for AdapterInfo {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        writeln!(fmt, "Adapter:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.0.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.0.address)?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.0.technology)?;
        if let Some(ref state) = self.0.state {
            for line in AdapterState::from(clone_host_state(state)).to_string().lines() {
                writeln!(fmt, "\t{}", line)?;
            }
        }
        Ok(())
    }
}

pub struct AdapterState(fidl_control::AdapterState);

impl From<fidl_control::AdapterState> for AdapterState {
    fn from(b: fidl_control::AdapterState) -> AdapterState {
        AdapterState(b)
    }
}
impl Into<fidl_control::AdapterState> for AdapterState {
    fn into(self) -> fidl_control::AdapterState {
        self.0
    }
}

impl fmt::Display for AdapterState {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        if let Some(ref local_name) = self.0.local_name {
            writeln!(fmt, "Local Name:\t{}", local_name)?;
        }
        if let Some(ref discoverable) = self.0.discoverable {
            writeln!(fmt, "Discoverable:\t{}", Bool::from(clone_bt_fidl_bool(discoverable)))?;
        }
        if let Some(ref discovering) = self.0.discovering {
            writeln!(fmt, "Discovering:\t{}", Bool::from(clone_bt_fidl_bool(discovering)))?;
        }
        writeln!(fmt, "Local UUIDs:\t{:#?}", self.0.local_service_uuids)
    }
}

pub struct RemoteDevice(pub fidl_control::RemoteDevice);

impl RemoteDevice {
    /// Construct a concise summary of a `RemoteDevice`'s information.
    pub fn summary(&self) -> String {
        let mut msg = String::new();
        write!(msg, "Device {}", self.0.address).expect("Error occurred writing to String");
        if let Some(rssi) = &self.0.rssi {
            write!(msg, ", RSSI {}", rssi.value).expect("Error occurred writing to String");
        }
        if let Some(name) = &self.0.name {
            write!(msg, ", Name {}", name).expect("Error occurred writing to String");
        }
        if self.0.bonded {
            write!(msg, " [bonded]").expect("Error occurred writing to String");
        }
        if self.0.connected {
            write!(msg, " [connected]").expect("Error occurred writing to String");
        }
        msg
    }
}

impl From<fidl_control::RemoteDevice> for RemoteDevice {
    fn from(b: fidl_control::RemoteDevice) -> RemoteDevice {
        RemoteDevice(b)
    }
}
impl Into<fidl_control::RemoteDevice> for RemoteDevice {
    fn into(self) -> fidl_control::RemoteDevice {
        self.0
    }
}

impl fmt::Display for RemoteDevice {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        writeln!(fmt, "Remote Device:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.0.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.0.address)?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.0.technology)?;
        if let Some(name) = &self.0.name {
            writeln!(fmt, "\tName:\t\t{}", name)?;
        }
        writeln!(fmt, "\tAppearance:\t{:?}", self.0.appearance)?;
        if let Some(rssi) = &self.0.rssi {
            writeln!(fmt, "\tRSSI:\t\t{}", rssi.value)?;
        }
        if let Some(tx_power) = &self.0.tx_power {
            writeln!(fmt, "\tTX Power:\t{}", tx_power.value)?;
        }
        writeln!(fmt, "\tConnected:\t{}", self.0.connected)?;
        writeln!(fmt, "\tBonded:\t\t{}", self.0.bonded)?;
        writeln!(
            fmt,
            "\tServices:\t{:?}",
            self.0
                .service_uuids
                .iter()
                .map(|uuid| find_service_uuid(uuid).map(|an| an.name).unwrap_or(uuid))
                .collect::<Vec<_>>(),
        )?;
        Ok(())
    }
}
