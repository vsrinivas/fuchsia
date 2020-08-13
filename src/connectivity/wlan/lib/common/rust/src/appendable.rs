// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::mem::size_of,
    thiserror::Error,
    zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned},
};

#[derive(Error, Debug)]
#[error("Buffer is too small for the written data")]
pub struct BufferTooSmall;

pub trait Appendable {
    fn append_bytes(&mut self, bytes: &[u8]) -> Result<(), BufferTooSmall>;

    fn append_bytes_zeroed(&mut self, len: usize) -> Result<&mut [u8], BufferTooSmall>;

    fn bytes_written(&self) -> usize;

    fn can_append(&self, bytes: usize) -> bool;

    fn append_value<T>(&mut self, value: &T) -> Result<(), BufferTooSmall>
    where
        T: AsBytes + ?Sized,
    {
        self.append_bytes(value.as_bytes())
    }

    fn append_byte(&mut self, byte: u8) -> Result<(), BufferTooSmall> {
        self.append_bytes(&[byte])
    }

    fn append_value_zeroed<T>(&mut self) -> Result<LayoutVerified<&mut [u8], T>, BufferTooSmall>
    where
        T: FromBytes + Unaligned,
    {
        let bytes = self.append_bytes_zeroed(size_of::<T>())?;
        Ok(LayoutVerified::new_unaligned(bytes).unwrap())
    }

    fn append_array_zeroed<T>(
        &mut self,
        num_elems: usize,
    ) -> Result<LayoutVerified<&mut [u8], [T]>, BufferTooSmall>
    where
        T: FromBytes + Unaligned,
    {
        let bytes = self.append_bytes_zeroed(size_of::<T>() * num_elems)?;
        Ok(LayoutVerified::new_slice_unaligned(bytes).unwrap())
    }
}

impl Appendable for Vec<u8> {
    fn append_bytes(&mut self, bytes: &[u8]) -> Result<(), BufferTooSmall> {
        self.extend_from_slice(bytes);
        Ok(())
    }

    fn append_bytes_zeroed(&mut self, len: usize) -> Result<&mut [u8], BufferTooSmall> {
        let old_len = self.len();
        self.resize(old_len + len, 0);
        Ok(&mut self[old_len..])
    }

    fn bytes_written(&self) -> usize {
        self.len()
    }

    fn can_append(&self, _bytes: usize) -> bool {
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn append_to_vec() {
        let mut data = vec![];
        assert_eq!(0, data.bytes_written());

        data.append_bytes(&[1, 2, 3]).unwrap();
        assert_eq!(3, data.bytes_written());

        let bytes = data.append_bytes_zeroed(2).unwrap();
        bytes[0] = 4;
        assert_eq!(5, data.bytes_written());

        data.append_value(&0x0706_u16).unwrap();
        assert_eq!(7, data.bytes_written());

        data.append_byte(8).unwrap();
        assert_eq!(8, data.bytes_written());

        let mut bytes = data.append_value_zeroed::<[u8; 2]>().unwrap();
        bytes[0] = 9;
        assert_eq!(10, data.bytes_written());

        data.append_value(&[0x0c0b_u16, 0x0e0d_u16]).unwrap();
        assert_eq!(14, data.bytes_written());

        let mut arr = data.append_array_zeroed::<[u8; 2]>(2).unwrap();
        arr[0] = [15, 16];
        assert_eq!(18, data.bytes_written());

        #[rustfmt::skip]
        assert_eq!(
            &[
                1, 2, 3,
                4, 0,
                6, 7,
                8,
                9, 0,
                11, 12, 13, 14,
                15, 16, 0, 0
            ],
            &data[..]
        );
    }
}
