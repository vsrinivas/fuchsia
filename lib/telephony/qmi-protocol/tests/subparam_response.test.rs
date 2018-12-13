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
        let mut part_a = Default::default();
        let mut part_b = Default::default();
        while total_len > 0 {
            let msg_id = buf.get_u8();
            total_len -= 1;
            let tlv_len = buf.get_u16_le();
            total_len -= 2;
            match msg_id {
                1 => {
                    part_a = buf.get_u8();
                    part_b = buf.get_u8();
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
            part_a,
            part_b,
        })
    }
}
