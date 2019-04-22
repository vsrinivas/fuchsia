pub mod TEST {
    use crate::{Decodable, Encodable};
    use crate::QmiError;
    use bytes::{Bytes, Buf, BufMut, BytesMut};
    const MSG_MAX: usize = 512;
    pub struct TestReq {
    }
    impl TestReq {
        pub fn new() -> Self {
            TestReq {
            }
        }
    }
    #[derive(Debug)]
    pub struct TestResp {
        pub uint_blah: u16,
        pub int_blah: i16,
    }
    impl Encodable for TestReq {
        fn transaction_id_len(&self) -> u8 {
            2
        }
        fn svc_id(&self) -> u8 {
            66
        }
        fn to_bytes(&self) -> (Bytes, u16) {
            let mut buf = BytesMut::with_capacity(MSG_MAX);
            // svc id
            buf.put_u16_le(288);
            // svc length calculation
            let mut Test_len = 0u16;
            buf.put_u16_le(Test_len);
            // tlvs
            return (buf.freeze(), Test_len + 2 /*msg id len*/ + 2 /*msg len len*/);
        }
    }
    impl Decodable for TestResp {
        fn from_bytes<T: Buf>(mut buf: T) -> Result<TestResp, QmiError> {
            assert_eq!(288, buf.get_u16_le());
            let mut total_len = buf.get_u16_le();
            let _ = buf.get_u8();
            total_len -= 1;
            let res_len = buf.get_u16_le();
            total_len -= res_len + 2;
            if 0x00 != buf.get_u16_le() {
                let error_code = buf.get_u16_le();
                return Err(QmiError::from_code(error_code))
            } else {
                assert_eq!(0x00, buf.get_u16_le()); // this must be zero if no error from above check
            }
            let mut uint_blah = Default::default();
            let mut int_blah = Default::default();
            while total_len > 0 {
                let msg_id = buf.get_u8();
                total_len -= 1;
                let tlv_len = buf.get_u16_le();
                total_len -= 2;
                match msg_id {
                    1 => {
                        uint_blah = buf.get_u16_le();
                        total_len -= 2;
                    }
                    3 => {
                        int_blah = buf.get_i16_le();
                        total_len -= 2;
                    }
                    0 => { eprintln!("Found a type of 0, modem gave a bad TLV, trying to recover"); break; }
                    e_code => {
                        eprintln!("Unknown id for this message type: {}, removing {} of len", e_code, tlv_len);
                        total_len -= tlv_len;
                    }
                }
            }
            Ok(TestResp {
                uint_blah,
                int_blah,
            })
        }
    }
}
