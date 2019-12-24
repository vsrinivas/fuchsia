// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_control as control,
    fuchsia_inspect::{self as inspect, Property},
    std::convert::TryFrom,
    std::fmt,
};

use crate::{
    inspect::{DebugExt, InspectData, Inspectable, IsInspectable, ToProperty},
    types::Address,
};

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

impl From<&control::AdapterState> for AdapterState {
    fn from(s: &control::AdapterState) -> AdapterState {
        AdapterState {
            local_name: s.local_name.clone(),
            discoverable: s.discoverable.as_ref().map(|d| d.value),
            discovering: s.discovering.as_ref().map(|d| d.value),
            local_service_uuids: s.local_service_uuids.clone(),
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
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
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
    pub address: Address,
    pub state: Option<AdapterState>,
}

impl TryFrom<control::AdapterInfo> for AdapterInfo {
    type Error = anyhow::Error;
    fn try_from(a: control::AdapterInfo) -> Result<AdapterInfo, Self::Error> {
        Ok(AdapterInfo {
            identifier: a.identifier,
            technology: a.technology,
            address: Address::public_from_str(&a.address)?,
            state: a.state.map(|s| AdapterState::from(*s)),
        })
    }
}

impl From<AdapterInfo> for control::AdapterInfo {
    fn from(a: AdapterInfo) -> control::AdapterInfo {
        control::AdapterInfo {
            identifier: a.identifier,
            technology: a.technology,
            address: a.address.to_string(),
            state: a.state.map(control::AdapterState::from).map(Box::new),
        }
    }
}

impl fmt::Display for AdapterInfo {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(fmt, "Adapter:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.address.to_string())?;
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
        address: Address,
        state: Option<AdapterState>,
    ) -> AdapterInfo {
        AdapterInfo { identifier, technology, address, state }
    }
    pub fn update_state(&mut self, state: Option<AdapterState>) {
        if let Some(current) = &mut self.state {
            if let Some(delta) = state {
                update_if_specified(&mut current.local_name, delta.local_name);
                update_if_specified(&mut current.discoverable, delta.discoverable);
                update_if_specified(&mut current.discovering, delta.discovering);
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

impl Inspectable<AdapterInfo> {
    pub fn update_state(&mut self, state: Option<AdapterState>) {
        self.inspect.update_state(&state);
        self.inner.update_state(state);
    }
}

impl IsInspectable for AdapterInfo {
    type I = AdapterInfoInspect;
}

impl InspectData<AdapterInfo> for AdapterInfoInspect {
    fn new(a: &AdapterInfo, inspect: inspect::Node) -> AdapterInfoInspect {
        let _discoverable = None;
        let _discovering = None;
        let _local_service_uuids = None;

        let mut i = AdapterInfoInspect {
            _identifier: inspect.create_string("identifier", &a.identifier),
            _technology: inspect.create_string("technology", a.technology.debug()),
            _discoverable,
            _discovering,
            _local_service_uuids,
            _inspect: inspect,
        };
        i.update_state(&a.state);
        i
    }
}

pub struct AdapterInfoInspect {
    _inspect: inspect::Node,
    _identifier: inspect::StringProperty,
    _technology: inspect::StringProperty,
    _discoverable: Option<inspect::UintProperty>,
    _discovering: Option<inspect::UintProperty>,
    _local_service_uuids: Option<inspect::StringProperty>,
}

impl AdapterInfoInspect {
    /// Update the inspect fields that provide visibility into `AdapterState`. Check if the
    /// `AdapterState` is set, updating the appropriate fields to `Some` or `None` values.
    fn update_state(&mut self, state: &Option<AdapterState>) {
        if let Some(state) = &state {
            if let Some(d) = &mut self._discoverable {
                d.set(state.discoverable.to_property());
            } else {
                let value = state.discoverable.to_property();
                self._discoverable = Some(self._inspect.create_uint("discoverable", value));
            }
            if let Some(d) = &mut self._discovering {
                d.set(state.discovering.to_property());
            } else {
                let value = state.discovering.to_property();
                self._discovering = Some(self._inspect.create_uint("discovering", value));
            }
            if let Some(s) = &mut self._local_service_uuids {
                s.set(&state.local_service_uuids.to_property());
            } else {
                let value = state.local_service_uuids.to_property();
                self._local_service_uuids =
                    Some(self._inspect.create_string("local_services_uuids", value));
            }
        } else {
            self._discoverable = None;
            self._discovering = None;
            self._local_service_uuids = None;
        }
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
            address: Address::Public([0, 0, 0, 0, 0, 0]),
            state,
        }
    }

    #[test]
    fn adapter_info_conversion() {
        let expected = adapter_info();
        let round_trip_conversion =
            AdapterInfo::try_from(control::AdapterInfo::from(adapter_info()))
                .map_err(|e| e.to_string());
        assert_eq!(Ok(expected), round_trip_conversion);
    }
}
