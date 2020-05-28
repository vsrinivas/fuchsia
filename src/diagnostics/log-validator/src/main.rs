// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fidl_fuchsia_diagnostics_stream::{Argument, Record, Severity, Value},
    fidl_fuchsia_validate_logs, fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher},
    pretty_assertions::assert_eq,
    std::convert::TryInto,
};

/// Validate Log VMO formats written by 'puppet' programs controlled by
/// this Validator program.
#[derive(Debug, FromArgs)]
struct Opt {
    /// required arg: The URL of the puppet
    #[argh(option, long = "url")]
    puppet_url: String,
}

type TestCase = (String, Record, Vec<u8>);

fn test_string() -> TestCase {
    let timestamp = 12;
    let arg = Argument { name: String::from("hello"), value: Value::Text("world".to_string()) };
    let record = Record { timestamp, severity: Severity::Info, arguments: vec![arg] };
    // 5: represents the size of the record
    // 9: represents the type of Record (Log record)
    // 30: represents the INFO severity
    let mut expected_record_header = vec![0x59, 0, 0, 0, 0, 0, 0, 0x30];
    let mut expected_time_stamp = vec![0xC, 0, 0, 0, 0, 0, 0, 0];
    // 3: represents the size of argument
    // 6: represents the value type
    // 5, 0x80: string ref for NameRef
    // second 5, 0x80: string ref for ValueRef
    let mut expected_arg_header = vec![0x36, 0, 0x5, 0x80, 0x5, 0x80, 0, 0];
    // Representation of "hello"
    let mut expected_arg_name = vec![0x68, 0x65, 0x6C, 0x6C, 0x6F, 0, 0, 0];
    // Representation of "world"
    let mut expected_test_value = vec![0x77, 0x6F, 0x72, 0x6C, 0x64, 0, 0, 0];

    let mut expected_result = vec![];
    expected_result.append(&mut expected_record_header);
    expected_result.append(&mut expected_time_stamp);
    expected_result.append(&mut expected_arg_header);
    expected_result.append(&mut expected_arg_name);
    expected_result.append(&mut expected_test_value);
    ("test_string".to_string(), record, expected_result)
}

fn test_float() -> TestCase {
    let timestamp = 6;
    let arg = Argument { name: String::from("name"), value: Value::Floating(3.1415) };
    let record = Record { timestamp, severity: Severity::Warn, arguments: vec![arg] };
    // Record header = 0x59, 0, 0, 0, 0, 0, 0, 0x40
    // Timestamp = 0x6, 0, 0, 0, 0, 0, 0, 0
    // Arg Header = 0x35, 0, 0x4, 0x80, 0, 0, 0, 0
    // Arg name = 0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0
    // Arg value = 0x6F, 0x12, 0x83, 0xC0, 0xCA, 0x21, 0x09, 0x40
    let expected_result = vec![
        0x59, 0, 0, 0, 0, 0, 0, 0x40, 0x6, 0, 0, 0, 0, 0, 0, 0, 0x35, 0, 0x4, 0x80, 0, 0, 0, 0,
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0, 0x6F, 0x12, 0x83, 0xC0, 0xCA, 0x21, 0x09, 0x40,
    ];
    ("test_float".to_string(), record, expected_result)
}

fn test_unsigned_int() -> TestCase {
    let timestamp = 6;
    let arg = Argument { name: String::from("name"), value: Value::UnsignedInt(3) };
    let record = Record { timestamp, severity: Severity::Debug, arguments: vec![arg] };
    // Record header = 0x59, 0, 0, 0, 0, 0, 0, 0x20
    // Timestamp = 0x6, 0, 0, 0, 0, 0, 0, 0
    // Arg Header = 0x34, 0, 0x4, 0x80, 0, 0, 0, 0
    // Arg name = 0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0
    // Arg value = 0x3, 0, 0, 0, 0, 0, 0, 0
    let expected_result = vec![
        0x59, 0, 0, 0, 0, 0, 0, 0x20, 0x6, 0, 0, 0, 0, 0, 0, 0, 0x34, 0, 0x4, 0x80, 0, 0, 0, 0,
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0, 0x3, 0, 0, 0, 0, 0, 0, 0,
    ];
    ("test_unsigned_int".to_string(), record, expected_result)
}

fn test_signed_int_negative() -> TestCase {
    let timestamp = 9;
    let arg = Argument { name: String::from("name"), value: Value::SignedInt(-7) };
    let record = Record { timestamp, severity: Severity::Error, arguments: vec![arg] };
    // Record header = 0x59, 0, 0, 0, 0, 0, 0, 0x50
    // Timestamp = 0x9, 0, 0, 0, 0, 0, 0, 0
    // Arg Header = 0x33, 0, 0x4, 0x80, 0, 0, 0, 0
    // Arg name = 0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0
    // Arg value = 0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    let expected_result = vec![
        0x59, 0, 0, 0, 0, 0, 0, 0x50, 0x9, 0, 0, 0, 0, 0, 0, 0, 0x33, 0, 0x4, 0x80, 0, 0, 0, 0,
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0, 0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    ];
    ("test_signed_int_negative".to_string(), record, expected_result)
}

fn test_signed_int_positive() -> TestCase {
    let timestamp = 9;
    let arg = Argument { name: String::from("name"), value: Value::SignedInt(4) };
    let record = Record { timestamp, severity: Severity::Warn, arguments: vec![arg] };
    // Record header = 0x59, 0, 0, 0, 0, 0, 0, 0x40
    // Timestamp = 0x9, 0, 0, 0, 0, 0, 0, 0
    // Arg Header = 0x33, 0, 0x4, 0x80, 0, 0, 0, 0
    // Arg name = 0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0
    // Arg value = 0x4, 0, 0, 0, 0, 0, 0, 0
    let expected_result = vec![
        0x59, 0, 0, 0, 0, 0, 0, 0x40, 0x9, 0, 0, 0, 0, 0, 0, 0, 0x33, 0, 0x4, 0x80, 0, 0, 0, 0,
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0, 0x4, 0, 0, 0, 0, 0, 0, 0,
    ];
    ("test_signed_int_positive".to_string(), record, expected_result)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).unwrap();
    let Opt { puppet_url } = argh::from_env();

    let launcher = launcher().unwrap();
    let app = launch(&launcher, puppet_url.to_string(), None).unwrap();

    let proxy = app.connect_to_service::<fidl_fuchsia_validate_logs::ValidateMarker>()?;

    let arr: Vec<&dyn Fn() -> TestCase> = vec![
        &test_signed_int_positive,
        &test_signed_int_negative,
        &test_unsigned_int,
        &test_float,
        &test_string,
    ];
    let mut expected = vec![];
    let mut actual = vec![];
    for f in &arr {
        let mut test_case = (f)();
        let result = proxy.log(&mut test_case.1).await?.expect("Unable to get Record");
        let size = result.size;
        let vmo = result.vmo;
        let test_name = test_case.0;
        let mut buffer = vec![0; size.try_into().expect("Unable to convert size")];
        vmo.read(&mut buffer, 0)?;
        expected.push((test_name.clone(), test_case.2));
        actual.push((test_name, buffer));
    }
    assert_eq!(expected, actual);
    log::info!("Ran {:?} tests successfully", arr.len());
    Ok(())
}
