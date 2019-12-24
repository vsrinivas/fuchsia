// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt::{self, Debug, Formatter},
    sync::Arc,
};

/// Asynchronous extensions to Expectation Predicates
pub mod asynchronous;

/// A Boolean predicate on type `T`. Predicate functions are a boolean algebra
/// just as raw boolean values are; they an be ANDed, ORed, NOTed. This allows
/// a clear and concise language for declaring test expectations.
pub struct Predicate<T> {
    inner: Arc<dyn Fn(&T) -> bool + Send + Sync + 'static>,
    /// A descriptive piece of text used for debug printing via `{:?}`
    description: String,
}

impl<T> Clone for Predicate<T> {
    fn clone(&self) -> Predicate<T> {
        Predicate { inner: self.inner.clone(), description: self.description.clone() }
    }
}

impl<T> Debug for Predicate<T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.description)
    }
}

impl<T: 'static> Predicate<T> {
    pub fn satisfied(&self, t: &T) -> bool {
        (self.inner)(t)
    }
    pub fn or(self, rhs: Predicate<T>) -> Predicate<T> {
        let description = format!("({}) OR ({})", self.description, rhs.description);
        Predicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) || (rhs.inner)(t) }),
            description,
        }
    }
    pub fn and(self, rhs: Predicate<T>) -> Predicate<T> {
        let description = format!("({}) AND ({})", self.description, rhs.description);
        Predicate {
            inner: Arc::new(move |t: &T| -> bool { (self.inner)(t) && (rhs.inner)(t) }),
            description,
        }
    }
    pub fn not(self) -> Predicate<T> {
        let description = format!("NOT ({})", self.description);
        Predicate { inner: Arc::new(move |t: &T| -> bool { !(self.inner)(t) }), description }
    }

    pub fn new<F>(f: F, label: Option<&str>) -> Predicate<T>
    where
        F: Fn(&T) -> bool + Send + Sync + 'static,
    {
        Predicate {
            inner: Arc::new(f),
            description: label.unwrap_or("<Unrepresentable Predicate>").to_string(),
        }
    }

    pub fn describe(&self) -> String {
        self.description.clone()
    }
}

/// Expectations for Bluetooth Peers (i.e. Remote Devices)
pub mod peer {
    use super::Predicate;
    use fidl_fuchsia_bluetooth_control::{RemoteDevice, TechnologyType};

    pub fn name(expected_name: &str) -> Predicate<RemoteDevice> {
        let name = Some(expected_name.to_string());
        Predicate::<RemoteDevice>::new(
            move |peer| peer.name == name,
            Some(&format!("name == {}", expected_name)),
        )
    }
    pub fn identifier(expected_ident: &str) -> Predicate<RemoteDevice> {
        let identifier = expected_ident.to_string();
        Predicate::<RemoteDevice>::new(
            move |peer| peer.identifier == identifier,
            Some(&format!("identifier == {}", expected_ident)),
        )
    }
    pub fn address(expected_address: &str) -> Predicate<RemoteDevice> {
        let address = expected_address.to_string();
        Predicate::<RemoteDevice>::new(
            move |peer| peer.address == address,
            Some(&format!("address == {}", expected_address)),
        )
    }
    pub fn technology(tech: TechnologyType) -> Predicate<RemoteDevice> {
        Predicate::<RemoteDevice>::new(
            move |peer| peer.technology == tech,
            Some(&format!("technology == {:?}", tech)),
        )
    }
    pub fn connected(connected: bool) -> Predicate<RemoteDevice> {
        Predicate::<RemoteDevice>::new(
            move |peer| peer.connected == connected,
            Some(&format!("connected == {}", connected)),
        )
    }
    pub fn bonded(bonded: bool) -> Predicate<RemoteDevice> {
        Predicate::<RemoteDevice>::new(
            move |peer| peer.bonded == bonded,
            Some(&format!("bonded == {}", bonded)),
        )
    }
}

/// Expectations for the Bluetooth Host Driver (bt-host)
pub mod host_driver {
    use super::Predicate;
    use crate::types::HostInfo;

