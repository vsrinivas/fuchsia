// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::*;
use diagnostics_data::*;
use diagnostics_log_encoding::{encode::Encoder, Argument, Record};
use fidl_fuchsia_diagnostics::Severity as StreamSeverity;
use fidl_fuchsia_logger::{LogLevelFilter, LogMessage};
use lazy_static::lazy_static;
use matches::assert_matches;
use std::{io::Cursor, sync::Arc};

lazy_static! {
    static ref TEST_IDENTITY: Arc<MonikerWithUrl> = {
        Arc::new(MonikerWithUrl {
            moniker: "fake-test-env/test-component.cmx".to_string(),
            url: "fuchsia-pkg://fuchsia.com/testing123#test-component.cmx".to_string(),
        })
    };
}

fn clear_legacy_verbosity(data: &mut LogsData) {
    data.payload_message_mut()
        .unwrap()
        .properties
        .retain(|p| !matches!(p, LogsProperty::Int(LogsField::Verbosity, _)));
}

#[repr(C, packed)]
struct fx_log_metadata_t_packed {
    pid: zx::sys::zx_koid_t,
    tid: zx::sys::zx_koid_t,
    time: zx::sys::zx_time_t,
    severity: fx_log_severity_t,
    dropped_logs: u32,
}

#[repr(C, packed)]
struct fx_log_packet_t_packed {
    metadata: fx_log_metadata_t_packed,
    /// Contains concatenated tags and message and a null terminating character at the end.
    /// `char(tag_len) + "tag1" + char(tag_len) + "tag2\0msg\0"`
    data: [c_char; MAX_DATAGRAM_LEN - METADATA_SIZE],
}

#[test]
fn abi_test() {
    assert_eq!(METADATA_SIZE, 32);
    assert_eq!(MAX_TAGS, 5);
    assert_eq!(MAX_TAG_LEN, 64);
    assert_eq!(mem::size_of::<fx_log_metadata_t>(), METADATA_SIZE);
    assert_eq!(mem::size_of::<fx_log_packet_t>(), MAX_DATAGRAM_LEN);

    // Test that there is no padding
    assert_eq!(mem::size_of::<fx_log_packet_t>(), mem::size_of::<fx_log_packet_t_packed>());

    assert_eq!(mem::size_of::<fx_log_metadata_t>(), mem::size_of::<fx_log_metadata_t_packed>());
}
fn test_packet() -> fx_log_packet_t {
    let mut packet: fx_log_packet_t = Default::default();
    packet.metadata.pid = 1;
    packet.metadata.tid = 2;
    packet.metadata.time = 3;
    packet.metadata.severity = LogLevelFilter::Debug as i32;
    packet.metadata.dropped_logs = 10;
    packet
}

fn get_test_identity() -> MonikerWithUrl {
    (**TEST_IDENTITY).clone()
}

#[test]
fn short_reads() {
    let packet = test_packet();
    let one_short = &packet.as_bytes()[..METADATA_SIZE];
    let two_short = &packet.as_bytes()[..METADATA_SIZE - 1];

    assert_eq!(LoggerMessage::try_from(one_short), Err(MessageError::ShortRead { len: 32 }));

    assert_eq!(LoggerMessage::try_from(two_short), Err(MessageError::ShortRead { len: 31 }));
}

#[test]
fn unterminated() {
    let mut packet = test_packet();
    let end = 9;
    packet.data[end] = 1;

    let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + end];
    let parsed = LoggerMessage::try_from(buffer);

    assert_eq!(parsed, Err(MessageError::NotNullTerminated { terminator: 1 }));
}

#[test]
fn tags_no_message() {
    let mut packet = test_packet();
    let end = 12;
    packet.data[0] = end as c_char - 1;
    packet.fill_data(1..end, 'A' as _);
    packet.data[end] = 0;

    let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + end]; // omit null-terminated
    let parsed = LoggerMessage::try_from(buffer);

    assert_eq!(parsed, Err(MessageError::OutOfBounds));
}

