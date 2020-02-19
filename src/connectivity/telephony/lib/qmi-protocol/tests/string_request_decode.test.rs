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
        let mut blah = Default::default();
        while total_len > 0 {
            let msg_id = buf.get_u8();
            total_len -= 1;
            let tlv_len = buf.get_u16_le();
            total_len -= 2;
            match msg_id {
                1 => {
                    let mut dst = vec![0; tlv_len as usize];
                    buf.copy_to_slice(&mut dst[..]);
                    total_len -= tlv_len;
                    let str = String::from_utf8(dst);
                    blah = str.unwrap();
                }
                0 => { eprintln!("Found a type of 0, modem gave a bad TLV, trying to recover"); break; }
                e_code => {
                    eprintln!("Unknown id for this message type: {}, removing {} of len", e_code, tlv_len);
                    total_len -= tlv_len;
                }
            }
        }
        Ok(TestResp {
            blah,
        })
    }
}
