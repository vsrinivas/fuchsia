// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::appendable::{Appendable, BufferTooSmall},
    zerocopy::ByteSliceMut,
};

pub struct BufferWriter<B> {
    buf: B,
    bytes_written: usize,
}

impl<B: ByteSliceMut> BufferWriter<B> {
    pub fn new(buf: B) -> Self {
        Self { buf, bytes_written: 0 }
    }

    pub fn bytes_remaining(&self) -> usize {
        self.buf.len() - self.bytes_written
    }

    pub fn bytes_written(&self) -> usize {
        self.bytes_written
    }

    pub fn into_written(self) -> B {
        let (written, _remaining) = self.buf.split_at(self.bytes_written);
        written
    }

    fn next_mut_slice(&mut self, len: usize) -> Result<&mut [u8], BufferTooSmall> {
        if self.bytes_written + len > self.buf.len() {
            return Err(BufferTooSmall);
        }
        let ret = &mut self.buf[self.bytes_written..self.bytes_written + len];
        self.bytes_written += len;
        Ok(ret)
    }
}

impl<B: ByteSliceMut> Appendable for BufferWriter<B> {
    fn append_bytes(&mut self, bytes: &[u8]) -> Result<(), BufferTooSmall> {
        self.next_mut_slice(bytes.len())?.copy_from_slice(bytes);
        Ok(())
    }

    fn append_bytes_zeroed(&mut self, len: usize) -> Result<&mut [u8], BufferTooSmall> {
        let ret = self.next_mut_slice(len)?;
        for x in &mut ret[..] {
            *x = 0;
        }
        Ok(ret)
    }

    fn bytes_written(&self) -> usize {
        self.bytes_written
    }

    fn can_append(&self, bytes: usize) -> bool {
        self.bytes_remaining() >= bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn buffer_too_short() {
        assert!(BufferWriter::new(&mut [0; 0][..]).append_byte(1).is_err());
        assert!(BufferWriter::new(&mut [0; 5][..]).append_bytes(&[0; 6]).is_err());
        assert!(BufferWriter::new(&mut [0; 5][..]).append_value_zeroed::<[u8; 6]>().is_err());
    }

    #[test]
    fn append_value_zeroed() {
        let mut buf = [1u8; 5];
        let mut w = BufferWriter::new(&mut buf[..]);
        let mut data = w.append_value_zeroed::<[u8; 3]>().expect("failed writing buffer");
        data[0] = 42;
        // Don't write `data[1]`: BufferWriter should zero this byte.
        data[2] = 43;

        assert_eq!(3, w.bytes_written());
        assert_eq!(2, w.bytes_remaining());
        assert_eq!([42, 0, 43, 1, 1], buf);
    }

    #[test]
    fn append_bytes() {
        let mut buf = [1u8; 5];
        let mut w = BufferWriter::new(&mut buf[..]);
        w.append_byte(42).expect("failed writing buffer");
        w.append_byte(43).expect("failed writing buffer");
        w.append_bytes(&[2, 3]).expect("failed writing buffer");

        assert_eq!(4, w.bytes_written());
        assert_eq!(1, w.bytes_remaining());
        assert_eq!(&[42, 43, 2, 3], w.into_written());
        assert_eq!([42, 43, 2, 3, 1], buf);
    }

    #[test]
    fn can_append() {
        let mut buf = [1u8; 5];
        let mut w = BufferWriter::new(&mut buf[..]);
        assert!(w.can_append(0));
        assert!(w.can_append(4));
        assert!(w.can_append(5));
        assert!(!w.can_append(6));

        w.append_byte(42).unwrap();
        assert!(w.can_append(4));
        assert!(!w.can_append(5));
    }
}
