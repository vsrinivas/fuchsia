// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use pretty_assertions::assert_eq;
use qmigen_lib::{ast, codegen};

mod macros;

#[test]
fn optional_response() {
    let source = include_str!("optional_response.test.json");
    let mut output = vec![];
    let mut c = codegen::Codegen::new(&mut output);
    let mut svc_set = ast::ServiceSet::new();
    svc_set.parse_service_file(source.as_bytes()).unwrap();
    for svc in svc_set.get_services() {
        let structure = svc_set.get_structure(&svc.message_structure).unwrap();
        for msg in svc.get_messages() {
            c.codegen_svc_decode(svc.ty, msg, &structure).unwrap();
        }
    }
    let output = String::from_utf8(output).unwrap();
    assert_eq!(output, include_str!("optional_response.test.rs"))
}

#[test]
fn subparam_request() {
    let source = include_str!("subparam_request.test.json");
    let mut output = vec![];
    let mut c = codegen::Codegen::new(&mut output);
    let mut svc_set = ast::ServiceSet::new();
    svc_set.parse_service_file(source.as_bytes()).unwrap();
    for svc in svc_set.get_services() {
        let structure = svc_set.get_structure(&svc.message_structure).unwrap();
        for msg in svc.get_messages() {
            c.codegen_svc_encode(svc.ty, msg, &structure).unwrap();
        }
    }
    let output = String::from_utf8(output).unwrap();
    assert_eq!(output, include_str!("subparam_request.test.rs"));
}

#[test]
fn subparam_response() {
    let source = include_str!("subparam_response.test.json");
    let mut output = vec![];
    let mut c = codegen::Codegen::new(&mut output);
    let mut svc_set = ast::ServiceSet::new();
    svc_set.parse_service_file(source.as_bytes()).unwrap();
    for svc in svc_set.get_services() {
        let structure = svc_set.get_structure(&svc.message_structure).unwrap();
        for msg in svc.get_messages() {
            c.codegen_svc_decode(svc.ty, msg, &structure).unwrap();
        }
    }
    let output = String::from_utf8(output).unwrap();
    assert_eq!(output, include_str!("subparam_response.test.rs"));
}

#[test]
fn string_request_decode() {
    let source = include_str!("string_request_decode.test.json");
    let mut output = vec![];
    let mut c = codegen::Codegen::new(&mut output);
    let mut svc_set = ast::ServiceSet::new();
    svc_set.parse_service_file(source.as_bytes()).unwrap();
    for svc in svc_set.get_services() {
        let structure = svc_set.get_structure(&svc.message_structure).unwrap();
        for msg in svc.get_messages() {
            c.codegen_svc_decode(svc.ty, msg, &structure).unwrap();
        }
    }
    let output = String::from_utf8(output).unwrap();
    assert_eq!(output, include_str!("string_request_decode.test.rs"));
}

#[test]
fn simple_response() {
    let mut code_gen = generate_source_code!(include_str!("simple_response.test.json"));
    let mut err_body = consume_header!(code_gen);
    let body = consume_empty_err!(err_body);
    assert_eq!(body, include_str!("simple_response.test.rs"))
}

#[test]
fn simple_request() {
    let mut code_gen = generate_source_code!(include_str!("simple_request.test.json"));
    let mut err_body = consume_header!(code_gen);
    let body = consume_empty_err!(err_body);
    assert_eq!(body, include_str!("simple_request.test.rs"));
}

#[test]
fn generate_service_stub() {
    let mut code_gen = generate_source_code!(
        r#"{
      "structures": [
        {
          "type": "standard",
          "transaction_len": 2
        }
      ],
      "services": [{
        "name": "TEST",
        "type": "0x42",
        "message_structure": "standard",
        "result_structure": "standard",
        "messages": []
        }]
    }"#
    );

    let mut err_body = consume_header!(code_gen);
    let body = consume_empty_err!(err_body);

    assert_eq!(
        body,
        r#"pub mod TEST {
    use crate::{Decodable, Encodable};
    use crate::QmiError;
    use bytes::{Bytes, Buf, BufMut, BytesMut};
    const MSG_MAX: usize = 512;
}
"#
    );
}
