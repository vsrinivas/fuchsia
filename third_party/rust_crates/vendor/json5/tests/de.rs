use json5;
use serde_derive::Deserialize;

use std::collections::HashMap;

mod common;

use crate::common::{deserializes_to, deserializes_with_error};

#[test]
fn deserializes_bool() {
    deserializes_to("true", true);
    deserializes_to("false", false);
}

#[test]
fn deserializes_i8() {
    let x: i8 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
    deserializes_to("-42", -x);
    deserializes_to("-42.", -x);
    deserializes_to("-42.0", -x);
    deserializes_to("-42e0", -x);
    deserializes_to("-4.2e1", -x);
    deserializes_to("-.42e2", -x);
    deserializes_to("-0.42e2", -x);
}

#[test]
fn deserializes_u8() {
    let x: u8 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
}

#[test]
fn deserializes_i16() {
    let x: i16 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
    deserializes_to("-42", -x);
    deserializes_to("-42.", -x);
    deserializes_to("-42.0", -x);
    deserializes_to("-42e0", -x);
    deserializes_to("-4.2e1", -x);
    deserializes_to("-.42e2", -x);
    deserializes_to("-0.42e2", -x);
}

#[test]
fn deserializes_u16() {
    let x: u16 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
}

#[test]
fn deserializes_i32() {
    let x: i32 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
    deserializes_to("-42", -x);
    deserializes_to("-42.", -x);
    deserializes_to("-42.0", -x);
    deserializes_to("-42e0", -x);
    deserializes_to("-4.2e1", -x);
    deserializes_to("-.42e2", -x);
    deserializes_to("-0.42e2", -x);
}

#[test]
fn deserializes_u32() {
    let x: u32 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
}

#[test]
fn deserializes_i64() {
    let x: i64 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
    deserializes_to("-42", -x);
    deserializes_to("-42.", -x);
    deserializes_to("-42.0", -x);
    deserializes_to("-42e0", -x);
    deserializes_to("-4.2e1", -x);
    deserializes_to("-.42e2", -x);
    deserializes_to("-0.42e2", -x);
}

#[test]
fn deserializes_u64() {
    let x: u64 = 42;
    deserializes_to("0x2A", x);
    deserializes_to("0x2a", x);
    deserializes_to("0X2A", x);
    deserializes_to("0X2a", x);
    deserializes_to("0x00002A", x);
    deserializes_to("42", x);
    deserializes_to("42.", x);
    deserializes_to("42.0", x);
    deserializes_to("42e0", x);
    deserializes_to("4.2e1", x);
    deserializes_to(".42e2", x);
    deserializes_to("0.42e2", x);
}

#[test]
fn deserializes_f32() {
    let x: f32 = 42.42;
    deserializes_to("42.42", x);
    deserializes_to("42.42e0", x);
    deserializes_to("4.242e1", x);
    deserializes_to(".4242e2", x);
    deserializes_to("0.4242e2", x);
    deserializes_to("-42.42", -x);
    deserializes_to("-42.42", -x);
    deserializes_to("-42.42", -x);
    deserializes_to("-42.42e0", -x);
    deserializes_to("-4.242e1", -x);
    deserializes_to("-.4242e2", -x);
    deserializes_to("-0.4242e2", -x);
}

#[test]
fn deserializes_f64() {
    let x: f64 = 42.42;
    deserializes_to("42.42", x);
    deserializes_to("42.42e0", x);
    deserializes_to("4.242e1", x);
    deserializes_to(".4242e2", x);
    deserializes_to("0.4242e2", x);
    deserializes_to("-42.42", -x);
    deserializes_to("-42.42", -x);
    deserializes_to("-42.42", -x);
    deserializes_to("-42.42e0", -x);
    deserializes_to("-4.242e1", -x);
    deserializes_to("-.4242e2", -x);
    deserializes_to("-0.4242e2", -x);
}

#[test]
fn deserializes_char() {
    deserializes_to("'x'", 'x');
    deserializes_to("\"자\"", '자');
}

#[test]
#[ignore] // TODO currently unsupported
fn deserializes_str() {
    deserializes_to("'Hello!'", "Hello!");
    deserializes_to("\"안녕하세요\"", "안녕하세요");
}

#[test]
fn deserializes_string() {
    deserializes_to("'Hello!'", "Hello!".to_owned());
    deserializes_to("\"안녕하세요\"", "안녕하세요".to_owned());
}

