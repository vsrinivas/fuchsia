// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use akm;
use bytes::{BufMut, Bytes, BytesMut};
use cipher;
use failure::Error;
use pmkid;
use suite_selector;

use nom::{le_u16, le_u8, IResult};

macro_rules! if_remaining (
  ($i:expr, $f:expr) => ( cond!($i, $i.len() !=0, call!($f)); );
);

macro_rules! check_remaining (
  ($required:expr, $remaining:expr) => {
    if $remaining < $required {
        return Err(ErrorBufferTooSmall($required, $remaining).into());
    }
  };
);

// IEEE 802.11-2016, 9.4.2.25.1
pub const ID: u8 = 48;
pub const VERSION: u16 = 1;

// IEEE 802.11-2016, 9.4.2.25.1
#[derive(Default, Debug, PartialOrd, PartialEq)]
pub struct Rsne {
    pub version: u16,
    pub group_data_cipher_suite: Option<cipher::Cipher>,
    pub pairwise_cipher_suites: Vec<cipher::Cipher>,
    pub akm_suites: Vec<akm::Akm>,
    pub rsn_capabilities: Option<u16>,
    pub pmkids: Vec<pmkid::Pmkid>,
    pub group_mgmt_cipher_suite: Option<cipher::Cipher>,
}

impl Rsne {
    pub fn new() -> Self {
        let mut rsne = Rsne::default();
        rsne.version = VERSION;
        rsne
    }

    pub fn len(&self) -> usize {
        let mut length: usize = 4;
        match self.group_data_cipher_suite.as_ref() {
            None => return length,
            Some(_) => length += 4,
        };

        if self.pairwise_cipher_suites.is_empty() {
            return length;
        }
        length += 2 + 4 * self.pairwise_cipher_suites.len();

        if self.akm_suites.is_empty() {
            return length;
        }
        length += 2 + 4 * self.akm_suites.len();

        match self.rsn_capabilities.as_ref() {
            None => return length,
            Some(_) => length += 2,
        };

        if self.pmkids.is_empty() {
            return length;
        }
        length += 2 + 16 * self.pmkids.len();

        length += match self.group_mgmt_cipher_suite.as_ref() {
            None => 0,
            Some(_) => 4,
        };
        length
    }

    pub fn as_bytes(&self, buf: &mut BufMut) -> Result<(), Error> {
        check_remaining!(4, buf.remaining_mut());
        buf.put_u8(ID);
        buf.put_u8((self.len() - 2) as u8);
        buf.put_u16_le(self.version);

        match self.group_data_cipher_suite.as_ref() {
            None => return Ok(()),
            Some(cipher) => {
                check_remaining!(4, buf.remaining_mut());
                buf.put_slice(&cipher.oui[..]);
                buf.put_u8(cipher.suite_type);
            }
        };

        if self.pairwise_cipher_suites.is_empty() {
            return Ok(());
        }
        check_remaining!(
            2 + 4 * self.pairwise_cipher_suites.len(),
            buf.remaining_mut()
        );
        buf.put_u16_le(self.pairwise_cipher_suites.len() as u16);
        for cipher in &self.pairwise_cipher_suites {
            buf.put_slice(&cipher.oui[..]);
            buf.put_u8(cipher.suite_type);
        }

        if self.akm_suites.is_empty() {
            return Ok(());
        }
        check_remaining!(2 + 4 * self.akm_suites.len(), buf.remaining_mut());
        buf.put_u16_le(self.akm_suites.len() as u16);
        for akm in &self.akm_suites {
            buf.put_slice(&akm.oui[..]);
            buf.put_u8(akm.suite_type);
        }

        match self.rsn_capabilities.as_ref() {
            None => return Ok(()),
            Some(caps) => {
                check_remaining!(2, buf.remaining_mut());
                buf.put_u16_le(*caps)
            }
        };

        if self.pmkids.is_empty() {
            return Ok(());
        }
        check_remaining!(2 + 16 * self.pmkids.len(), buf.remaining_mut());
        buf.put_u16_le(self.pmkids.len() as u16);
        for pmkid in &self.pmkids {
            buf.put_slice(&pmkid[..]);
        }

        if let Some(cipher) = self.group_mgmt_cipher_suite.as_ref() {
            check_remaining!(4, buf.remaining_mut());
            buf.put_slice(&cipher.oui[..]);
            buf.put_u8(cipher.suite_type);
        }

        Ok(())
    }
}

