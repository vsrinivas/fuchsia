impl Encodable for TestReq {
    type DecodeResult = TestResp;
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
        Test_len += 1; // tlv type length;
        Test_len += 2; // tlv length length;
        let _blah = &self.blah;
        Test_len += 2;
        buf.put_u16_le(Test_len);
        // tlvs
        let part_a = &self.part_a;
        let part_b = &self.part_b;
        buf.put_u8(1);
        buf.put_u16_le(2);
        buf.put_u8(*part_a);
        buf.put_u8(*part_b);
        return (buf.freeze(), Test_len + 2 /*msg id len*/ + 2 /*msg len len*/);
    }
}
