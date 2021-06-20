// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests the public APIs of FIDL data types.

use fidl_fidl_rust_test_external::{
    FlexibleAnimal, FlexibleButtons, FlexibleResourceThing, FlexibleValueThing, ResourceRecord,
    StrictAnimal, StrictButtons, StrictResourceThing, StrictValueThing, ValueRecord,
};
use matches::assert_matches;

#[test]
fn strict_bits() {
    assert_eq!(StrictButtons::from_bits(0b001), Some(StrictButtons::Play));
    assert_eq!(StrictButtons::from_bits(0b1100), None);
    assert_eq!(StrictButtons::from_bits_truncate(0b1010), StrictButtons::Pause);
    assert_eq!(StrictButtons::from_bits_truncate(u32::MAX), StrictButtons::all());
    assert_eq!(StrictButtons::Stop.bits(), 0b100);

    // You can use the flexible methods on strict types, but it produces a
    // deprecation warning.
    #[allow(deprecated)]
    let has_unknown_bits = StrictButtons::Play.has_unknown_bits();
    assert_eq!(has_unknown_bits, false);
    #[allow(deprecated)]
    let get_unknown_bits = StrictButtons::Play.get_unknown_bits();
    assert_eq!(get_unknown_bits, 0);
}

#[test]
fn flexible_bits() {
    assert_eq!(FlexibleButtons::from_bits(0b001), Some(FlexibleButtons::Play));
    assert_eq!(FlexibleButtons::from_bits(0b1100), None);
    assert_eq!(
        FlexibleButtons::from_bits_allow_unknown(0b1010) & FlexibleButtons::all(),
        FlexibleButtons::Pause
    );
    assert_eq!(FlexibleButtons::Stop.bits(), 0b100);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(0b1010).bits(), 0b1010);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(u32::MAX).bits(), u32::MAX);

    assert_eq!(FlexibleButtons::Play.has_unknown_bits(), false);
    assert_eq!(FlexibleButtons::Play.get_unknown_bits(), 0);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(0b1010).has_unknown_bits(), true);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(0b1010).get_unknown_bits(), 0b1000);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(u32::MAX).has_unknown_bits(), true);
    assert_eq!(
        FlexibleButtons::from_bits_allow_unknown(u32::MAX).get_unknown_bits(),
        u32::MAX & !0b111
    );

    // Negation ANDs with the mask.
    assert_ne!(
        FlexibleButtons::from_bits_allow_unknown(0b101000101),
        FlexibleButtons::Play | FlexibleButtons::Stop
    );
    assert_eq!(!FlexibleButtons::from_bits_allow_unknown(0b101000101), FlexibleButtons::Pause);
    assert_eq!(
        !!FlexibleButtons::from_bits_allow_unknown(0b101000101),
        FlexibleButtons::Play | FlexibleButtons::Stop
    );
}

#[test]
fn strict_enum() {
    assert_eq!(StrictAnimal::from_primitive(0), Some(StrictAnimal::Dog));
    assert_eq!(StrictAnimal::from_primitive(3), None);
    assert_eq!(StrictAnimal::Cat.into_primitive(), 1);

    // You can use the flexible methods on strict types, but it produces a
    // deprecation warning.
    #[allow(deprecated)]
    let is_unknown = StrictAnimal::Cat.is_unknown();
    assert_eq!(is_unknown, false);
    #[allow(deprecated)]
    let validate = StrictAnimal::Cat.validate();
    assert_eq!(validate, Ok(StrictAnimal::Cat));
}

#[test]
fn flexible_enum() {
    assert_eq!(FlexibleAnimal::from_primitive(0), Some(FlexibleAnimal::Dog));
    assert_eq!(FlexibleAnimal::from_primitive(3), None);
    assert_eq!(FlexibleAnimal::from_primitive_allow_unknown(0), FlexibleAnimal::Dog);

    #[allow(deprecated)] // allow referencing __Unknown
    let unknown3 = FlexibleAnimal::__Unknown(3);
    assert_eq!(FlexibleAnimal::from_primitive_allow_unknown(3), unknown3);

    assert_eq!(FlexibleAnimal::Cat.into_primitive(), 1);
    assert_eq!(FlexibleAnimal::from_primitive_allow_unknown(3).into_primitive(), 3);
    assert_eq!(FlexibleAnimal::unknown().into_primitive(), i32::MAX);

    assert_eq!(FlexibleAnimal::Cat.is_unknown(), false);
    assert_eq!(FlexibleAnimal::from_primitive_allow_unknown(3).is_unknown(), true);
    assert_eq!(FlexibleAnimal::unknown().is_unknown(), true);

    assert_eq!(FlexibleAnimal::Cat.validate(), Ok(FlexibleAnimal::Cat));
    assert_eq!(FlexibleAnimal::from_primitive_allow_unknown(3).validate(), Err(3));
    assert_eq!(FlexibleAnimal::unknown().validate(), Err(i32::MAX));
}

