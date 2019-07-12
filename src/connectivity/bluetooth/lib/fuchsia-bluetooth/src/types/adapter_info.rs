// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_bluetooth_control as control, std::fmt};

/// `AdapterState` is derived from deltas sent over the Control fidl protocol.
/// Fields that are set to `None` indicate that the field is in an unknown state because the host
/// has not sent a delta specifying a value for that field.
#[derive(Clone, Debug, PartialEq)]
pub struct AdapterState {
    pub local_name: Option<String>,
    pub discoverable: Option<bool>,
    pub discovering: Option<bool>,
    pub local_service_uuids: Option<Vec<String>>,
}

impl From<control::AdapterState> for AdapterState {
    fn from(s: control::AdapterState) -> AdapterState {
        AdapterState {
            local_name: s.local_name,
            discoverable: s.discoverable.map(|d| d.value),
            discovering: s.discovering.map(|d| d.value),
            local_service_uuids: s.local_service_uuids,
        }
    }
}

impl From<AdapterState> for control::AdapterState {
    fn from(s: AdapterState) -> control::AdapterState {
        control::AdapterState {
            local_name: s.local_name,
            discoverable: s
                .discoverable
                .map(|d| Box::new(fidl_fuchsia_bluetooth::Bool { value: d })),
            discovering: s.discovering.map(|d| Box::new(fidl_fuchsia_bluetooth::Bool { value: d })),
            local_service_uuids: s.local_service_uuids,
        }
    }
}

impl fmt::Display for AdapterState {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        if let Some(local_name) = &self.local_name {
            writeln!(fmt, "Local Name:\t{}", local_name)?;
        }
        if let Some(discoverable) = &self.discoverable {
            writeln!(fmt, "Discoverable:\t{}", discoverable)?;
        }
        if let Some(discovering) = &self.discovering {
            writeln!(fmt, "Discovering:\t{}", discovering)?;
        }
        writeln!(fmt, "Local UUIDs:\t{:#?}", self.local_service_uuids)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct AdapterInfo {
    pub identifier: String,
    pub technology: control::TechnologyType,
    pub address: String,
    pub state: Option<AdapterState>,
}

impl From<control::AdapterInfo> for AdapterInfo {
    fn from(a: control::AdapterInfo) -> AdapterInfo {
        AdapterInfo {
            identifier: a.identifier,
            technology: a.technology,
            address: a.address,
            state: a.state.map(|s| AdapterState::from(*s)),
        }
    }
}

impl From<AdapterInfo> for control::AdapterInfo {
    fn from(a: AdapterInfo) -> control::AdapterInfo {
        control::AdapterInfo {
            identifier: a.identifier,
            technology: a.technology,
            address: a.address,
            state: a.state.map(control::AdapterState::from).map(Box::new),
        }
    }
}

impl fmt::Display for AdapterInfo {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        writeln!(fmt, "Adapter:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.address)?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.technology)?;
        if let Some(state) = &self.state {
            for line in AdapterState::from(state.clone()).to_string().lines() {
                writeln!(fmt, "\t{}", line)?;
            }
        }
        Ok(())
    }
}

impl AdapterInfo {
    pub fn new(
        identifier: String,
        technology: control::TechnologyType,
        address: String,
        state: Option<AdapterState>,
    ) -> AdapterInfo {
        AdapterInfo { identifier, technology, address, state }
    }
    pub fn update_state(&mut self, state: Option<control::AdapterState>) {
        if let Some(current) = &mut self.state {
            if let Some(delta) = state {
                update_if_specified(&mut current.local_name, delta.local_name);
                update_if_specified(&mut current.discoverable, delta.discoverable.map(|b| b.value));
                update_if_specified(&mut current.discovering, delta.discovering.map(|b| b.value));
                update_if_specified(&mut current.local_service_uuids, delta.local_service_uuids);
            }
        } else {
            self.state = state.map(AdapterState::from);
        }
    }
}

/// Update the `dst` value if the `src` value is `Some` value.
fn update_if_specified<T>(dst: &mut Option<T>, src: Option<T>) {
    if let Some(value) = src {
        dst.replace(value);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn adapter_info() -> AdapterInfo {
        let state = Some(AdapterState {
            local_name: Some("local_name".into()),
            discoverable: Some(true),
            discovering: Some(false),
            local_service_uuids: Some(vec!["uuid1".into(), "uuid2".into()]),
        });
        AdapterInfo {
            identifier: "id".into(),
            technology: control::TechnologyType::DualMode,
            address: "address".into(),
            state,
        }
    }

    #[test]
    fn adapter_info_conversion() {
        let expected = adapter_info();
        let round_trip_conversion = AdapterInfo::from(control::AdapterInfo::from(adapter_info()));
        assert_eq!(expected, round_trip_conversion);
    }
}
