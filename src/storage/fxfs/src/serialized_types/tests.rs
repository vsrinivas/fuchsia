// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::serialized_types::{versioned_type, Version, VersionLatest, VersionNumber},
    serde::{Deserialize, Serialize},
    std::convert::From,
    std::io::Cursor,
};

#[derive(Debug, Serialize, Deserialize)]
struct FooV1 {
    a: u32,
}
#[derive(Debug, Serialize, Deserialize)]
struct FooV2 {
    a: u32,
    b: u8,
}
#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
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
    1 => FooV1,
    2 => FooV1,
    3 => FooV2,
    4 => FooV3,
}

#[test]
fn test_deserialize_from_version() {
    let f1 = FooV1 { a: 1 };
    let f2 = FooV2 { a: 1, b: 1 };
    let f3 = FooV3 { a: 1, c: 256 };

    assert_eq!(FooV1::version().major, 2);
    assert_eq!(FooV2::version().major, 3);
    assert_eq!(FooV3::version().major, 4);

    let mut v: Vec<u8> = Vec::new();
    f1.serialize_into(&mut v).expect("FooV1");
    assert_eq!(
        FooV3::deserialize_from_version(&mut Cursor::new(&v), FooV1::version())
            .expect("Deserialize FooV1"),
        f3
    );

    let mut v: Vec<u8> = Vec::new();
    f2.serialize_into(&mut v).expect("FooV2");
    assert_eq!(
        FooV3::deserialize_from_version(&mut Cursor::new(&v), FooV2::version())
            .expect("Deserialize FooV2"),
        f3
    );

    let mut v: Vec<u8> = Vec::new();
    f3.serialize_into(&mut v).expect("FooV3");
    assert_eq!(
        FooV3::deserialize_from_version(&mut Cursor::new(&v), FooV3::version())
            .expect("Deserialize FooV3"),
        f3
    );

    // Unsupported version.
    assert!(FooV3::deserialize_from_version(
        &mut Cursor::new(&v),
        VersionNumber { major: 5, minor: 0 }
    )
    .is_err());
}
