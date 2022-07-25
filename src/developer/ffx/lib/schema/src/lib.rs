// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generate schemas for types that implement the serde::Deserialize trait.
//!
//!     let mut gen = Generator::new(Default::default());
//!     gen.resolve_type::<MyType>()?;
//!     let schema = gen.schema()?;
//!
//! The returned schema implements the serde::Serialize trait. It is possible to output that in
//! whatever format you want. Alternatively, you can inspect the schema directly as it contains
//! types from the ast module.
//!
//! The methodology used here gets a little tricky with enums. By default, we attempt to resolve
//! all enum variants even within nested types. If you call `resolve_type` with all enum types
//! that you transitively depend on as well as the top level type, then you can ensure you won't
//! have to deal with any of the trickery required to handle enums not at the top level.
//!
//!     let config = GeneratorConfig::default();
//!     let mut gen = Generator::new(config);
//!     gen.resolve_type::<OtherNestedEnum>()?;
//!     gen.resolve_type::<SomeEnum>()?;
//!     gen.resolve_type::<MyType>()?;
//!     let schema = gen.schema()?;

mod de;

mod errors;

pub mod ast;
pub mod schema;

pub use errors::Error;
pub use schema::{Generator, GeneratorConfig, Schema};

#[cfg(test)]
mod test {
    use super::*;
    use serde::{Deserialize, Serialize};
    use std::collections::HashMap;

    #[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
    enum Hello {
        Unit,
        Newtype(u16),
        Tuple(u16, Option<bool>),
        Struct { a: u32, furry: Option<Vec<Fuzzy>> },
        NewTupleArray((u16, u16, u16)),
        Buzz(Foo<u32>),
        Fee { z: HashMap<String, ()> },
    }

    #[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
    struct Foo<T> {
        bar: Vec<T>,
    }

    #[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
    enum Fuzzy {
        A,
        B { x: i32 },
    }

    #[test]
    fn simple_types_work() {
        let mut generator = Generator::new(Default::default());
        generator.resolve_type::<Fuzzy>().unwrap();
        let schema = generator.schema().unwrap();
        let output = serde_json::to_string(&schema).unwrap();
        let expected = r##"{"Fuzzy":{"Enum":{"0":{"name":"A","value":"Unit"},"1":{"name":"B","value":{"Struct":[{"name":"x","value":"I32"}]}}}}}"##;
        assert_eq!(output, expected);
    }

    #[test]
    fn complex_types_work() {
        let mut generator = Generator::new(Default::default());
        generator.resolve_type::<Hello>().unwrap();
        let schema = generator.schema().unwrap();
        let output = serde_json::to_string(&schema).unwrap();
        let expected = r##"{"Foo":{"Struct":[{"name":"bar","value":{"Seq":"U32"}}]},"Fuzzy":{"Enum":{"0":{"name":"A","value":"Unit"},"1":{"name":"B","value":{"Struct":[{"name":"x","value":"I32"}]}}}},"Hello":{"Enum":{"0":{"name":"Unit","value":"Unit"},"1":{"name":"Newtype","value":{"NewType":"U16"}},"2":{"name":"Tuple","value":{"Tuple":["U16",{"Option":"Bool"}]}},"3":{"name":"Struct","value":{"Struct":[{"name":"a","value":"U32"},{"name":"furry","value":{"Option":{"Seq":{"TypeName":"Fuzzy"}}}}]}},"4":{"name":"NewTupleArray","value":{"NewType":{"TupleArray":{"content":"U16","size":3}}}},"5":{"name":"Buzz","value":{"NewType":{"TypeName":"Foo"}}},"6":{"name":"Fee","value":{"Struct":[{"name":"z","value":{"Map":{"key":"Str","value":"Unit"}}}]}}}}}"##;
        assert_eq!(output, expected);
    }

    #[test]
    fn works_if_all_enums_are_resolved_ahead_of_time() {
        let mut generator = Generator::new(Default::default());
        generator.resolve_type::<Fuzzy>().unwrap();
        generator.resolve_type::<Hello>().unwrap();
        let schema = generator.schema().unwrap();
        let output = serde_json::to_string(&schema).unwrap();
        let expected = r##"{"Foo":{"Struct":[{"name":"bar","value":{"Seq":"U32"}}]},"Fuzzy":{"Enum":{"0":{"name":"A","value":"Unit"},"1":{"name":"B","value":{"Struct":[{"name":"x","value":"I32"}]}}}},"Hello":{"Enum":{"0":{"name":"Unit","value":"Unit"},"1":{"name":"Newtype","value":{"NewType":"U16"}},"2":{"name":"Tuple","value":{"Tuple":["U16",{"Option":"Bool"}]}},"3":{"name":"Struct","value":{"Struct":[{"name":"a","value":"U32"},{"name":"furry","value":{"Option":{"Seq":{"TypeName":"Fuzzy"}}}}]}},"4":{"name":"NewTupleArray","value":{"NewType":{"TupleArray":{"content":"U16","size":3}}}},"5":{"name":"Buzz","value":{"NewType":{"TypeName":"Foo"}}},"6":{"name":"Fee","value":{"Struct":[{"name":"z","value":{"Map":{"key":"Str","value":"Unit"}}}]}}}}}"##;
        assert_eq!(output, expected);
    }

    #[test]
    fn the_depth_limit_works() {
        let config = GeneratorConfig::new(1);
        let mut generator = Generator::new(config);
        let err = generator.resolve_type::<Hello>().unwrap_err();
        match err {
            Error::OverDepthLimit(1) => assert!(true),
            _ => assert!(false, "unexpected error: {:#?}", err),
        }
    }
}