#[test]
fn tags_with_message() {
    let mut packet = test_packet();
    let a_start = 1;
    let a_count = 11;
    let a_end = a_start + a_count;

    packet.data[0] = a_count as c_char;
    packet.fill_data(a_start..a_end, 'A' as _);
    packet.data[a_end] = 0; // terminate tags

    let b_start = a_start + a_count + 1;
    let b_count = 5;
    let b_end = b_start + b_count;
    packet.fill_data(b_start..b_end, 'B' as _);

    let data_size = b_start + b_count;

    let buffer = &packet.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminate message
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + b_end);
    let parsed = crate::from_logger(get_test_identity(), logger_message);
    let expected = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: packet.metadata.time.into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Debug,
        size_bytes: METADATA_SIZE + b_end,
    })
    .set_dropped(packet.metadata.dropped_logs.into())
    .set_pid(packet.metadata.pid)
    .set_message("BBBBB".to_string())
    .add_tag("AAAAAAAAAAA")
    .set_tid(packet.metadata.tid)
    .build();

    assert_eq!(parsed, expected);
}

#[test]
fn placeholder_tag_replaced_with_attributed_name() {
    let mut packet = test_packet();

    let t_count = COMPONENT_NAME_PLACEHOLDER_TAG.len();
    packet.data[0] = t_count as c_char;
    let t_start = 1;
    packet.add_data(1, COMPONENT_NAME_PLACEHOLDER_TAG.as_bytes());
    let t_end = t_start + t_count;

    let a_count = 5;
    packet.data[t_end] = a_count as c_char;
    let a_start = t_end + 1;
    let a_end = a_start + a_count;
    packet.fill_data(a_start..a_end, 'A' as _);
    packet.data[a_end] = 0; // terminate tags

    let b_start = a_end + 1;
    let b_count = 5;
    let b_end = b_start + b_count;
    packet.fill_data(b_start..b_end, 'B' as _);

    let buffer = &packet.as_bytes()[..METADATA_SIZE + b_end + 1]; // null-terminate message
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + b_end);
    let parsed = crate::from_logger(get_test_identity(), logger_message);
    let expected = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: packet.metadata.time.into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Debug,
        size_bytes: METADATA_SIZE + b_end,
    })
    .set_pid(packet.metadata.pid)
    .set_dropped(packet.metadata.dropped_logs.into())
    .set_message("BBBBB".to_string())
    .add_tag(TEST_IDENTITY.moniker.clone())
    .add_tag("AAAAA")
    .set_tid(packet.metadata.tid)
    .build();
    assert_eq!(parsed, expected);
}

#[test]
fn two_tags_no_message() {
    let mut packet = test_packet();
    let a_start = 1;
    let a_count = 11;
    let a_end = a_start + a_count;

    packet.data[0] = a_count as c_char;
    packet.fill_data(a_start..a_end, 'A' as _);

    let b_start = a_end + 1;
    let b_count = 5;
    let b_end = b_start + b_count;

    packet.data[a_end] = b_count as c_char;
    packet.fill_data(b_start..b_end, 'B' as _);

    let buffer = &packet.as_bytes()[..MIN_PACKET_SIZE + b_end];
    let parsed = LoggerMessage::try_from(buffer);

    assert_eq!(parsed, Err(MessageError::OutOfBounds));
}

#[test]
fn two_tags_with_message() {
    let mut packet = test_packet();
    let a_start = 1;
    let a_count = 11;
    let a_end = a_start + a_count;

    packet.data[0] = a_count as c_char;
    packet.fill_data(a_start..a_end, 'A' as _);

    let b_start = a_end + 1;
    let b_count = 5;
    let b_end = b_start + b_count;

    packet.data[a_end] = b_count as c_char;
    packet.fill_data(b_start..b_end, 'B' as _);

    let c_start = b_end + 1;
    let c_count = 5;
    let c_end = c_start + c_count;
    packet.fill_data(c_start..c_end, 'C' as _);

    let data_size = c_start + c_count;

    let buffer = &packet.as_bytes()[..METADATA_SIZE + data_size + 1]; // null-terminated
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + data_size);
    let parsed = crate::from_logger(get_test_identity(), logger_message);
    let expected = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: packet.metadata.time.into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Debug,
        size_bytes: METADATA_SIZE + data_size,
    })
    .set_dropped(packet.metadata.dropped_logs.into())
    .set_pid(packet.metadata.pid)
    .set_message("CCCCC".to_string())
    .add_tag("AAAAAAAAAAA")
    .add_tag("BBBBB")
    .set_tid(packet.metadata.tid)
    .build();

    assert_eq!(parsed, expected);
}

