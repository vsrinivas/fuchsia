// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_bluetooth_sys::TechnologyType, std::collections::HashMap};

use crate::{
    assert_satisfies,
    expectation::{Predicate as P, *},
    over,
    types::{Address, Peer, PeerId},
};

const TEST_PEER_NAME: &'static str = "TestPeer";
const INCORRECT_PEER_NAME: &'static str = "IncorrectPeer";
const TEST_PEER_ADDRESS: [u8; 6] = [1, 0, 0, 0, 0, 0];
const INCORRECT_PEER_ADDRESS: [u8; 6] = [2, 0, 0, 0, 0, 0];

fn correct_name() -> Predicate<Peer> {
    peer::name(TEST_PEER_NAME)
}
fn incorrect_name() -> Predicate<Peer> {
    peer::name(INCORRECT_PEER_NAME)
}
fn correct_address() -> Predicate<Peer> {
    peer::address(Address::Public(TEST_PEER_ADDRESS))
}
fn incorrect_address() -> Predicate<Peer> {
    peer::address(Address::Public(INCORRECT_PEER_ADDRESS))
}

fn test_peer() -> Peer {
    Peer {
        name: Some(TEST_PEER_NAME.into()),
        address: Address::Public(TEST_PEER_ADDRESS),
        technology: TechnologyType::LowEnergy,
        connected: false,
        bonded: false,
        appearance: None,
        id: PeerId(1),
        rssi: None,
        tx_power: None,
        device_class: None,
        le_services: vec![],
        bredr_services: vec![],
    }
}

#[test]
fn test() -> Result<(), DebugString> {
    correct_name().assert_satisfied(&test_peer())
}

#[test]
fn simple_predicate_succeeds() {
    let predicate =
        P::equal(Some(TEST_PEER_NAME.to_string())).over(|dev: &Peer| &dev.name, ".name");
    assert!(predicate.satisfied(&test_peer()));
}
#[test]
fn simple_incorrect_predicate_fail() {
    let predicate =
        P::equal(Some(INCORRECT_PEER_NAME.to_string())).over(|dev: &Peer| &dev.name, ".name");
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
    assert_satisfies!(&test_peer(), predicate);

    let predicate = incorrect_name().or(correct_address());
    assert_satisfies!(&test_peer(), predicate);

    let predicate = correct_name().or(incorrect_address());
    assert_satisfies!(&test_peer(), predicate);
}

#[test]
fn predicate_not_incorrect_succeeds() {
    let predicate = incorrect_name().not();
    assert_satisfies!(&test_peer(), predicate);
}

#[test]
fn predicate_not_correct_fails() {
    let predicate = correct_name().not();
    assert!(!predicate.satisfied(&test_peer()));
}

#[test]
fn incorrect_over_predicate_fails() {
    let predicate = over!(Peer: name, P::equal(Some("INCORRECT_NAME".to_string())));

    let expected_msg = vec![
        "FAILED EXPECTATION",
        "  .name == Some(\"INCORRECT_NAME\")",
        "BECAUSE",
        "  .name Some(\"TestPeer\") != Some(\"INCORRECT_NAME\")",
    ]
    .join("\n");

    assert_eq!(predicate.assert_satisfied(&test_peer()), Err(DebugString(expected_msg)))
}

#[test]
fn incorrect_not_predicate_fails() {
    let predicate = over!(Peer: name, P::not_equal(Some(TEST_PEER_NAME.to_string())));

    let expected_msg = vec![
        "FAILED EXPECTATION",
        "  .name != Some(\"TestPeer\")",
        "BECAUSE",
        "  .name Some(\"TestPeer\") == Some(\"TestPeer\")",
    ]
    .join("\n");

    assert_eq!(predicate.assert_satisfied(&test_peer()), Err(DebugString(expected_msg)))
}

#[test]
fn vec_all_predicate_succeeds() {
    let strings = vec!["hello".to_string(), "world".to_string()];
    let predicate = P::all(P::not_equal("goodbye".to_string()));
    assert_satisfies!(&strings, predicate);
}

#[test]
fn map_keys_iter_all_predicate_succeeds() {
    let mut strings: HashMap<String, String> = HashMap::new();
    strings.insert("Hello".to_string(), "World".to_string());
    strings.insert("Goodnight".to_string(), "Moon".to_string());

    let predicate = P::iter_all(P::not_equal("goodbye".to_string()));

    assert_satisfies!(&strings.keys(), predicate);
}

#[test]
fn map_over_keys_all_predicate_succeeds() {
    let mut strings: HashMap<String, String> = HashMap::new();
    strings.insert("Hello".to_string(), "World".to_string());
    strings.insert("Goodnight".to_string(), "Moon".to_string());

    let predicate = P::all(P::not_equal("goodbye".to_string())).over_value(
        |m: &HashMap<String, String>| m.keys().cloned().collect::<Vec<String>>(),
        ".keys()",
    );

    assert_satisfies!(&strings, predicate)
}

// Introduce some example types used in the tests below to validate predicates over structs

/// An example Person
#[derive(Debug, PartialEq, Clone)]
struct Person {
    name: String,
    age: u64,
}

/// An example Group of Persons
#[derive(Debug, PartialEq, Clone)]
struct Group {
    persons: Vec<Person>,
}

#[test]
fn incorrect_compound_all_predicate_fails() {
    let test_group = Group {
        persons: vec![
            Person { name: "Alice".to_string(), age: 40 },
            Person { name: "Bob".to_string(), age: 41 },
        ],
    };

    let predicate = over!(
        Group: persons,
        P::all(
            over!(Person: name, P::not_equal("Bob".to_string()))
                .and(over!(Person: age, P::predicate(|age: &u64| *age < 50, "< 50")))
        )
    );

    let expected_msg = vec![
        "FAILED EXPECTATION",
        "  .persons ALL (.name != \"Bob\") AND (.age < 50)",
        "BECAUSE",
        "  .persons [1] Person { name: \"Bob\", age: 41 } BECAUSE .name \"Bob\" == \"Bob\",",
    ]
    .join("\n");

    assert_eq!(predicate.assert_satisfied(&test_group), Err(DebugString(expected_msg)));
}

#[test]
fn incorrect_compound_any_predicate_fails() {
    let test_group = Group {
        persons: vec![
            Person { name: "Alice".to_string(), age: 40 },
            Person { name: "Bob".to_string(), age: 41 },
            Person { name: "Bob".to_string(), age: 39 },
        ],
    };

    let predicate = over!(
        Group: persons,
        P::any(
            over!(Person: name, P::not_equal("Bob".to_string()))
                .and(over!(Person: age, P::predicate(|age: &u64| *age > 40, "> 40")))
        )
    );

    let expected_msg = vec![
        "FAILED EXPECTATION",
        "  .persons ANY (.name != \"Bob\") AND (.age > 40)",
        "BECAUSE",
        "  .persons",
        "    [0] Person { name: \"Alice\", age: 40 } BECAUSE .age NOT > 40,",
        "    [1] Person { name: \"Bob\", age: 41 } BECAUSE .name \"Bob\" == \"Bob\",",
        "    [2] Person { name: \"Bob\", age: 39 } BECAUSE (.name \"Bob\" == \"Bob\") AND (.age NOT > 40),"
    ].join("\n");

    assert_eq!(predicate.assert_satisfied(&test_group), Err(DebugString(expected_msg)));
}
