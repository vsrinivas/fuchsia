use bluetooth::types::Bool;
use bluetooth::util::clone_adapter_state;
use bluetooth::util::clone_bt_fidl_bool;
use fidl_bluetooth_control;
use std::fmt;

pub struct AdapterInfo(fidl_bluetooth_control::AdapterInfo);

impl From<fidl_bluetooth_control::AdapterInfo> for AdapterInfo {
    fn from(b: fidl_bluetooth_control::AdapterInfo) -> AdapterInfo {
        AdapterInfo(b)
    }
}
impl Into<fidl_bluetooth_control::AdapterInfo> for AdapterInfo {
    fn into(self) -> fidl_bluetooth_control::AdapterInfo {
        self.0
    }
}

impl fmt::Display for AdapterInfo {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        let _ = writeln!(fmt, "Adapter:");
        let _ = writeln!(fmt, "\tIdentifier:\t{}", self.0.identifier);
        let _ = writeln!(fmt, "\tAddress:\t{}", self.0.address);
        let _ = writeln!(fmt, "\tTechnology:\t{:?}", self.0.technology);
        if let Some(ref state) = self.0.state {
            for line in AdapterState::from(clone_adapter_state(state))
                .to_string()
                .lines()
            {
                let _ = writeln!(fmt, "\t{}", line);
            }
        }
        Ok(())
    }
}

pub struct AdapterState(fidl_bluetooth_control::AdapterState);

impl From<fidl_bluetooth_control::AdapterState> for AdapterState {
    fn from(b: fidl_bluetooth_control::AdapterState) -> AdapterState {
        AdapterState(b)
    }
}
impl Into<fidl_bluetooth_control::AdapterState> for AdapterState {
    fn into(self) -> fidl_bluetooth_control::AdapterState {
        self.0
    }
}

impl fmt::Display for AdapterState {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        if let Some(ref local_name) = self.0.local_name {
            let _ = writeln!(fmt, "Local Name:\t{}", local_name);
        }
        if let Some(ref discoverable) = self.0.discoverable {
            let _ = writeln!(
                fmt,
                "Discoverable:\t{}",
                Bool::from(clone_bt_fidl_bool(discoverable))
            );
        }
        if let Some(ref discovering) = self.0.discovering {
            let _ = writeln!(
                fmt,
                "Discovering:\t{}",
                Bool::from(clone_bt_fidl_bool(discovering))
            );
        }
        writeln!(fmt, "Local UUIDs:\t{:#?}", self.0.local_service_uuids)
    }
}
