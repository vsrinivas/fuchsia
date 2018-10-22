impl Decodable for TestResp {
    fn from_bytes<T: Buf>(mut buf: T) -> Result<TestResp, QmiError> {
        assert_eq!(288, buf.get_u16_le());
        let mut total_len = buf.get_u16_le();
        let _ = buf.get_u8();
        total_len -= 1;
        let res_len = buf.get_u16_le();
        total_len -= (res_len + 2);
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
                    let dst = buf.by_ref().take(tlv_len as usize).collect();
                    total_len -= tlv_len;
                    blah = String::from_utf8(dst).unwrap();
                }
                _ => panic!("unknown id for this message type")
            }
        }
        Ok(TestResp {
            blah,
        })
    }
}
