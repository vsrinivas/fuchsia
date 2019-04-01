// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[cfg(test)]
mod test {
    use fuchsia_wayland_core::{Arg, Enum, Fixed, FromArgs, IntoMessage};
    use fuchsia_zircon::{self as zx, HandleBased};
    use test_protocol::{test_interface, TestInterfaceEvent, TestInterfaceRequest};
    use zerocopy::AsBytes;

    static SENDER_ID: u32 = 3;

    // Force a compile error if value does not have the AsBytes trait.
    fn is_as_bytes<T: AsBytes>(_: &T) {}

    macro_rules! message_bytes(
        ($sender:expr, $opcode:expr, $val:expr) => {
            unsafe {
                is_as_bytes(&$val);
                use std::mem;
                let value: u32 = mem::transmute($val);
                &[
                    $sender as u8,
                    ($sender >> 8) as u8,
                    ($sender >> 16) as u8,
                    ($sender >> 24) as u8,
                    $opcode as u8,
                    ($opcode >> 8) as u8, // opcode
                    0x0c, 0x00, // length
                    value as u8,
                    (value >> 8) as u8,
                    (value >> 16) as u8,
                    (value >> 24) as u8,
                ]
            }
        }
    );

    macro_rules! assert_match(
        ($e:expr, $p:pat => $a:expr) => (
            match $e {
                $p => $a,
                _ => panic!("Unexpected variant {:?}", $e),
            }
        )
    );

    static UINT_VALUE: u32 = 0x12345678;