#[test]
fn max_tags_with_message() {
    let mut packet = test_packet();

    let tags_start = 1;
    let tag_len = 2;
    let tag_size = tag_len + 1; // the length-prefix byte
    for tag_num in 0..MAX_TAGS {
        let start = tags_start + (tag_size * tag_num);
        let end = start + tag_len;

        packet.data[start - 1] = tag_len as c_char;
        let ascii = 'A' as c_char + tag_num as c_char;
        packet.fill_data(start..end, ascii);
    }

    let msg_start = tags_start + (tag_size * MAX_TAGS);
    let msg_len = 5;
    let msg_end = msg_start + msg_len;
    let msg_ascii = 'A' as c_char + MAX_TAGS as c_char;
    packet.fill_data(msg_start..msg_end, msg_ascii);

    let min_buffer = &packet.as_bytes()[..METADATA_SIZE + msg_end + 1]; // null-terminated
    let full_buffer = packet.as_bytes();

    let logger_message = LoggerMessage::try_from(min_buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + msg_end);
    let min_parsed = crate::from_logger(get_test_identity(), logger_message);

    let logger_message = LoggerMessage::try_from(full_buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + msg_end);
    let full_parsed =
        crate::from_logger(get_test_identity(), LoggerMessage::try_from(full_buffer).unwrap());

    let tag_properties = (0..MAX_TAGS as _)
        .map(|tag_num| String::from_utf8(vec![('A' as c_char + tag_num) as u8; tag_len]).unwrap())
        .collect::<Vec<_>>();
    let mut builder = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: packet.metadata.time.into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Debug,
        size_bytes: METADATA_SIZE + msg_end,
    })
    .set_dropped(packet.metadata.dropped_logs.into())
    .set_pid(packet.metadata.pid)
    .set_message(String::from_utf8(vec![msg_ascii as u8; msg_len]).unwrap())
    .set_tid(packet.metadata.tid);
    for tag in tag_properties {
        builder = builder.add_tag(tag);
    }
    let expected_message = builder.build();

    assert_eq!(min_parsed, expected_message);
    assert_eq!(full_parsed, expected_message);
}

#[test]
fn max_tags() {
    let mut packet = test_packet();
    let tags_start = 1;
    let tag_len = 2;
    let tag_size = tag_len + 1; // the length-prefix byte
    for tag_num in 0..MAX_TAGS {
        let start = tags_start + (tag_size * tag_num);
        let end = start + tag_len;

        packet.data[start - 1] = tag_len as c_char;
        let ascii = 'A' as c_char + tag_num as c_char;
        packet.fill_data(start..end, ascii);
    }

    let msg_start = tags_start + (tag_size * MAX_TAGS);

    let buffer_missing_terminator = &packet.as_bytes()[..METADATA_SIZE + msg_start];
    assert_eq!(
        LoggerMessage::try_from(buffer_missing_terminator),
        Err(MessageError::OutOfBounds),
        "can't parse an empty message without a nul terminator"
    );

    let buffer = &packet.as_bytes()[..METADATA_SIZE + msg_start + 1]; // null-terminated
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, 48);
    let parsed = crate::from_logger(get_test_identity(), logger_message);
    let mut builder = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: packet.metadata.time.into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Debug,
        size_bytes: 48,
    })
    .set_dropped(packet.metadata.dropped_logs as u64)
    .set_pid(packet.metadata.pid)
    .set_tid(packet.metadata.tid)
    .set_message("".to_string());
    for tag_num in 0..MAX_TAGS as _ {
        builder =
            builder.add_tag(String::from_utf8(vec![('A' as c_char + tag_num) as u8; 2]).unwrap());
    }
    assert_eq!(parsed, builder.build());
}

