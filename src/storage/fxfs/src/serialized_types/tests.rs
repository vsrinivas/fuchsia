// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::io::Cursor,
    type_hash::TypeHash,
};

// Note we don't use the standard serialized_types::EARLIEST_SUPPORTED_VERSION for tests.
const EARLIEST_SUPPORTED_VERSION: Version = Version { major: 1, minor: 0 };

// Note we don't use the standard serialized_types::LATEST_VERSION for tests.
const LATEST_VERSION: Version = Version { major: 4, minor: 2 };

#[derive(Debug, Serialize, Deserialize, TypeHash, Versioned)]
struct FooV1 {
    a: u32,
}
#[derive(Debug, Serialize, Deserialize, TypeHash, Versioned)]
struct FooV2 {
    a: u32,
    b: u8,
}
#[derive(Debug, Eq, PartialEq, Serialize, Deserialize, TypeHash, Versioned)]
struct FooV3 {
    a: u32,
    c: u64,
}
impl From<FooV1> for FooV2 {
    fn from(f: FooV1) -> Self {
        FooV2 { a: f.a, b: 1 }
    }
}
impl From<FooV2> for FooV3 {
    fn from(f: FooV2) -> Self {
        FooV3 { a: f.a, c: (f.b as u64) << 8 }
    }
}

versioned_type! {
    4.. => FooV3,
    3.. => FooV2,
    1.. => FooV1,
}

#[test]
fn test_deserialize_from_version() {
    let f1 = FooV1 { a: 1 };
    let f2 = FooV2 { a: 1, b: 1 };
    let f3 = FooV3 { a: 1, c: 256 };

    let mut v: Vec<u8> = Vec::new();
    // Note we do this by hand because Versioned::serialize_into will use the latest serializer
    // and we do NOT want varint encoding here.
    bincode::serialize_into(&mut v, &f1).expect("FooV1");
    assert_eq!(
        FooV3::deserialize_from_version(&mut Cursor::new(&v), EARLIEST_SUPPORTED_VERSION)
            .expect("Deserialize FooV1"),
        f3
    );

    let mut v: Vec<u8> = Vec::new();
    f2.serialize_into(&mut v).expect("FooV2");
    assert_eq!(
        FooV3::deserialize_from_version(&mut Cursor::new(&v), Version { major: 3, minor: 0 })
            .expect("Deserialize FooV2"),
        f3
    );

    let mut v: Vec<u8> = Vec::new();
    f3.serialize_into(&mut v).expect("FooV3");
    assert_eq!(
        FooV3::deserialize_from_version(&mut Cursor::new(&v), LATEST_VERSION)
            .expect("Deserialize FooV3"),
        f3
    );

    // Unsupported version.
    assert!(FooV3::deserialize_from_version(&mut Cursor::new(&v), Version { major: 5, minor: 0 })
        .is_err());
}
