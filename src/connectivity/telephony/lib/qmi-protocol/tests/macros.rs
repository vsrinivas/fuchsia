// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
macro_rules! consume_empty_err {
    ($data:expr) => {{
        let header = r#"#[derive(Debug, Error)]
pub enum QmiError {
}
impl QmiError {
    pub fn from_code(code: u16) -> Self {
        match code {
            _c => panic!("Unknown Error Code: {}", _c),
        }
    }
}
"#;
        let h = $data.split_off(header.len());
        assert_eq!($data, header);
        h
    }};
}

#[macro_export]
macro_rules! consume_header {
    ($data:expr) => {{
        let header = r#"// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused_mut, non_snake_case)]
use thiserror::Error;
use bytes::{Bytes, Buf};
use std::fmt::Debug;
use std::result;

pub type QmiResult<T> = result::Result<T, QmiError>;

pub trait Encodable {
    type DecodeResult;

    fn to_bytes(&self) -> (Bytes, u16);

    fn transaction_id_len(&self) -> u8;

    fn svc_id(&self) -> u8;
}

pub trait Decodable {
    fn from_bytes<T: Buf + Debug>(b: T) -> QmiResult<Self> where Self: Sized;
}


"#;
        let h = $data.split_off(header.len());
        assert_eq!($data, header);
        h
    }};
}

#[macro_export]
macro_rules! generate_source_code {
    ($data:expr) => {{
        let mut output = vec![];
        let mut c = codegen::Codegen::new(&mut output);
        let mut svc_set = ast::ServiceSet::new();
        svc_set.parse_service_file($data.as_bytes()).unwrap();
        c.codegen(svc_set).unwrap();
        String::from_utf8(output.clone()).unwrap()
    }};
}
