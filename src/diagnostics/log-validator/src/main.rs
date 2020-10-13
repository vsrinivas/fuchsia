// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context,
    anyhow::Error,
    argh::FromArgs,
    diagnostics_stream::parse::parse_record as parse,
    fidl_fuchsia_diagnostics::Severity,
    fidl_fuchsia_diagnostics_stream::{Argument, Record, Value},
    fidl_fuchsia_logger::LogSinkRequest,
    fidl_fuchsia_logger::LogSinkRequestStream,
    fidl_fuchsia_validate_logs, fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher},
    fuchsia_component::server::ServiceFs,
    futures::channel::mpsc::channel,
    futures::channel::mpsc::Receiver,
    futures::channel::mpsc::Sender,
    futures::prelude::*,
    log::*,
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
    /// required arg: Whether or not to test the log sink
    #[argh(switch)]
    test_log_sink: bool,
}

type TestCase = (&'static str, Record, Vec<u8>);

fn test_string() -> TestCase {
    let timestamp = 12;
    let arg = Argument { name: String::from("hello"), value: Value::Text("world".to_string()) };
    let record = Record { timestamp, severity: Severity::Info, arguments: vec![arg] };
    // 5: represents the size of the record
    // 9: represents the type of Record (Log record)
    // 0x30: represents the INFO severity
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
    ("test_string", record, expected_result)
}

fn test_multiword_string() -> TestCase {
    let timestamp = 0x24;
    let arg =
        Argument { name: String::from("name"), value: Value::Text(String::from("aaaaaaabbb")) };
    let record = Record { timestamp, severity: Severity::Warn, arguments: vec![arg] };

    #[rustfmt::skip]
    let expected_result = vec![
        // Record header - 9 for log type, 6 for log size, 0x40 for WARN severity
        0x69, 0, 0, 0, 0, 0, 0, 0x40,
        // timestamp
        0x24, 0, 0, 0, 0, 0, 0, 0,
        // argument header - 6 for value type, 4 for arg size, 0x4/0x80 for NameRef
        // 0xa/0x80 for ValueRef
        0x46, 0, 0x4, 0x80, 0xa, 0x80, 0, 0,
        // "name" representation with padding
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0,
        // "aaaaaaabbb" with padding
        0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x62,
        0x62, 0x62, 0, 0, 0, 0, 0, 0,
    ];
    ("test_multiword_string", record, expected_result)
}

fn test_empty_string() -> TestCase {
    let timestamp = 0x24;
    let arg = Argument { name: String::from("name"), value: Value::Text(String::from("")) };
    let record = Record { timestamp, severity: Severity::Warn, arguments: vec![arg] };

    #[rustfmt::skip]
    let expected_result = vec![
        // Record header - 9 for log type, 4 for log size, 0x40 for WARN severity
        0x49, 0, 0, 0, 0, 0, 0, 0x40,
        // timestamp
        0x24, 0, 0, 0, 0, 0, 0, 0,
        // argument header - 6 for value type, 2 for arg size, 0x4/0x80 for NameRef
        // ValueRef is 0 for empty string
        0x26, 0, 0x4, 0x80, 0, 0, 0, 0,
        // "name" representation with padding
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0,
        // (empty string, no bytes)
    ];
    ("test_empty_string", record, expected_result)
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
    ("test_float", record, expected_result)
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
    ("test_unsigned_int", record, expected_result)
}

fn test_unsigned_int_max() -> TestCase {
    let timestamp = 6;
    let arg = Argument { name: String::from("name"), value: Value::UnsignedInt(u64::MAX) };
    let record = Record { timestamp, severity: Severity::Debug, arguments: vec![arg] };
    // Record header = 0x59, 0, 0, 0, 0, 0, 0, 0x20
    // Timestamp = 0x6, 0, 0, 0, 0, 0, 0, 0
    // Arg Header = 0x34, 0, 0x4, 0x80, 0, 0, 0, 0
    // Arg name = 0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0
    // Arg value = 0xff, 0xff, ...
    let expected_result = vec![
        0x59, 0, 0, 0, 0, 0, 0, 0x20, 0x6, 0, 0, 0, 0, 0, 0, 0, 0x34, 0, 0x4, 0x80, 0, 0, 0, 0,
        0x6E, 0x61, 0x6D, 0x65, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    ];
    ("test_unsigned_int", record, expected_result)
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
    ("test_signed_int_negative", record, expected_result)
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
    ("test_signed_int_positive", record, expected_result)
}

fn test_multiword_arg_name() -> TestCase {
    let timestamp = 0x4523;
    let arg = Argument { name: String::from("abcdabcdabcd"), value: Value::SignedInt(9) };
    let record = Record { timestamp, severity: Severity::Error, arguments: vec![arg] };
    #[rustfmt::skip]
    let expected_result = vec![
        // record header
        0x69, 0, 0, 0, 0, 0, 0, 0x50,
        // timestamp
        0x23, 0x45, 0, 0, 0, 0, 0, 0,
        // arg header - record size 4, NameRef 0xC/0x80
        0x43, 0, 0xC, 0x80, 0, 0, 0, 0,
        // arg name "abcdabcdabcd"
        0x61, 0x62, 0x63, 0x64, 0x61, 0x62, 0x63, 0x64,
        0x61, 0x62, 0x63, 0x64, 0, 0, 0, 0,
        // arg value
        0x9, 0, 0, 0, 0, 0, 0, 0,
    ];
    ("test_multiword_arg_name", record, expected_result)
}

fn test_word_size_arg_name() -> TestCase {
    let timestamp = 0x4523;
    let arg = Argument { name: String::from("abcdabcd"), value: Value::SignedInt(9) };
    let record = Record { timestamp, severity: Severity::Error, arguments: vec![arg] };
    #[rustfmt::skip]
    let expected_result = vec![
        // record header
        0x59, 0, 0, 0, 0, 0, 0, 0x50,
        // timestamp
        0x23, 0x45, 0, 0, 0, 0, 0, 0,
        // arg header - record size 4, NameRef 0x8/0x80
        0x33, 0, 0x8, 0x80, 0, 0, 0, 0,
        // arg name "abcdabcd" (no padding - already word-aligned)
        0x61, 0x62, 0x63, 0x64, 0x61, 0x62, 0x63, 0x64,
        // arg value
        0x9, 0, 0, 0, 0, 0, 0, 0,
    ];
    ("test_word_size_arg_name", record, expected_result)
}

fn test_no_args() -> TestCase {
    let timestamp = 0x1234;
    let record = Record { timestamp, severity: Severity::Error, arguments: vec![] };
    #[rustfmt::skip]
    let expected_result = vec![
        // record header
        0x29, 0, 0, 0, 0, 0, 0, 0x50,
        // timestamp
        0x34, 0x12, 0, 0, 0, 0, 0, 0,
    ];
    ("test_no_args", record, expected_result)
}

fn test_multiple_args() -> TestCase {
    let timestamp = 0xabcd;
    let arguments = vec![
        Argument { name: String::from("aa"), value: Value::SignedInt(3) },
        Argument { name: String::from("bbb"), value: Value::UnsignedInt(0x90) },
    ];
    let record = Record { timestamp, severity: Severity::Error, arguments };
    #[rustfmt::skip]
    let expected_result = vec![
        // record header
        0x89, 0, 0, 0, 0, 0, 0, 0x50,
        // timestamp
        0xcd, 0xab, 0, 0, 0, 0, 0, 0,
        // arg 1 header
        0x33, 0, 0x2, 0x80, 0, 0, 0, 0,
        // arg name "aa"
        0x61, 0x61, 0, 0, 0, 0, 0, 0,
        // arg value
        0x3, 0, 0, 0, 0, 0, 0, 0,
        // arg 2 header
        0x34, 0, 0x3, 0x80, 0, 0, 0, 0,
        // arg name "bbb"
        0x62, 0x62, 0x62, 0, 0, 0, 0, 0,
        // arg value
        0x90, 0, 0, 0, 0, 0, 0, 0,
    ];
    ("test_multiple_args", record, expected_result)
}

async fn test_socket(s: &fasync::Socket, puppet_url: String) {
    let mut buf: Vec<u8> = vec![];
    // TODO(fxbug.dev/61495): Validate that this is in fact a datagram socket.
    let bytes_read = s.read_datagram(&mut buf).await.unwrap();
    let result = parse(&buf[0..bytes_read]).unwrap();
    assert_eq!(result.0.arguments[0].name, "pid");
    assert_eq!(result.0.arguments[1].name, "tid");
    assert_eq!(result.0.arguments[2].name, "tag");
    assert!(
        matches!(&result.0.arguments[2].value, diagnostics_stream::Value::Text(v) if *v == puppet_url.rsplit('/').next().unwrap())
    );
    // TODO(fxbug.dev/61538) validate we can log arbitrary messages
    assert_eq!(result.0.arguments[3].name, "tag");
    assert!(
        matches!(&result.0.arguments[3].value, diagnostics_stream::Value::Text(v) if *v == "test_log")
    );
    assert_eq!(result.0.arguments[4].name, "foo");
    assert!(
        matches!(&result.0.arguments[4].value, diagnostics_stream::Value::Text(v) if *v == "bar")
    );
}

enum IncomingRequest {
    LogProviderRequest(LogSinkRequestStream),
}

async fn retrieve_sockets_from_logsink(
    mut stream: LogSinkRequestStream,
    mut channel: Sender<fidl::Socket>,
) -> Result<(), Error> {
    let request = stream.next().await;
    match request {
        Some(Ok(LogSinkRequest::Connect { socket: _, control_handle: _ })) => {
            panic!("shouldn't ever receive legacy connections");
        }
        Some(Ok(LogSinkRequest::ConnectStructured { socket, control_handle: _ })) => {
            info!("This happened! We got a structured connection.");
            channel.send(socket).await?;
        }
        None => (),
        Some(Err(e)) => panic!("log sink request failure: {:?}", e),
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).unwrap();
    let Opt { puppet_url, test_log_sink } = argh::from_env();
    let (tx, mut rx): (Sender<fidl::Socket>, Receiver<fidl::Socket>) = channel(1);

    let (_env, app) = if test_log_sink {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(IncomingRequest::LogProviderRequest);
        let (_env, app) = fs
            .launch_component_in_nested_environment(
                puppet_url.clone(),
                None,
                "log_validator_puppet",
            )
            .unwrap();
        // Wait for the puppet to connect to our fake log service
        fs.take_and_serve_directory_handle()?;
        let _future = fasync::Task::spawn(async move {
            while let Some(IncomingRequest::LogProviderRequest(stream)) = fs.next().await {
                let tx_local = tx.clone();
                retrieve_sockets_from_logsink(stream, tx_local)
                    .await
                    .context("couldn't retrieve sockets")
                    .unwrap();
            }
        });
        (Some((_env, _future)), app)
    } else {
        let launcher = launcher().unwrap();
        let app = launch(&launcher, puppet_url.to_string(), None)?;
        (None, app)
    };
    let proxy = app.connect_to_service::<fidl_fuchsia_validate_logs::ValidateMarker>()?;

    let arr: Vec<&dyn Fn() -> TestCase> = vec![
        &test_signed_int_positive,
        &test_signed_int_negative,
        &test_unsigned_int,
        &test_float,
        &test_string,
        &test_multiword_string,
        &test_empty_string,
        &test_multiword_arg_name,
        &test_word_size_arg_name,
        &test_unsigned_int_max,
        &test_no_args,
        &test_multiple_args,
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

    if test_log_sink {
        info!("We're running the log sink test!");
        let socket = fasync::Socket::from_socket(rx.next().await.unwrap()).unwrap();

        test_socket(&socket, puppet_url).await;
    }
    log::info!("Ran {:?} tests successfully", arr.len());
    Ok(())
}
