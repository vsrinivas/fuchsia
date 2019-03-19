use std::cmp::{ self, Ordering };
use bytes::Buf;
use iovec::IoVec;

pub struct VecBuf<'a, 'b: 'a> {
    pos: usize,
    cur: usize,
    inner: &'a [&'b [u8]]
}

impl<'a, 'b> VecBuf<'a, 'b> {
    pub fn new(vbytes: &'a [&'b [u8]]) -> Self {
        VecBuf { pos: 0, cur: 0, inner: vbytes }
    }
}

impl<'a, 'b> Buf for VecBuf<'a, 'b> {
    fn remaining(&self) -> usize {
        let sum = self.inner
            .iter()
            .skip(self.pos)
            .map(|bytes| bytes.len())
            .sum::<usize>();
        sum - self.cur
    }

    fn bytes(&self) -> &[u8] {
        &self.inner[self.pos][self.cur..]
    }

    fn advance(&mut self, cnt: usize) {
        let current = self.inner[self.pos].len();
        match (self.cur + cnt).cmp(&current) {
            Ordering::Equal => if self.pos + 1 < self.inner.len() {
                self.pos += 1;
                self.cur = 0;
            } else {
                self.cur += cnt;
            },
            Ordering::Greater => {
                if self.pos + 1 < self.inner.len() {
                    self.pos += 1;
                }
                let remaining = self.cur + cnt - current;
                self.advance(remaining);
            },
            Ordering::Less => self.cur += cnt,
        }
    }

    #[cfg_attr(feature = "cargo-clippy", allow(needless_range_loop))]
    fn bytes_vec<'c>(&'c self, dst: &mut [&'c IoVec]) -> usize {
        let len = cmp::min(self.inner.len() - self.pos, dst.len());

        if len > 0 {
            dst[0] = self.bytes().into();
        }

        for i in 1..len {
            dst[i] = self.inner[self.pos + i].into();
        }

        len
    }
}

#[cfg(test)]
mod test_vecbuf {
    use super::*;

    #[test]
    fn test_fresh_cursor_vec() {
        let mut buf = VecBuf::new(&[b"he", b"llo"]);

        assert_eq!(buf.remaining(), 5);
        assert_eq!(buf.bytes(), b"he");

        buf.advance(2);

        assert_eq!(buf.remaining(), 3);
        assert_eq!(buf.bytes(), b"llo");

        buf.advance(3);

        assert_eq!(buf.remaining(), 0);
        assert_eq!(buf.bytes(), b"");
    }

    #[test]
    fn test_get_u8() {
        let mut buf = VecBuf::new(&[b"\x21z", b"omg"]);
        assert_eq!(0x21, buf.get_u8());
    }

    #[test]
    fn test_get_u16() {
        let mut buf = VecBuf::new(&[b"\x21\x54z", b"omg"]);
        assert_eq!(0x2154, buf.get_u16_be());
        let mut buf = VecBuf::new(&[b"\x21\x54z", b"omg"]);
        assert_eq!(0x5421, buf.get_u16_le());
    }

    #[test]
    #[should_panic]
    fn test_get_u16_buffer_underflow() {
        let mut buf = VecBuf::new(&[b"\x21"]);
        buf.get_u16_be();
    }

    #[test]
    fn test_bufs_vec() {
        let buf = VecBuf::new(&[b"he", b"llo"]);

        let b1: &[u8] = &mut [0];
        let b2: &[u8] = &mut [0];

        let mut dst: [&IoVec; 2] =
            [b1.into(), b2.into()];

        assert_eq!(2, buf.bytes_vec(&mut dst[..]));
    }
}
