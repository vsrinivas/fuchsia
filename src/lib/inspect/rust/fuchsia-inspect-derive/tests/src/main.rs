// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fuchsia_inspect::{assert_inspect_tree, Inspector};
use fuchsia_inspect_derive::Inspect;
use serde::Serialize;

// TODO(49049): Add negative tests when compile failure tests are possible.

#[derive(Inspect, Serialize)]
struct Yak {
    name: String,
    age: i64,
    #[serde(rename = "Name")] // Unrelated field attributes together with inspect attributes allowed
    #[inspect(skip)] // Hide PII of Yak
    credit_card_no: String,
    yakling: Yakling,
}

#[derive(Inspect, Serialize)]
#[serde(rename = "YakLing")] // Unrelated container attributes allowed
struct Yakling {
    #[serde(rename = "Name")] // Unrelated field attributes allowed
    name: String,
    age: u8,
}

#[derive(Inspect, Default)]
struct BasicTypes {
    t_u8: u8,
    t_u16: u16,
    t_u32: u32,
    t_u64: u64,
    t_usize: usize,
    t_i8: i8,
    t_i16: i16,
    t_i32: i32,
    t_i64: i64,
    t_f32: f32,
    t_f64: f64,
    t_bool: bool,
    t_string: String,
    t_vec_u8: Vec<u8>,
}

#[test]
fn primitive() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut num = 127i8;
    let mut num_inspect = num.inspect_create(&root, "num");
    assert_inspect_tree!(inspector, root: { num: 127i64 });
    num = -128;
    num.inspect_update(&mut num_inspect);
    assert_inspect_tree!(inspector, root: { num: -128i64 });
    std::mem::drop(num_inspect);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn flat() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut yakling = Yakling { name: "Lil Sebastian".to_string(), age: 5 };
    let mut yakling_inspect = yakling.inspect_create(&root, "yak");
    assert_inspect_tree!(inspector, root: {
        yak: { name: "Lil Sebastian", age: 5u64 }
    });
    yakling.name = "Sebastian".to_string();
    yakling.age = 10;
    yakling.inspect_update(&mut yakling_inspect);
    assert_inspect_tree!(inspector, root: {
        yak: { name: "Sebastian", age: 10u64 }
    });
    std::mem::drop(yakling_inspect);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn nested() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut yak = Yak {
        name: "Big Sebastian".to_string(),
        age: 25,
        credit_card_no: "12345678".to_string(),
        yakling: Yakling { name: "Lil Sebastian".to_string(), age: 2 },
    };
    let mut yak_inspect = yak.inspect_create(&root, "my_yak");
    assert_inspect_tree!(inspector, root: {
        my_yak: {
            name: "Big Sebastian",
            age: 25i64,
            yakling: {
                name: "Lil Sebastian",
                age: 2u64,
            },
        }
    });
    yak.yakling.age += 1; // Happy bday, Lil Sebastian
    yak.name = "Big Sebastian Sr.".to_string();
    yak.credit_card_no = "1234".to_string();
    yak.inspect_update(&mut yak_inspect);
    assert_inspect_tree!(inspector, root: {
        my_yak: {
            name: "Big Sebastian Sr.",
            age: 25i64,
            yakling: {
                name: "Lil Sebastian",
                age: 3u64,
            },
        }
    });
    std::mem::drop(yak_inspect);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn basic_types() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut basic = BasicTypes::default();
    let mut basic_inspect = basic.inspect_create(&root, "basic");
    assert_inspect_tree!(inspector, root: {
        basic: {
            t_u8: 0u64,
            t_u16: 0u64,
            t_u32: 0u64,
            t_u64: 0u64,
            t_usize: 0u64,
            t_i8: 0i64,
            t_i16: 0i64,
            t_i32: 0i64,
            t_i64: 0i64,
            t_f32: 0f64,
            t_f64: 0f64,
            t_bool: false,
            t_string: "",
            t_vec_u8: Vec::<u8>::default(),
        }
    });
    basic.t_string = "hello world".to_string();
    basic.t_bool = true;
    basic.t_f32 = 1.0;
    basic.t_vec_u8 = vec![0x13, 0x37];
    basic.inspect_update(&mut basic_inspect);
    assert_inspect_tree!(inspector, root: {
        basic: {
            t_u8: 0u64,
            t_u16: 0u64,
            t_u32: 0u64,
            t_u64: 0u64,
            t_usize: 0u64,
            t_i8: 0i64,
            t_i16: 0i64,
            t_i32: 0i64,
            t_i64: 0i64,
            t_f32: 1f64,
            t_f64: 0f64,
            t_bool: true,
            t_string: "hello world",
            t_vec_u8: vec![0x13u8, 0x37u8],
        }
    });
    std::mem::drop(basic_inspect);
    assert_inspect_tree!(inspector, root: {});
}