#[test]
#[ignore] // TODO currently unsupported
fn deserializes_bytes() {}

#[test]
#[ignore] // TODO currently unsupported
fn deserializes_byte_buf() {}

#[test]
fn deserializes_option() {
    deserializes_to::<Option<i32>>("null", None);
    deserializes_to("42", Some(42));
    deserializes_to("42", Some(Some(42)));
}

#[test]
fn deserializes_unit() {
    deserializes_to("null", ());
}

#[test]
fn deserializes_unit_struct() {
    #[derive(Deserialize, PartialEq, Debug)]
    struct A;
    deserializes_to("null", A);
}

#[test]
fn deserializes_newtype_struct() {
    #[derive(Deserialize, PartialEq, Debug)]
    struct A(i32);

    #[derive(Deserialize, PartialEq, Debug)]
    struct B(f64);

    deserializes_to("42", A(42));
    deserializes_to("42", B(42.));
}

#[test]
fn deserializes_seq() {
    #[derive(Deserialize, PartialEq, Debug)]
    #[serde(untagged)]
    enum Val {
        Number(f64),
        Bool(bool),
        String(String),
    }

    deserializes_to("[1, 2, 3]", vec![1, 2, 3]);
    deserializes_to(
        "[42, true, 'hello']",
        vec![
            Val::Number(42.),
            Val::Bool(true),
            Val::String("hello".to_owned()),
        ],
    )
}

#[test]
fn deserializes_tuple() {
    deserializes_to("[1, 2, 3]", (1, 2, 3));
}

#[test]
fn deserializes_tuple_struct() {
    #[derive(Deserialize, PartialEq, Debug)]
    struct A(i32, f64);

    #[derive(Deserialize, PartialEq, Debug)]
    struct B(f64, i32);

    deserializes_to("[1, 2]", A(1, 2.));
    deserializes_to("[1, 2]", B(1., 2));
}

#[test]
fn deserializes_map() {
    let mut m = HashMap::new();
    m.insert("a".to_owned(), 1);
    m.insert("b".to_owned(), 2);
    m.insert("c".to_owned(), 3);

    deserializes_to("{ a: 1, 'b': 2, \"c\": 3 }", m);
}

#[test]
fn deserializes_struct() {
    #[derive(Deserialize, PartialEq, Debug)]
    struct S {
        a: i32,
        b: i32,
        c: i32,
    }

    deserializes_to("{ a: 1, 'b': 2, \"c\": 3 }", S { a: 1, b: 2, c: 3 });
}

#[test]
fn deserializes_enum() {
    #[derive(Deserialize, PartialEq, Debug)]
    enum E {
        A,
        B(i32),
        C(i32, i32),
        D { a: i32, b: i32 },
    }

    deserializes_to("'A'", E::A);
    deserializes_to("{ B: 2 }", E::B(2));
    deserializes_to("{ C: [3, 5] }", E::C(3, 5));
    deserializes_to("{ D: { a: 7, b: 11 } }", E::D { a: 7, b: 11 });
}

#[test]
fn deserializes_ignored() {
    #[derive(Deserialize, PartialEq, Debug)]
    struct S {
        a: i32,
        b: i32,
    }

    deserializes_to("{ a: 1, ignored: 42, b: 2 }", S { a: 1, b: 2 });
}

#[test]
fn deserializes_json_values() {
    // As int if json uses int type.
    deserializes_to("0x2a", serde_json::json!(42));
    deserializes_to("0x2A", serde_json::json!(42));
    deserializes_to("0X2A", serde_json::json!(42));
    deserializes_to("42", serde_json::json!(42));

    // As float if json calls for explicit float type.
    deserializes_to("42.", serde_json::json!(42.));
    deserializes_to("42e0", serde_json::json!(42.));
    deserializes_to("4e2", serde_json::json!(400.));
    deserializes_to("4e2", serde_json::json!(4e2));
}

#[test]
fn deserialize_error_messages() {
    #[derive(Deserialize, PartialEq, Debug)]
    enum E {
        A,
    }
    deserializes_with_error("'B'", E::A, "unknown variant `B`, expected `A`");

    deserializes_with_error("0xffffffffff", 42, "error parsing hex");

    let mut over_i64 = i64::max_value().to_string();
    over_i64.push_str("0");
    deserializes_with_error(
        over_i64.as_str(),
        serde_json::json!(42),
        "error parsing integer",
    );

    deserializes_with_error("1e309", 42, "error parsing number: too large");
}