fn read_suite_selector<'a, T>(input: &'a [u8]) -> IResult<&'a [u8], T>
where
    T: suite_selector::Factory<Suite = T>,
{
    let (i1, bytes) = try_parse!(input, take!(4));
    let oui = Bytes::from(&bytes[0..3]);
    let (i2, ctor_result) = try_parse!(i1, expr_res!(T::new(oui, bytes[3])));
    return IResult::Done(i2, ctor_result);
}

fn read_pmkid<'a>(input: &'a [u8]) -> IResult<&'a [u8], pmkid::Pmkid> {
    let (i1, bytes) = try_parse!(input, take!(16));
    let pmkid_data = Bytes::from(bytes);
    let (i2, result) = try_parse!(i1, expr_res!(pmkid::new(pmkid_data)));
    return IResult::Done(i2, result);
}

named!(akm<&[u8], akm::Akm>, call!(read_suite_selector::<akm::Akm>));
named!(cipher<&[u8], cipher::Cipher>, call!(read_suite_selector::<cipher::Cipher>));

named!(pub from_bytes<&[u8], Rsne>,
       do_parse!(
           _element_id: le_u8 >>
           _length: le_u8 >>
           version: le_u16 >>
           group_cipher: if_remaining!(cipher) >>
           pairwise_count: if_remaining!(le_u16) >>
           pairwise_list: count!(cipher, pairwise_count.unwrap_or(0) as usize)  >>
           akm_count: if_remaining!(le_u16) >>
           akm_list: count!(akm, akm_count.unwrap_or(0) as usize)  >>
           rsn_capabilities: if_remaining!(le_u16) >>
           pmkid_count: if_remaining!(le_u16) >>
           pmkid_list: count!(read_pmkid, pmkid_count.unwrap_or(0) as usize)  >>
           group_mgmt_cipher_suite: if_remaining!(cipher) >>
           eof!() >>
           (Rsne{
                version: version,
                group_data_cipher_suite: group_cipher,
                pairwise_cipher_suites: pairwise_list,
                akm_suites: akm_list,
                rsn_capabilities: rsn_capabilities,
                pmkids: pmkid_list,
                group_mgmt_cipher_suite: group_mgmt_cipher_suite
           })
    )
);

#[derive(Debug, Fail)]
#[fail(display = "buffer too small; required: {}, available: {}", _0, _1)]
struct ErrorBufferTooSmall(usize, usize);

#[cfg(test)]
mod tests {
    use super::*;
    use test::Bencher;

    #[bench]
    fn bench_parse_with_nom(b: &mut Bencher) {
        let frame: Vec<u8> = vec![
            0x30, 0x2A, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x00, 0x0f,
            0xac, 0x04,
        ];
        b.iter(|| from_bytes(&frame));
    }

    #[test]
    fn test_as_bytes() {
        let frame: Vec<u8> = vec![
            0x30, 0x2A, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x00, 0x0f,
            0xac, 0x04,
        ];
        let mut buf = BytesMut::with_capacity(128);
        let result = from_bytes(&frame);
        assert!(result.is_done());
        let rsne = result.unwrap().1;
        let result = rsne.as_bytes(&mut buf);
        assert!(result.is_ok());
        let rsne_len = buf.len();
        let left_over = buf.split_off(rsne_len);
        assert_eq!(&buf[..], &frame[..]);
        assert!(left_over.iter().all(|b| *b == 0));
    }

    #[test]
    fn test_short_buffer() {
        let frame: Vec<u8> = vec![
            0x30, 0x2A, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x07, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f,
            0xac, 0x04, 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04,
        ];
        let mut buf = BytesMut::with_capacity(32);
        let result = from_bytes(&frame);
        assert!(result.is_done());
        let rsne = result.unwrap().1;
        let result = rsne.as_bytes(&mut buf);
        assert!(result.is_err());
    }
}