#[test]
fn strict_value_union() {
    // You can use the flexible methods on strict types, but it produces a
    // deprecation warning.
    #[allow(deprecated)]
    let is_unknown = StrictValueThing::Number(42).is_unknown();
    assert_eq!(is_unknown, false);
    #[allow(deprecated)]
    let validate = StrictValueThing::Name("hello".to_owned()).validate();
    assert_eq!(validate, Ok(StrictValueThing::Name("hello".to_owned())));
}

#[test]
fn flexible_value_union() {
    assert_eq!(FlexibleValueThing::Number(42).is_unknown(), false);
    assert_eq!(FlexibleValueThing::Name("hello".to_owned()).is_unknown(), false);
    assert_eq!(FlexibleValueThing::Number(42).validate(), Ok(FlexibleValueThing::Number(42)));
    assert_eq!(
        FlexibleValueThing::Name("hello".to_owned()).validate(),
        Ok(FlexibleValueThing::Name("hello".to_owned()))
    );
    assert_eq!(FlexibleValueThing::unknown(0, vec![]).is_unknown(), true);
    assert_eq!(FlexibleValueThing::unknown(0, vec![]).validate(), Err((0, vec![])));
}

#[test]
fn strict_resource_union() {
    // You can use the flexible methods on strict types, but it produces a
    // deprecation warning.
    #[allow(deprecated)]
    let is_unknown = StrictResourceThing::Number(42).is_unknown();
    assert_eq!(is_unknown, false);
    #[allow(deprecated)]
    let validate = StrictResourceThing::Name("hello".to_owned()).validate();
    assert_eq!(validate, Ok(StrictResourceThing::Name("hello".to_owned())));
}

#[test]
fn flexible_resource_union() {
    assert_eq!(FlexibleResourceThing::Number(42).is_unknown(), false);
    assert_eq!(FlexibleResourceThing::Name("hello".to_owned()).is_unknown(), false);
    assert_eq!(FlexibleResourceThing::Number(42).validate(), Ok(FlexibleResourceThing::Number(42)));
    assert_eq!(
        FlexibleResourceThing::Name("hello".to_owned()).validate(),
        Ok(FlexibleResourceThing::Name("hello".to_owned()))
    );
    assert_eq!(
        FlexibleResourceThing::unknown(0, fidl::UnknownData { bytes: vec![], handles: vec![] })
            .is_unknown(),
        true
    );
    assert_eq!(
        FlexibleResourceThing::unknown(0, fidl::UnknownData { bytes: vec![], handles: vec![] })
            .validate(),
        Err((0, fidl::UnknownData { bytes: vec![], handles: vec![] }))
    );
}

#[test]
fn value_table() {
    assert_matches!(
        ValueRecord::EMPTY,
        ValueRecord { name: None, age: None, unknown_data: None, .. }
    );

    let table = ValueRecord { age: Some(30), ..ValueRecord::EMPTY };
    assert_eq!(table.name, None);
    assert_eq!(table.age, Some(30));

    let ValueRecord { name, .. } = table;
    assert_eq!(name, None);
    let ValueRecord { age, .. } = table;
    assert_eq!(age, Some(30));

    let bytes = vec![1, 2, 3, 4, 5, 6, 7, 8];
    let unknown = ValueRecord {
        unknown_data: Some(std::array::IntoIter::new([(123, bytes.clone())]).collect()),
        ..ValueRecord::EMPTY
    };
    let unknown_data = unknown.unknown_data.as_ref().unwrap();
    assert_eq!(unknown_data.len(), 1);
    assert_eq!(unknown_data.get(&123), Some(&bytes));
    assert_eq!(unknown_data.get(&456), None);
}

#[test]
fn resource_table() {
    assert_matches!(
        ResourceRecord::EMPTY,
        ResourceRecord { name: None, age: None, unknown_data: None, .. }
    );

    let table = ResourceRecord { age: Some(30), ..ResourceRecord::EMPTY };
    assert_eq!(table.name, None);
    assert_eq!(table.age, Some(30));

    let ResourceRecord { name, .. } = table;
    assert_eq!(name, None);
    let ResourceRecord { age, .. } = table;
    assert_eq!(age, Some(30));

    let bytes = vec![1, 2, 3, 4, 5, 6, 7, 8];
    let unknown = ResourceRecord {
        unknown_data: Some(
            std::array::IntoIter::new([(
                123,
                fidl::UnknownData { bytes: bytes.clone(), handles: vec![fidl::Handle::invalid()] },
            )])
            .collect(),
        ),
        ..ResourceRecord::EMPTY
    };
    let unknown_data = unknown.unknown_data.as_ref().unwrap();
    assert_eq!(unknown_data.len(), 1);
    assert_eq!(
        unknown_data.get(&123),
        Some(&fidl::UnknownData { bytes, handles: vec![fidl::Handle::invalid()] })
    );
    assert_eq!(unknown_data.get(&456), None);
}