    pub fn name(expected_name: &str) -> Predicate<HostInfo> {
        let name = Some(expected_name.to_string());
        Predicate::<HostInfo>::new(
            move |host_driver| host_driver.local_name == name,
            Some(&format!("name == {}", expected_name)),
        )
    }
    pub fn discovering(discovering: bool) -> Predicate<HostInfo> {
        Predicate::<HostInfo>::new(
            move |host_driver| host_driver.discovering == discovering,
            Some(&format!("discovering == {}", discovering)),
        )
    }
    pub fn discoverable(discoverable: bool) -> Predicate<HostInfo> {
        Predicate::<HostInfo>::new(
            move |host_driver| host_driver.discoverable == discoverable,
            Some(&format!("discoverable == {}", discoverable)),
        )
    }
}

#[cfg(test)]
mod test {
    use crate::expectation::*;
    use fidl_fuchsia_bluetooth_control::{Appearance, RemoteDevice, TechnologyType};

    const TEST_PEER_NAME: &'static str = "TestPeer";
    const TEST_PEER_ADDRESS: &'static str = "00:00:00:00:00:01";
    const INCORRECT_PEER_NAME: &'static str = "IncorrectPeer";
    const INCORRECT_PEER_ADDRESS: &'static str = "00:00:00:00:00:02";

    fn correct_name() -> Predicate<RemoteDevice> {
        peer::name(TEST_PEER_NAME)
    }
    fn incorrect_name() -> Predicate<RemoteDevice> {
        peer::name(INCORRECT_PEER_NAME)
    }
    fn correct_address() -> Predicate<RemoteDevice> {
        peer::address(TEST_PEER_ADDRESS)
    }
    fn incorrect_address() -> Predicate<RemoteDevice> {
        peer::address(INCORRECT_PEER_ADDRESS)
    }

    fn test_peer() -> RemoteDevice {
        RemoteDevice {
            name: Some(TEST_PEER_NAME.into()),
            address: TEST_PEER_ADDRESS.into(),
            technology: TechnologyType::LowEnergy,
            connected: false,
            bonded: false,
            appearance: Appearance::Unknown,
            identifier: "".into(),
            rssi: None,
            tx_power: None,
            service_uuids: vec![],
        }
    }

    #[test]
    fn simple_predicate_succeeds() {
        let predicate = Predicate::<RemoteDevice>::new(
            move |peer| peer.name == Some(TEST_PEER_NAME.into()),
            None,
        );
        assert!(predicate.satisfied(&test_peer()));
    }
    #[test]
    fn simple_incorrect_predicate_error() {
        let predicate = Predicate::<RemoteDevice>::new(
            move |peer| peer.name == Some("INCORRECT_NAME".into()),
            None,
        );
        assert!(!predicate.satisfied(&test_peer()));
    }

    #[test]
    fn predicate_and_both_true_succeeds() {
        let predicate = correct_name().and(correct_address());
        assert!(predicate.satisfied(&test_peer()));
    }

    #[test]
    fn predicate_and_one_or_more_false_fails() {
        let predicate = correct_name().and(incorrect_address());
        assert!(!predicate.satisfied(&test_peer()));

        let predicate = incorrect_name().and(correct_address());
        assert!(!predicate.satisfied(&test_peer()));

        let predicate = incorrect_name().and(incorrect_address());
        assert!(!predicate.satisfied(&test_peer()));
    }

    #[test]
    fn predicate_or_both_false_fails() {
        let predicate = incorrect_name().or(incorrect_address());
        assert!(!predicate.satisfied(&test_peer()));
    }

    #[test]
    fn predicate_or_one_or_more_true_succeeds() {
        let predicate = correct_name().or(correct_address());
        assert!(predicate.satisfied(&test_peer()));

        let predicate = incorrect_name().or(correct_address());
        assert!(predicate.satisfied(&test_peer()));

        let predicate = correct_name().or(incorrect_address());
        assert!(predicate.satisfied(&test_peer()));
    }

    #[test]
    fn predicate_not_incorrect_succeeds() {
        let predicate = incorrect_name().not();
        assert!(predicate.satisfied(&test_peer()));
    }

    #[test]
    fn predicate_not_correct_fails() {
        let predicate = correct_name().not();
        assert!(!predicate.satisfied(&test_peer()));
    }
}
