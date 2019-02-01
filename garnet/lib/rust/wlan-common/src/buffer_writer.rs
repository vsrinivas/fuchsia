// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{ensure, format_err, Error},
    zerocopy::{self, LayoutVerified, Unaligned},
};

pub use zerocopy::ByteSliceMut;

pub struct BufferWriter<B: ByteSliceMut> {
    buf: B,
    written_bytes: usize,
}

impl<B: ByteSliceMut> BufferWriter<B> {
    pub fn new(buf: B) -> Self {
        Self { written_bytes: 0, buf }
    }

    /// Writes a single given `byte` and returns a `BufferWriter` to write remaining bytes of the
    /// original buffer.
    pub fn write_byte(mut self, byte: u8) -> Result<Self, Error> {
        ensure!(self.buf.len() >= 1, "buffer too short");
        let (mut data, remaining) = self.buf.split_at(1);
        data[0] = byte;
        Ok(Self { buf: remaining, written_bytes: self.written_bytes + 1 })
    }

    /// Writes the given `bytes` and returns a `BufferWriter` to write remaining bytes of the
    /// original buffer.
    pub fn write_bytes(mut self, bytes: &[u8]) -> Result<Self, Error> {
        ensure!(self.buf.len() >= bytes.len(), "buffer too short");
        let (mut data, remaining) = self.buf.split_at(bytes.len());
        data.copy_from_slice(bytes);
        Ok(Self { buf: remaining, written_bytes: self.written_bytes + bytes.len() })
    }

    /// Reserves and zeroes a typed chunk of bytes of size |mem::size_of::<T>|.
    /// Returns a tuple of the typed reserved chunk and a `BufferWriter` for writing remaining
    /// bytes of the original buffer.
    pub fn reserve_zeroed<T: Unaligned>(mut self) -> Result<(LayoutVerified<B, T>, Self), Error> {
        let (data, remaining) = LayoutVerified::new_unaligned_from_prefix_zeroed(self.buf)
            .ok_or(format_err!("buffer too short"))?;
        Ok((
            data,
            Self { buf: remaining, written_bytes: self.written_bytes + std::mem::size_of::<T>() },
        ))
    }

    pub fn written_bytes(&self) -> usize {
        self.written_bytes
    }

    pub fn remaining_bytes(&self) -> usize {
        self.buf.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn buffer_too_short() {
        assert!(BufferWriter::new(&mut [0; 0][..]).write_byte(1).is_err());
        assert!(BufferWriter::new(&mut [0; 5][..]).write_bytes(&[0; 6]).is_err());
        assert!(BufferWriter::new(&mut [0; 5][..]).reserve_zeroed::<[u8; 6]>().is_err());
    }

    #[test]
    fn reserve_zeroed_for_type() {
        let mut buf = [1u8; 5];
        let (mut data, w) = BufferWriter::new(&mut buf[..])
            .reserve_zeroed::<[u8; 3]>()
            .expect("failed writing buffer");
        data[0] = 42;
        // Don't write `data[1]`: BufferWriter should zero this byte.
        data[2] = 43;

        assert_eq!(3, w.written_bytes());
        assert_eq!(2, w.remaining_bytes());
        assert_eq!([42, 0, 43, 1, 1], buf);
    }

    #[test]
    fn write_bytes() {
        let mut buf = [1u8; 5];
        let w = BufferWriter::new(&mut buf[..])
            .write_byte(42)
            .expect("failed writing buffer")
            .write_byte(43)
            .expect("failed writing buffer")
            .write_bytes(&[2, 3])
            .expect("failed writing buffer");

        assert_eq!(4, w.written_bytes());
        assert_eq!(1, w.remaining_bytes());
        assert_eq!([42, 43, 2, 3, 1], buf);
    }
}