    #[test]
    fn test_serialize_uint() {
        let (bytes, handles) =
            TestInterfaceEvent::Uint { arg: UINT_VALUE }.into_message(SENDER_ID).unwrap().take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 0 /* opcode */, UINT_VALUE));
    }

    #[test]
    fn test_deserialize_uint() {
        let request =
            TestInterfaceRequest::from_args(0 /* opcode */, vec![Arg::Uint(UINT_VALUE)]).unwrap();

        assert_match!(request, TestInterfaceRequest::Uint{arg} => assert_eq!(arg, UINT_VALUE));
    }

    static INT_VALUE: i32 = -123;

    #[test]
    fn test_serialize_int() {
        let (bytes, handles) =
            TestInterfaceEvent::Int { arg: INT_VALUE }.into_message(SENDER_ID).unwrap().take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 1 /* opcode */, INT_VALUE));
    }

    #[test]
    fn test_deserialize_int() {
        let request =
            TestInterfaceRequest::from_args(1 /* opcode */, vec![Arg::Int(INT_VALUE)]).unwrap();

        assert_match!(request, TestInterfaceRequest::Int{arg} => assert_eq!(arg, INT_VALUE));
    }

    static FIXED_VALUE: i32 = 23332125;

    #[test]
    fn test_serialize_fixed() {
        let (bytes, handles) = TestInterfaceEvent::Fixed { arg: Fixed::from_bits(FIXED_VALUE) }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 2 /* opcode */, FIXED_VALUE));
    }

    #[test]
    fn test_deserialize_fixed() {
        let request = TestInterfaceRequest::from_args(
            2, /* opcode */
            vec![Arg::Fixed(Fixed::from_bits(FIXED_VALUE))],
        )
        .unwrap();

        assert_match!(request, TestInterfaceRequest::Fixed{arg} => assert_eq!(arg, Fixed::from_bits(FIXED_VALUE)));
    }

    static STRING_VALUE: &'static str = "This is a wayland string.";
    static STRING_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x03, 0x00, // opcode: 3 (string)
        0x28, 0x00, // length: 40
        0x1a, 0x00, 0x00, 0x00, // string (len) = 26
        0x54, 0x68, 0x69, 0x73, // 'This'
        0x20, 0x69, 0x73, 0x20, // ' is '
        0x61, 0x20, 0x77, 0x61, // 'a way'
        0x79, 0x6c, 0x61, 0x6e, // 'lan'
        0x64, 0x20, 0x73, 0x74, // 'd st'
        0x72, 0x69, 0x6e, 0x67, // 'ring'
        0x2e, 0x00, 0x00, 0x00, // '.' NULL PAD PAD
    ];

    #[test]
    fn test_serialize_string() {
        let (bytes, handles) = TestInterfaceEvent::String { arg: STRING_VALUE.to_string() }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, STRING_MESSAGE_BYTES);
    }

    #[test]
    fn test_deserialize_string() {
        let request = TestInterfaceRequest::from_args(
            3, /* opcode */
            vec![Arg::String(STRING_VALUE.to_string())],
        )
        .unwrap();

        assert_match!(request, TestInterfaceRequest::String{arg} => assert_eq!(arg, STRING_VALUE));
    }

    static OBJECT_VALUE: u32 = 2;

    #[test]
    fn test_serialize_object() {
        let (bytes, handles) = TestInterfaceEvent::Object { arg: OBJECT_VALUE }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 4 /* opcode */, OBJECT_VALUE));
    }

    #[test]
    fn test_deserialize_object() {
        let request =
            TestInterfaceRequest::from_args(4 /* opcode */, vec![Arg::Object(OBJECT_VALUE)])
                .unwrap();

        assert_match!(request, TestInterfaceRequest::Object{arg} => assert_eq!(arg, OBJECT_VALUE));
    }

    static NEW_ID_VALUE: u32 = 112233;

    #[test]
    fn test_serialize_new_id() {
        let (bytes, handles) =
            TestInterfaceEvent::NewId { arg: NEW_ID_VALUE }.into_message(SENDER_ID).unwrap().take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 5 /* opcode */, NEW_ID_VALUE));
    }

    #[test]
    fn test_deserialize_new_id() {
        let request =
            TestInterfaceRequest::from_args(5 /* opcode */, vec![Arg::NewId(NEW_ID_VALUE)])
                .unwrap();

        assert_match!(request, TestInterfaceRequest::NewId{arg} => assert_eq!(arg.id(), NEW_ID_VALUE));
    }

    static UNTYPED_NEW_ID_INTERFACE_NAME: &'static str = "test_interface";
    static UNTYPED_NEW_ID_INTERFACE_VERSION: u32 = 4;
    static UNTYPED_NEW_ID_VALUE: u32 = 8;
    static UNTYPED_NEW_ID_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x09, 0x00, // opcode: 9 (untyped_new_id)
        0x24, 0x00, // length: 36
        0x0f, 0x00, 0x00, 0x00, // string (len) = 15
        0x74, 0x65, 0x73, 0x74, // 'test'
        0x5f, 0x69, 0x6e, 0x74, // '_int'
        0x65, 0x72, 0x66, 0x61, // 'erfa'
        0x63, 0x65, 0x00, 0x00, // 'ce' NULL PAD
        0x04, 0x00, 0x00, 0x00, // version: (4)
        0x08, 0x00, 0x00, 0x00, // new_id(8)
    ];

    #[test]
    fn test_serialize_untyped_new_id() {
        let message = TestInterfaceEvent::UntypedNewId {
            arg: UNTYPED_NEW_ID_VALUE,
            arg_interface_name: UNTYPED_NEW_ID_INTERFACE_NAME.to_string(),
            arg_interface_version: UNTYPED_NEW_ID_INTERFACE_VERSION,
        };
        let (bytes, handles) = message.into_message(SENDER_ID).unwrap().take();
        assert!(handles.is_empty());
        assert_eq!(UNTYPED_NEW_ID_MESSAGE_BYTES, bytes.as_slice());
    }

    #[test]
    fn test_deserialize_untyped_new_id() {
        let request = TestInterfaceRequest::from_args(
            9, /* opcode */
            vec![
                Arg::String(UNTYPED_NEW_ID_INTERFACE_NAME.to_string()),
                Arg::Uint(UNTYPED_NEW_ID_INTERFACE_VERSION),
                Arg::NewId(UNTYPED_NEW_ID_VALUE),
            ],
        )
        .unwrap();

        assert_match!(request, TestInterfaceRequest::UntypedNewId{
            arg,
            arg_interface_name,
            arg_interface_version,
        } => {
            assert_eq!(arg, UNTYPED_NEW_ID_VALUE);
            assert_eq!(&arg_interface_name, UNTYPED_NEW_ID_INTERFACE_NAME);
            assert_eq!(arg_interface_version, UNTYPED_NEW_ID_INTERFACE_VERSION);
        });
    }

    static ARRAY_VALUE: &'static [u8] = &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    static ARRAY_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x06, 0x00, // opcode: 6 (array)
        0x18, 0x00, // length: 24
        0x0a, 0x00, 0x00, 0x00, // array (len) = 10
        0x01, 0x02, 0x03, 0x04, // array[0..3]
        0x05, 0x06, 0x07, 0x08, // array[4..7]
        0x09, 0x0a, 0x00, 0x00, // array[8..9] PAD PAD
    ];

    #[test]
    fn test_serialize_array() {
        let (bytes, handles) = TestInterfaceEvent::Array { arg: ARRAY_VALUE.to_vec().into() }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert!(handles.is_empty());
        assert_eq!(bytes, ARRAY_MESSAGE_BYTES);
    }

    #[test]
    fn test_deserialize_array() {
        let request = TestInterfaceRequest::from_args(
            6, /* opcode */
            vec![Arg::Array(ARRAY_VALUE.to_vec().into())],
        )
        .unwrap();

        assert_match!(request, TestInterfaceRequest::Array{arg} => assert_eq!(arg.into_vec(), ARRAY_VALUE));
    }

    static HANDLE_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x07, 0x00, // opcode: 7 (handle)
        0x08, 0x00, // length: 8
    ];

    #[test]
    fn test_serialize_handle() {
        let (s1, _s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let (bytes, handles) = TestInterfaceEvent::Handle { arg: s1.into_handle() }
            .into_message(SENDER_ID)
            .unwrap()
            .take();
        assert_eq!(bytes, HANDLE_MESSAGE_BYTES);
        assert_eq!(handles.len(), 1);
        assert!(!handles[0].is_invalid());
    }

    #[test]
    fn test_deserialize_handle() {
        let (s1, _s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let request = TestInterfaceRequest::from_args(
            7, /* opcode */
            vec![Arg::Handle(s1.into_handle())],
        )
        .unwrap();

        assert_match!(request, TestInterfaceRequest::Handle{arg} => assert!(!arg.is_invalid()));
    }

    static COMPLEX_MESSAGE_BYTES: &'static [u8] = &[
        0x03, 0x00, 0x00, 0x00, // sender: 3
        0x08, 0x00, // opcode: 8 (complex)
        0x44, 0x00, // length: 68
        0x78, 0x56, 0x34, 0x12, // uint 0x12345678
        0x85, 0xff, 0xff, 0xff, // int: -123
        0x02, 0x00, 0x00, 0x00, // object: 2
        0x1a, 0x00, 0x00, 0x00, // string (len) = 26
        0x54, 0x68, 0x69, 0x73, // 'This'
        0x20, 0x69, 0x73, 0x20, // ' is '
        0x61, 0x20, 0x77, 0x61, // 'a way'
        0x79, 0x6c, 0x61, 0x6e, // 'lan'
        0x64, 0x20, 0x73, 0x74, // 'd st'
        0x72, 0x69, 0x6e, 0x67, // 'ring'
        0x2e, 0x00, 0x00, 0x00, // '.' NULL PAD PAD
        0x0a, 0x00, 0x00, 0x00, // array (len) = 10
        0x01, 0x02, 0x03, 0x04, // array[0..3]
        0x05, 0x06, 0x07, 0x08, // array[4..7]
        0x09, 0x0a, 0x00, 0x00, // array[8..9] PAD PAD
    ];

    #[test]
    fn test_deserialize_complex() {
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let request = test_protocol::TestInterfaceRequest::from_args(
            8, /* opcode */
            vec![
                Arg::Uint(UINT_VALUE),
                Arg::Int(INT_VALUE),
                Arg::Handle(s1.into_handle()),
                Arg::Object(OBJECT_VALUE),
                Arg::Handle(s2.into_handle()),
                Arg::String(STRING_VALUE.to_string()),
                Arg::Array(ARRAY_VALUE.to_vec().into()),
            ],
        )
        .unwrap();

        match request {
            test_protocol::TestInterfaceRequest::Complex {
                uint_arg,
                int_arg,
                handle_arg1,
                object_arg,
                handle_arg2,
                string_arg,
                array_arg,
            } => {
                assert_eq!(UINT_VALUE, uint_arg);
                assert_eq!(INT_VALUE, int_arg);
                assert!(!handle_arg1.is_invalid());
                assert_eq!(OBJECT_VALUE, object_arg);
                assert!(!handle_arg2.is_invalid());
                assert_eq!(STRING_VALUE, &string_arg);
                assert_eq!(ARRAY_VALUE, array_arg.as_slice());
            }
            _ => panic!("Message deserialized to incorrect variant {:?}", request),
        }
    }

    #[test]
    fn test_serialize_complex() {
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let event = test_protocol::TestInterfaceEvent::Complex {
            uint_arg: UINT_VALUE,
            int_arg: INT_VALUE,
            handle_arg1: s1.into_handle(),
            object_arg: OBJECT_VALUE,
            handle_arg2: s2.into_handle(),
            string_arg: STRING_VALUE.to_string(),
            array_arg: ARRAY_VALUE.to_vec().into(),
        };
        let message = event.into_message(SENDER_ID).unwrap();
        let (message_bytes, message_handles) = message.take();

        assert_eq!(COMPLEX_MESSAGE_BYTES, message_bytes.as_slice());
        assert_eq!(2, message_handles.len());
        assert!(!message_handles[0].is_invalid());
        assert!(!message_handles[1].is_invalid());
    }

    #[test]
    fn test_deserialize_request_invalid_opcode() {
        let result = test_protocol::TestInterfaceRequest::from_args(111, vec![]);
        assert!(result.is_err());
    }

    #[test]
    fn test_deserialize_request_message_too_short() {
        let result = test_protocol::TestInterfaceRequest::from_args(0, vec![]);
        assert!(result.is_err());
    }

    #[test]
    fn test_enum() {
        assert_eq!(0, test_interface::TestEnum::Entry1.bits());
        assert_eq!(1, test_interface::TestEnum::Entry2.bits());
        assert_eq!(2, test_interface::TestEnum::_0StartsWithNumber.bits());

        assert_eq!(Some(test_interface::TestEnum::Entry1), test_interface::TestEnum::from_bits(0));
        assert_eq!(Some(test_interface::TestEnum::Entry2), test_interface::TestEnum::from_bits(1));
        assert_eq!(
            Some(test_interface::TestEnum::_0StartsWithNumber),
            test_interface::TestEnum::from_bits(2)
        );
        assert_eq!(None, test_interface::TestEnum::from_bits(3));
    }

    #[test]
    fn test_bitfield() {
        assert_eq!(1, test_interface::TestBitfield::Entry1.bits());
        assert_eq!(2, test_interface::TestBitfield::Entry2.bits());
        assert_eq!(4, test_interface::TestBitfield::_0StartsWithNumber.bits());

        assert_eq!(
            Some(test_interface::TestBitfield::Entry1),
            test_interface::TestBitfield::from_bits(1)
        );
        assert_eq!(
            Some(test_interface::TestBitfield::Entry2),
            test_interface::TestBitfield::from_bits(2)
        );
        assert_eq!(
            Some(test_interface::TestBitfield::_0StartsWithNumber),
            test_interface::TestBitfield::from_bits(4)
        );
    }

    #[test]
    fn test_serialize_uint_enum_arg() {
        let event = TestInterfaceEvent::TestUintEnum { arg: test_interface::TestEnum::Entry2 };
        let (bytes, handles) = event.into_message(SENDER_ID).unwrap().take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 11 /* opcode */, 1));
    }

    #[test]
    fn test_deserialize_uint_enum_arg() {
        let request = TestInterfaceRequest::from_args(11 /* opcode */, vec![Arg::Uint(1)]).unwrap();
        assert_match!(request, TestInterfaceRequest::TestUintEnum{arg} => assert_eq!(arg, Enum::Recognized(test_interface::TestEnum::Entry2)));
    }

    #[test]
    fn test_serialize_int_enum_arg() {
        let event = TestInterfaceEvent::TestIntEnum { arg: test_interface::TestEnum::Entry2 };
        let (bytes, handles) = event.into_message(SENDER_ID).unwrap().take();
        assert!(handles.is_empty());
        assert_eq!(bytes, message_bytes!(SENDER_ID, 10 /* opcode */, 1));
    }

    #[test]
    fn test_deserialize_int_enum_arg() {
        let request = TestInterfaceRequest::from_args(10 /* opcode */, vec![Arg::Uint(1)]).unwrap();
        assert_match!(request, TestInterfaceRequest::TestIntEnum{arg} => assert_eq!(arg, Enum::Recognized(test_interface::TestEnum::Entry2)));
    }
}