#[test]
fn no_tags_with_message() {
    let mut packet = test_packet();
    packet.data[0] = 0;
    packet.data[1] = 'A' as _;
    packet.data[2] = 'A' as _; // measured size ends here
    packet.data[3] = 0;

    let buffer = &packet.as_bytes()[..METADATA_SIZE + 4]; // 0 tag size + 2 byte message + null
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + 3);
    let parsed = crate::from_logger(get_test_identity(), logger_message);

    assert_eq!(
        parsed,
        LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(3).into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.moniker.clone(),
            severity: Severity::Debug,
            size_bytes: METADATA_SIZE + 3,
        })
        .set_dropped(packet.metadata.dropped_logs as u64)
        .set_pid(packet.metadata.pid)
        .set_tid(packet.metadata.tid)
        .set_message("AA".to_string())
        .build()
    );
}

#[test]
fn message_severity() {
    let mut packet = test_packet();
    packet.metadata.severity = LogLevelFilter::Info as i32;
    packet.data[0] = 0; // tag size
    packet.data[1] = 0; // null terminated

    let mut buffer = &packet.as_bytes()[..METADATA_SIZE + 2]; // tag size + null
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + 1);
    let mut parsed = crate::from_logger(get_test_identity(), logger_message);

    let mut expected_message = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: packet.metadata.time.into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Info,
        size_bytes: METADATA_SIZE + 1,
    })
    .set_pid(packet.metadata.pid)
    .set_message("".to_string())
    .set_dropped(10)
    .set_tid(packet.metadata.tid)
    .build();

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = LogLevelFilter::Trace as i32;
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Trace;

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = LogLevelFilter::Debug as i32;
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Debug;

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = LogLevelFilter::Warn as i32;
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Warn;

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = LogLevelFilter::Error as i32;
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Error;

    assert_eq!(parsed, expected_message);
}

#[test]
fn legacy_message_severity() {
    let mut packet = test_packet();
    // legacy verbosity where v=10
    packet.metadata.severity = LogLevelFilter::Info as i32 - 10;
    packet.data[0] = 0; // tag size
    packet.data[1] = 0; // null terminated

    let mut buffer = &packet.as_bytes()[..METADATA_SIZE + 2]; // tag size + null
    let logger_message = LoggerMessage::try_from(buffer).unwrap();
    assert_eq!(logger_message.size_bytes, METADATA_SIZE + 1);
    let mut parsed = crate::from_logger(get_test_identity(), logger_message);
    let mut expected_message = LogsDataBuilder::new(BuilderArgs {
        timestamp_nanos: zx::Time::from_nanos(3).into(),
        component_url: Some(TEST_IDENTITY.url.clone()),
        moniker: TEST_IDENTITY.moniker.clone(),
        severity: Severity::Debug,
        size_bytes: METADATA_SIZE + 1,
    })
    .set_dropped(packet.metadata.dropped_logs as u64)
    .set_pid(1)
    .set_tid(2)
    .set_message("".to_string())
    .build();
    clear_legacy_verbosity(&mut expected_message);
    expected_message.set_legacy_verbosity(10);

    assert_eq!(parsed, expected_message);

    // legacy verbosity where v=2
    packet.metadata.severity = LogLevelFilter::Info as i32 - 2;
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    clear_legacy_verbosity(&mut expected_message);
    expected_message.set_legacy_verbosity(2);

    assert_eq!(parsed, expected_message);

    // legacy verbosity where v=1
    packet.metadata.severity = LogLevelFilter::Info as i32 - 1;
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    clear_legacy_verbosity(&mut expected_message);
    expected_message.set_legacy_verbosity(1);

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = 0; // legacy severity
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    clear_legacy_verbosity(&mut expected_message);
    expected_message.metadata.severity = Severity::Info;

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = 1; // legacy severity
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Warn;

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = 2; // legacy severity
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Error;

    assert_eq!(parsed, expected_message);

    packet.metadata.severity = 3; // legacy severity
    buffer = &packet.as_bytes()[..METADATA_SIZE + 2];
    parsed = crate::from_logger(get_test_identity(), LoggerMessage::try_from(buffer).unwrap());
    expected_message.metadata.severity = Severity::Fatal;

    assert_eq!(parsed, expected_message);
}

