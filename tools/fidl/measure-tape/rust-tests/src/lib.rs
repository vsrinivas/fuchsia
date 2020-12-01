// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fidl_measuretape as fmt,
    fuchsia_zircon::{Event, HandleBased, Vmo},
    measure_tape_for_toplevelunion::{measure, Size},
};

const HELLO_WORLD_EN: &str = "hello, world!";
const HELLO_WORLD_FR: &str = "bonjour, le monde!";
const HELLO_WORLD_DE: &str = "hallo, welt!";
const HELLO_WORLD_ES: &str = "Hola, Mundo!";
const HELLO_WORLD_RU: &str = "Привет мир!";
const HELLO_WORLD_ZH: &str = "你好，世界!";

#[test]
fn primitive() {
    let value = fmt::TopLevelUnion::Primitive(5);
    assert_eq!(Size { num_bytes: 24 + 8, num_handles: 0 }, measure(&value))
}

#[test]
fn handle() {
    let handle = Event::create().unwrap().into_handle();
    let value = fmt::TopLevelUnion::Handle(handle);
    assert_eq!(Size { num_bytes: 24 + 8, num_handles: 1 }, measure(&value))
}

#[test]
fn struct_with_string() {
    let value = fmt::TopLevelUnion::StructWithString(fmt::StructWithString {
        string: HELLO_WORLD_EN.to_string(),
    });
    assert_eq!(Size { num_bytes: 24 + 16 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn struct_with_opt_string_no_string() {
    let value =
        fmt::TopLevelUnion::StructWithOptString(fmt::StructWithOptString { opt_string: None });
    assert_eq!(Size { num_bytes: 24 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn struct_with_opt_string_has_string() {
    let value = fmt::TopLevelUnion::StructWithOptString(fmt::StructWithOptString {
        opt_string: Some(HELLO_WORLD_FR.to_string()),
    });
    assert_eq!(Size { num_bytes: 24 + 16 + 24, num_handles: 0 }, measure(&value))
}

#[test]
fn table_empty() {
    let value = fmt::TopLevelUnion::Table(fmt::Table::EMPTY);
    assert_eq!(Size { num_bytes: 24 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn table_only_max_ordinal_is_set() {
    let value = fmt::TopLevelUnion::Table(fmt::Table { primitive: Some(42), ..fmt::Table::EMPTY });
    assert_eq!(Size { num_bytes: 24 + 16 + (5 * 16) + 8, num_handles: 0 }, measure(&value))
}

#[test]
fn table_string_is_set() {
    let value = fmt::TopLevelUnion::Table(fmt::Table {
        string: Some(HELLO_WORLD_EN.to_string()),
        ..fmt::Table::EMPTY
    });
    assert_eq!(Size { num_bytes: 24 + 16 + (3 * 16) + 16 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn array_of_twelve_bytes() {
    let value = fmt::TopLevelUnion::ArrayOfTwelveBytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    assert_eq!(Size { num_bytes: 24 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn array_of_three_strings() {
    let value = fmt::TopLevelUnion::ArrayOfThreeStrings([
        HELLO_WORLD_ES.to_string(), // 12 bytes
        HELLO_WORLD_RU.to_string(), // 20 bytes
        HELLO_WORLD_ZH.to_string(), // 16 bytes
    ]);
    assert_eq!(Size { num_bytes: 24 + (3 * 16) + 16 + 24 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn array_of_two_tables_both_empty() {
    let value = fmt::TopLevelUnion::ArrayOfTwoTables([fmt::Table::EMPTY, fmt::Table::EMPTY]);
    assert_eq!(Size { num_bytes: 24 + (2 * 16), num_handles: 0 }, measure(&value))
}

#[test]
fn array_of_two_tables_mixed() {
    let handle = Event::create().unwrap().into_handle();
    let value = fmt::TopLevelUnion::ArrayOfTwoTables([
        fmt::Table { primitive: Some(27), ..fmt::Table::EMPTY },
        fmt::Table { handle: Some(handle), ..fmt::Table::EMPTY },
    ]);
    assert_eq!(
        Size { num_bytes: 24 + (2 * 16) + (5 * 16) + 8 + (4 * 16) + 8, num_handles: 1 },
        measure(&value)
    )
}

#[test]
fn array_of_two_unions() {
    let value = fmt::TopLevelUnion::ArrayOfTwoUnions([
        fmt::Union::Primitive(654321),
        fmt::Union::Primitive(123456),
    ]);
    assert_eq!(Size { num_bytes: 24 + (2 * 24) + 8 + 8, num_handles: 0 }, measure(&value))
}

#[test]
fn struct_with_two_arrays() {
    let value = fmt::TopLevelUnion::StructWithTwoArrays(fmt::StructWithTwoArrays {
        array_of_twelve_bytes: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        array_of_three_strings: ["".to_string(), "".to_string(), "".to_string()],
    });
    assert_eq!(Size { num_bytes: 24 + 64, num_handles: 0 }, measure(&value))
}

#[test]
fn array_of_three_structs_with_one_handle() {
    let value = fmt::TopLevelUnion::ArrayOfThreeStructsWithOneHandle([
        fmt::StructWithOneHandle {
            tiny1: 1,
            vmo: Vmo::create(1024).expect("vmo creation failed"),
            tiny2: 2,
        },
        fmt::StructWithOneHandle {
            tiny1: 1,
            vmo: Vmo::create(1024).expect("vmo creation failed"),
            tiny2: 2,
        },
        fmt::StructWithOneHandle {
            tiny1: 1,
            vmo: Vmo::create(1024).expect("vmo creation failed"),
            tiny2: 2,
        },
    ]);
    assert_eq!(Size { num_bytes: 24 + (3 * 12 + 4), num_handles: 3 }, measure(&value))
}

#[test]
fn array_of_three_structs_with_two_handles() {
    let value = fmt::TopLevelUnion::ArrayOfThreeStructsWithTwoHandles([
        fmt::StructWithTwoHandles {
            tiny1: 1,
            vmo1: Vmo::create(1024).expect("vmo creation failed"),
            vmo2: Vmo::create(1024).expect("vmo creation failed"),
        },
        fmt::StructWithTwoHandles {
            tiny1: 1,
            vmo1: Vmo::create(1024).expect("vmo creation failed"),
            vmo2: Vmo::create(1024).expect("vmo creation failed"),
        },
        fmt::StructWithTwoHandles {
            tiny1: 1,
            vmo1: Vmo::create(1024).expect("vmo creation failed"),
            vmo2: Vmo::create(1024).expect("vmo creation failed"),
        },
    ]);
    assert_eq!(Size { num_bytes: 24 + (3 * 12 + 4), num_handles: 6 }, measure(&value))
}

#[test]
fn vector_of_bytes_three_bytes() {
    let value = fmt::TopLevelUnion::VectorOfBytes(vec![1, 2, 3]);
    assert_eq!(Size { num_bytes: 24 + 16 + 8, num_handles: 0 }, measure(&value))
}

#[test]
fn vector_of_bytes_nine_bytes() {
    let value = fmt::TopLevelUnion::VectorOfBytes(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]);
    assert_eq!(Size { num_bytes: 24 + 16 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn vector_of_strings() {
    let value = fmt::TopLevelUnion::VectorOfStrings(vec![
        HELLO_WORLD_ES.to_string(),
        HELLO_WORLD_RU.to_string(),
        HELLO_WORLD_ZH.to_string(),
    ]);
    assert_eq!(
        Size { num_bytes: 24 + 16 + (3 * 16) + 16 + 24 + 16, num_handles: 0 },
        measure(&value)
    )
}

#[test]
fn vector_of_handles_empty() {
    let value = fmt::TopLevelUnion::VectorOfHandles(vec![]);
    assert_eq!(Size { num_bytes: 24 + 16, num_handles: 0 }, measure(&value))
}

#[test]
fn vector_of_handles_three_handles() {
    let value = fmt::TopLevelUnion::VectorOfHandles(vec![
        Event::create().unwrap().into_handle(),
        Event::create().unwrap().into_handle(),
        Event::create().unwrap().into_handle(),
    ]);
    assert_eq!(Size { num_bytes: 24 + 16 + 16, num_handles: 3 }, measure(&value))
}

#[test]
fn vector_of_tables_two_empty_tables() {
    let value = fmt::TopLevelUnion::VectorOfTables(vec![fmt::Table::EMPTY, fmt::Table::EMPTY]);
    assert_eq!(Size { num_bytes: 24 + 16 + (2 * 16), num_handles: 0 }, measure(&value))
}

#[test]
fn vector_of_tables_mixed() {
    let handle = Event::create().unwrap().into_handle();
    let value = fmt::TopLevelUnion::VectorOfTables(vec![
        fmt::Table { primitive: Some(42), ..fmt::Table::EMPTY },
        fmt::Table { handle: Some(handle), ..fmt::Table::EMPTY },
    ]);
    assert_eq!(
        Size { num_bytes: 24 + 16 + (2 * 16) + (5 * 16) + 8 + (4 * 16) + 8, num_handles: 1 },
        measure(&value)
    )
}

#[test]
fn vector_of_unions() {
    let value = fmt::TopLevelUnion::VectorOfUnions(vec![
        fmt::Union::Primitive(654321),
        fmt::Union::Primitive(123456),
    ]);
    assert_eq!(Size { num_bytes: 24 + 16 + (2 * 24) + 8 + 8, num_handles: 0 }, measure(&value))
}

#[test]
fn struct_with_two_vectors_both_null() {
    let value = fmt::TopLevelUnion::StructWithTwoVectors(fmt::StructWithTwoVectors {
        vector_of_bytes: None,
        vector_of_strings: None,
    });
    assert_eq!(Size { num_bytes: 24 + 32, num_handles: 0 }, measure(&value))
}

#[test]
fn struct_with_two_vectors_three_bytes_in_first_two_strings_in_second() {
    let value = fmt::TopLevelUnion::StructWithTwoVectors(fmt::StructWithTwoVectors {
        vector_of_bytes: Some(vec![1, 2, 3]),
        vector_of_strings: Some(vec![HELLO_WORLD_RU.to_string(), HELLO_WORLD_DE.to_string()]),
    });
    assert_eq!(
        Size { num_bytes: 24 + 32 + 8 + (2 * 16) + 24 + 16, num_handles: 0 },
        measure(&value)
    )
}

#[test]
fn vector_of_structs_with_one_handle() {
    let value = fmt::TopLevelUnion::VectorOfStructsWithOneHandle(vec![
        fmt::StructWithOneHandle {
            tiny1: 1,
            vmo: Vmo::create(1024).expect("vmo creation failed"),
            tiny2: 2,
        },
        fmt::StructWithOneHandle {
            tiny1: 1,
            vmo: Vmo::create(1024).expect("vmo creation failed"),
            tiny2: 2,
        },
        fmt::StructWithOneHandle {
            tiny1: 1,
            vmo: Vmo::create(1024).expect("vmo creation failed"),
            tiny2: 2,
        },
    ]);
    assert_eq!(Size { num_bytes: 24 + 16 + (3 * 12 + 4), num_handles: 3 }, measure(&value))
}

#[test]
fn vector_of_structs_with_two_handles() {
    let value = fmt::TopLevelUnion::VectorOfStructsWithTwoHandles(vec![
        fmt::StructWithTwoHandles {
            tiny1: 1,
            vmo1: Vmo::create(1024).expect("vmo creation failed"),
            vmo2: Vmo::create(1024).expect("vmo creation failed"),
        },
        fmt::StructWithTwoHandles {
            tiny1: 1,
            vmo1: Vmo::create(1024).expect("vmo creation failed"),
            vmo2: Vmo::create(1024).expect("vmo creation failed"),
        },
        fmt::StructWithTwoHandles {
            tiny1: 1,
            vmo1: Vmo::create(1024).expect("vmo creation failed"),
            vmo2: Vmo::create(1024).expect("vmo creation failed"),
        },
    ]);
    assert_eq!(Size { num_bytes: 24 + 16 + (3 * 12 + 4), num_handles: 6 }, measure(&value))
}

#[test]
fn struct_with_a_vector() {
    let value = fmt::TopLevelUnion::StructWithAVector(fmt::StructWithAVector {
        vector_of_strings: vec!["a".to_string()],
    });
    assert_eq!(Size { num_bytes: 16 * 4, num_handles: 0 }, measure(&value));
}
