// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use core::fmt;
use fuchsia_inspect::{assert_inspect_tree, Inspector};
use fuchsia_inspect_derive::{IDebug, IValue, Unit};
use serde::Serialize;

// TODO(49049): Add negative tests when compile failure tests are possible.

#[derive(Unit, Serialize)]
struct Yak {
    name: String,
    age: i64,
    #[serde(rename = "Name")] // Unrelated field attributes together with inspect attributes allowed
    #[inspect(skip)] // Hide PII of Yak
    credit_card_no: String,
    yakling: Yakling,
}

#[derive(Unit, Serialize)]
#[serde(rename = "YakLing")] // Unrelated container attributes allowed
struct Yakling {
    #[serde(rename = "Name")] // Unrelated field attributes allowed
    name: String,
    age: u8,
}

#[derive(Debug)]
enum Horse {
    Arabian,
    Icelandic,
}

#[derive(Unit, Default)]
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
    t_isize: isize,
    t_f32: f32,
    t_f64: f64,
    t_bool: bool,
    t_string: String,
    t_vec_u8: Vec<u8>,
}

// Compile test to check that derive of Default and Debug works with IOwned wrappers
#[derive(Default, Debug)]
struct _Composite {
    name: IValue<String>,
    age: IDebug<u8>,
}

// Display cannot be derived with std, so we require that the fields are `Display` instead.
// This is important so that 3p crates such as `Derivative` can auto-derive `Display`.
impl fmt::Display for _Composite {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "name: {}, age: {}", self.name, self.age)
    }
}

#[test]
fn unit_primitive() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut num = 127i8;
    let mut num_data = num.inspect_create(&root, "num");
    assert_inspect_tree!(inspector, root: { num: 127i64 });
    num = -128;
    num.inspect_update(&mut num_data);
    assert_inspect_tree!(inspector, root: { num: -128i64 });
    std::mem::drop(num_data);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn unit_flat() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut yakling = Yakling { name: "Lil Sebastian".to_string(), age: 5 };
    let mut yakling_data = yakling.inspect_create(&root, "yak");
    assert_inspect_tree!(inspector, root: {
        yak: { name: "Lil Sebastian", age: 5u64 }
    });
    yakling.name = "Sebastian".to_string();
    yakling.age = 10;
    yakling.inspect_update(&mut yakling_data);
    assert_inspect_tree!(inspector, root: {
        yak: { name: "Sebastian", age: 10u64 }
    });
    std::mem::drop(yakling_data);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn unit_nested() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut yak = Yak {
        name: "Big Sebastian".to_string(),
        age: 25,
        credit_card_no: "12345678".to_string(),
        yakling: Yakling { name: "Lil Sebastian".to_string(), age: 2 },
    };
    let mut yak_data = yak.inspect_create(&root, "my_yak");
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
    yak.inspect_update(&mut yak_data);
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
    std::mem::drop(yak_data);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn unit_basic_types() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut basic = BasicTypes::default();
    let mut basic_data = basic.inspect_create(&root, "basic");
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
            t_isize: 0i64,
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
    basic.inspect_update(&mut basic_data);
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
            t_isize: 0i64,
            t_f32: 1f64,
            t_f64: 0f64,
            t_bool: true,
            t_string: "hello world",
            t_vec_u8: vec![0x13u8, 0x37u8],
        }
    });
    std::mem::drop(basic_data);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn ivalue_primitive() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut num = IValue::attached(126i8, &root, "num");
    assert_inspect_tree!(inspector, root: { num: 126i64 });

    // Modifying num should change its value but not update inspect
    *num += 1;
    assert_eq!(*num, 127);
    assert_inspect_tree!(inspector, root: { num: 126i64 });

    // Now update inspect
    num.iupdate();
    assert_inspect_tree!(inspector, root: { num: 127i64 });
    num.iset(-128);
    assert_eq!(*num, -128);
    assert_inspect_tree!(inspector, root: { num: -128i64 });
    std::mem::drop(num);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn ivalue_nested() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let yak_base = Yak {
        name: "Big Sebastian".to_string(),
        age: 25,
        credit_card_no: "12345678".to_string(),
        yakling: Yakling { name: "Lil Sebastian".to_string(), age: 2 },
    };
    let mut yak = IValue::attached(yak_base, &root, "my_yak");
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
    yak.iupdate();
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
    std::mem::drop(yak);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn idebug_enum() {
    let inspector = Inspector::new();
    let root = inspector.root();
    let mut horse = IDebug::attached(Horse::Arabian, &root, "horse");
    assert_inspect_tree!(inspector, root: { horse: "Arabian" });
    *horse = Horse::Icelandic;
    horse.iupdate();
    assert_inspect_tree!(inspector, root: { horse: "Icelandic" });
    std::mem::drop(horse);
    assert_inspect_tree!(inspector, root: {});
}

#[test]
fn iowned_new() {
    let mut v = IValue::new(1u64);
    assert_eq!(*v, 1u64);
    v.iset(2);
    assert_eq!(*v, 2u64);
    let mut d = IDebug::new(1u64);
    assert_eq!(*d, 1u64);
    d.iset(2);
    assert_eq!(*d, 2u64);
}

#[test]
fn iowned_default() {
    let v: IValue<u16> = IValue::default();
    assert_eq!(*v, 0u16);
    let d: IDebug<String> = IDebug::default();
    assert_eq!(d.as_str(), "");
}

#[test]
fn iowned_from() {
    let v = IValue::from(17u16);
    assert_eq!(*v, 17u16);
    let d = IDebug::from("hello".to_string());
    assert_eq!(d.as_str(), "hello");
}

#[test]
fn iowned_into() {
    let v: IValue<_> = 17u16.into();
    assert_eq!(*v, 17u16);
    let d: IDebug<String> = "hello".to_string().into();
    assert_eq!(d.as_str(), "hello");
}

#[test]
fn iowned_into_inner() {
    let v = IValue::new(17u16);
    assert_eq!(v.into_inner(), 17u16);
    let d = IDebug::new("hello".to_string());
    assert_eq!(d.into_inner(), "hello".to_string());
}

#[test]
fn iowned_debug() {
    let mut v = IValue::new(1337u64);
    assert_eq!(format!("{:?}", v).as_str(), "1337");
    v.iset(1338);
    assert_eq!(format!("{:?}", v).as_str(), "1338");
    let mut d = IDebug::new("hello".to_string());
    assert_eq!(format!("{:?}", d).as_str(), "\"hello\"");
    d.iset("hello, world".to_string());
    assert_eq!(format!("{:?}", d).as_str(), "\"hello, world\"");
}

#[test]
fn iowned_display() {
    let mut v = IValue::new(1337u64);
    assert_eq!(format!("{}", v).as_str(), "1337");
    v.iset(1338);
    assert_eq!(format!("{}", v).as_str(), "1338");
    let mut d = IDebug::new("hello".to_string());
    assert_eq!(format!("{}", d).as_str(), "hello");
    d.iset("hello, world".to_string());
    assert_eq!(format!("{}", d).as_str(), "hello, world");
}