#[test]
fn test_from_structured() {
    let record = Record {
        timestamp: 72,
        severity: StreamSeverity::Error,
        arguments: vec![
            Argument {
                name: FILE_PATH_LABEL.to_string(),
                value: Value::Text("some_file.cc".to_string()),
            },
            Argument { name: LINE_NUMBER_LABEL.to_string(), value: Value::UnsignedInt(420) },
            Argument { name: "arg1".to_string(), value: Value::SignedInt(-23) },
            Argument { name: PID_LABEL.to_string(), value: Value::UnsignedInt(43) },
            Argument { name: TID_LABEL.to_string(), value: Value::UnsignedInt(912) },
            Argument { name: DROPPED_LABEL.to_string(), value: Value::UnsignedInt(2) },
            Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag".to_string()) },
            Argument { name: MESSAGE_LABEL.to_string(), value: Value::Text("msg".to_string()) },
        ],
    };

    let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
    let mut encoder = Encoder::new(&mut buffer);
    encoder.write_record(&record).unwrap();
    let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];
    let parsed = crate::from_structured(get_test_identity(), encoded).unwrap();
    assert_eq!(
        parsed,
        LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(72).into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.moniker.clone(),
            severity: Severity::Error,
            size_bytes: 224,
        })
        .set_dropped(2)
        .set_file("some_file.cc".to_string())
        .set_line(420)
        .set_pid(43u64)
        .set_tid(912u64)
        .add_tag("tag")
        .set_message("msg".to_string())
        .add_key(LogsProperty::Int(LogsField::Other("arg1".to_string()), -23i64))
        .build()
    );
    let severity: i32 = LegacySeverity::Error.into();
    let message: LogMessage = parsed.into();
    assert_eq!(
        message,
        LogMessage {
            severity,
            time: 72,
            dropped_logs: 2,
            pid: 43,
            tid: 912,
            msg: "[some_file.cc(420)] msg arg1=-23".into(),
            tags: vec!["tag".into()]
        }
    );

    // multiple tags
    let record = Record {
        timestamp: 72,
        severity: StreamSeverity::Error,
        arguments: vec![
            Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag1".to_string()) },
            Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag2".to_string()) },
            Argument { name: TAG_LABEL.to_string(), value: Value::Text("tag3".to_string()) },
        ],
    };
    let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
    let mut encoder = Encoder::new(&mut buffer);
    encoder.write_record(&record).unwrap();
    let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];
    let parsed = crate::from_structured(get_test_identity(), encoded).unwrap();
    assert_eq!(
        parsed,
        LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(72).into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.moniker.clone(),
            severity: Severity::Error,
            size_bytes: encoded.len(),
        })
        .add_tag("tag1")
        .add_tag("tag2")
        .add_tag("tag3")
        .build()
    );

    // empty record
    let record = Record { timestamp: 72, severity: StreamSeverity::Error, arguments: vec![] };
    let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
    let mut encoder = Encoder::new(&mut buffer);
    encoder.write_record(&record).unwrap();
    let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];
    let parsed = crate::from_structured(get_test_identity(), encoded).unwrap();
    assert_eq!(
        parsed,
        LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(72).into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.moniker.clone(),
            severity: Severity::Error,
            size_bytes: encoded.len(),
        })
        .build()
    );

    // parse error
    assert_matches!(
        crate::from_structured(get_test_identity(), &vec![]).unwrap_err(),
        MessageError::ParseError { .. }
    );
}

#[test]
fn basic_structured_info() {
    let expected_timestamp = 72;
    let record = Record {
        timestamp: expected_timestamp,
        severity: StreamSeverity::Error,
        arguments: vec![],
    };
    let mut buffer = Cursor::new(vec![0u8; MAX_DATAGRAM_LEN]);
    let mut encoder = Encoder::new(&mut buffer);
    encoder.write_record(&record).unwrap();
    let encoded = &buffer.get_ref().as_slice()[..buffer.position() as usize];

    let (timestamp, severity) = parse_basic_structured_info(encoded).unwrap();
    assert_eq!(timestamp, expected_timestamp);
    assert_eq!(severity, Severity::Error);
}
