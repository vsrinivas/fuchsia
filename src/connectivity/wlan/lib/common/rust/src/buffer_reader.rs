// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::unaligned_view::UnalignedView,
    core::mem::size_of,
    zerocopy::{ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned},
};

pub struct BufferReader<B> {
    buffer: Option<B>,
    bytes_read: usize,
}

impl<B: ByteSlice> BufferReader<B> {
    pub fn new(bytes: B) -> Self {
        BufferReader { buffer: Some(bytes), bytes_read: 0 }
    }

    pub fn read<T>(&mut self) -> Option<LayoutVerified<B, T>>
    where
        T: Unaligned + FromBytes,
    {
        self.read_bytes(size_of::<T>()).map(|bytes| LayoutVerified::new_unaligned(bytes).unwrap())
    }

    pub fn read_unaligned<T>(&mut self) -> Option<UnalignedView<B, T>>
    where
        T: FromBytes,
    {
        self.read_bytes(size_of::<T>()).map(|bytes| UnalignedView::new(bytes).unwrap())
    }

    pub fn peek<T>(&self) -> Option<LayoutVerified<&[u8], T>>
    where
        T: Unaligned + FromBytes,
    {
        self.peek_bytes(size_of::<T>()).map(|bytes| LayoutVerified::new_unaligned(bytes).unwrap())
    }

    pub fn peek_unaligned<T>(&self) -> Option<UnalignedView<&[u8], T>>
    where
        T: FromBytes,
    {
        self.peek_bytes(size_of::<T>()).map(|bytes| UnalignedView::new(bytes).unwrap())
    }

    pub fn read_array<T>(&mut self, num_elems: usize) -> Option<LayoutVerified<B, [T]>>
    where
        T: Unaligned + FromBytes,
    {
        self.read_bytes(size_of::<T>() * num_elems)
            .map(|bytes| LayoutVerified::new_slice_unaligned(bytes).unwrap())
    }

    pub fn peek_array<T>(&self, num_elems: usize) -> Option<LayoutVerified<&[u8], [T]>>
    where
        T: Unaligned + FromBytes,
    {
        self.peek_bytes(size_of::<T>() * num_elems)
            .map(|bytes| LayoutVerified::new_slice_unaligned(bytes).unwrap())
    }

    pub fn read_byte(&mut self) -> Option<u8> {
        self.read_bytes(1).map(|bytes| bytes[0])
    }

    pub fn peek_byte(&mut self) -> Option<u8> {
        self.peek_bytes(1).map(|bytes| bytes[0])
    }

    /// Useful for reading integers.
    ///
    /// Example:
    /// ```
    /// let mut reader = BufferReader::new(&vec![1, 2, 3]);
    /// let val = reader.read_value::<u16>();
    /// assert_eq!(Some(1 + 256 * 2), val);
    /// ```
    pub fn read_value<T>(&mut self) -> Option<T>
    where
        T: FromBytes + Copy,
    {
        self.read_unaligned::<T>().map(|view| view.get())
    }

    pub fn peek_value<T>(&self) -> Option<T>
    where
        T: FromBytes + Copy,
    {
        self.peek_unaligned::<T>().map(|view| view.get())
    }

    pub fn read_bytes(&mut self, len: usize) -> Option<B> {
        if self.buffer.as_ref().unwrap().len() >= len {
            let (head, tail) = self.buffer.take().unwrap().split_at(len);
            self.buffer = Some(tail);
            self.bytes_read += len;
            Some(head)
        } else {
            None
        }
    }

    pub fn peek_bytes(&self, len: usize) -> Option<&[u8]> {
        let buffer = self.buffer.as_ref().unwrap();
        if buffer.len() >= len {
            Some(&buffer[0..len])
        } else {
            None
        }
    }

    pub fn peek_remaining(&self) -> &[u8] {
        &self.buffer.as_ref().unwrap()[..]
    }

    pub fn bytes_read(&self) -> usize {
        self.bytes_read
    }

    pub fn bytes_remaining(&self) -> usize {
        self.buffer.as_ref().unwrap().len()
    }

    pub fn into_remaining(self) -> B {
        self.buffer.unwrap()
    }
}

impl<B: ByteSliceMut> BufferReader<B> {
    pub fn peek_mut<T>(&mut self) -> Option<LayoutVerified<&mut [u8], T>>
    where
        T: Unaligned + FromBytes,
    {
        self.peek_bytes_mut(size_of::<T>())
            .map(|bytes| LayoutVerified::new_unaligned(bytes).unwrap())
    }

    pub fn peek_mut_unaligned<T>(&mut self) -> Option<UnalignedView<&mut [u8], T>>
    where
        T: FromBytes,
    {
        self.peek_bytes_mut(size_of::<T>()).map(|bytes| UnalignedView::new(bytes).unwrap())
    }

    pub fn peek_array_mut<T>(&mut self, num_elems: usize) -> Option<LayoutVerified<&mut [u8], [T]>>
    where
        T: Unaligned + FromBytes,
    {
        self.peek_bytes_mut(size_of::<T>() * num_elems)
            .map(|bytes| LayoutVerified::new_slice_unaligned(bytes).unwrap())
    }

    pub fn peek_bytes_mut(&mut self, len: usize) -> Option<&mut [u8]> {
        let buffer = self.buffer.as_mut().unwrap();
        if buffer.len() >= len {
            Some(&mut buffer[0..len])
        } else {
            None
        }
    }

    pub fn peek_remaining_mut(&mut self) -> &mut [u8] {
        &mut self.buffer.as_mut().unwrap()[..]
    }
}

#[cfg(test)]
mod tests {
    use {super::*, zerocopy::AsBytes};

    #[repr(C, packed)]
    #[derive(AsBytes, FromBytes, Unaligned)]
    struct Foo {
        x: u8,
        y: u16,
    }

    #[test]
    pub fn read() {
        let mut data = vec![1u8, 2, 3, 4, 5, 6, 7];
        let mut reader = BufferReader::new(&mut data[..]);
        let foo = reader.read::<Foo>().expect("expected a Foo to be read");
        assert_eq!(1, foo.x);
        let y = foo.y;
        assert_eq!(2 + 3 * 256, y); // assuming little endian
        assert_eq!(3, reader.bytes_read());
        assert_eq!(4, reader.bytes_remaining());

        let bytes = reader.read_bytes(2).expect("expected 2 bytes to be read");
        assert_eq!(&[4, 5], bytes);
        assert_eq!(5, reader.bytes_read());
        assert_eq!(2, reader.bytes_remaining());

        assert!(reader.read::<Foo>().is_none());

        let rest = reader.into_remaining();
        assert_eq!(&[6, 7], rest);
    }

    #[test]
    pub fn peek() {
        let mut data = vec![1u8, 2, 3, 4, 5, 6, 7];
        let mut reader = BufferReader::new(&mut data[..]);

        let foo = reader.peek::<Foo>().expect("expected a Foo (1)");
        assert_eq!(1, foo.x);

        let foo = reader.peek::<Foo>().expect("expected a Foo (2)");
        assert_eq!(1, foo.x);

        assert_eq!(0, reader.bytes_read());
        assert_eq!(7, reader.bytes_remaining());

        reader.read_bytes(5);

        let bytes = reader.peek_bytes(2).expect("expected a slice of 2 bytes");
        assert_eq!(&[6, 7], bytes);

        assert!(reader.peek_bytes(3).is_none());

        assert_eq!(&[6, 7], reader.peek_remaining());
    }

    #[test]
    pub fn peek_mut() {
        let mut data = vec![1u8, 2, 3, 4, 5, 6, 7];
        let mut reader = BufferReader::new(&mut data[..]);

        let foo = reader.peek_mut::<Foo>().expect("expected a Foo (1)");
        assert_eq!(1, foo.x);

        let foo = reader.peek_mut::<Foo>().expect("expected a Foo (2)");
        assert_eq!(1, foo.x);

        assert_eq!(0, reader.bytes_read());
        assert_eq!(7, reader.bytes_remaining());

        reader.read_bytes(5);

        let bytes = reader.peek_bytes_mut(2).expect("expected a slice of 2 bytes");
        assert_eq!(&[6, 7], bytes);

        assert!(reader.peek_bytes_mut(3).is_none());

        assert_eq!(&[6, 7], reader.peek_remaining_mut());
    }

    #[test]
    pub fn peek_and_read_value() {
        let mut data = vec![1u8, 2, 3, 4];
        let mut reader = BufferReader::new(&mut data[..]);

        assert_eq!(Some(1), reader.peek_byte());
        assert_eq!(0, reader.bytes_read());
        assert_eq!(4, reader.bytes_remaining());

        assert_eq!(Some(1), reader.read_byte());
        assert_eq!(1, reader.bytes_read());
        assert_eq!(3, reader.bytes_remaining());

        assert_eq!(Some(2 + 256 * 3), reader.peek_value::<u16>()); // assuming little endian
        assert_eq!(1, reader.bytes_read());
        assert_eq!(3, reader.bytes_remaining());

        assert_eq!(Some(2 + 256 * 3), reader.read_value::<u16>()); // assuming little endian
        assert_eq!(3, reader.bytes_read());
        assert_eq!(1, reader.bytes_remaining());

        assert_eq!(None, reader.peek_value::<u16>());
        assert_eq!(None, reader.read_value::<u16>());
        assert_eq!(3, reader.bytes_read());
        assert_eq!(1, reader.bytes_remaining());
    }

    #[test]
    pub fn peek_and_read_array() {
        let mut data = vec![1u8, 2, 3, 4, 5, 6, 7, 8];
        let mut reader = BufferReader::new(&mut data[..]);

        let arr = reader.peek_array::<Foo>(2).expect("expected peek() to return Some");
        assert_eq!(2, arr.len());
        assert_eq!(1, arr[0].x);
        assert_eq!(4, arr[1].x);

        assert_eq!(0, reader.bytes_read());
        assert_eq!(8, reader.bytes_remaining());

        let arr = reader.peek_array_mut::<Foo>(2).expect("expected peek() to return Some");
        assert_eq!(2, arr.len());
        assert_eq!(1, arr[0].x);
        assert_eq!(4, arr[1].x);

        assert_eq!(0, reader.bytes_read());
        assert_eq!(8, reader.bytes_remaining());

        let arr = reader.read_array::<Foo>(2).expect("expected peek() to return Some");
        assert_eq!(2, arr.len());
        assert_eq!(1, arr[0].x);
        assert_eq!(4, arr[1].x);

        assert_eq!(6, reader.bytes_read());
        assert_eq!(2, reader.bytes_remaining());

        assert!(reader.peek_array::<Foo>(1).is_none());
        assert!(reader.read_array::<Foo>(1).is_none());
        assert_eq!(6, reader.bytes_read());
        assert_eq!(2, reader.bytes_remaining());
    }

    #[test]
    pub fn peek_mut_and_modify() {
        let mut data = vec![1u8, 2, 3, 4, 5, 6, 7, 8];
        let mut reader = BufferReader::new(&mut data[..]);

        let mut foo = reader.peek_mut::<Foo>().expect("expected peek() to return Some");
        foo.y = 0xaabb;

        let foo = reader.read::<Foo>().expect("expected read() to return Some");
        let y = foo.y;
        assert_eq!(0xaabb, y);
        assert_eq!(0xbb, data[1]);
        assert_eq!(0xaa, data[2]);
    }

    #[test]
    pub fn unaligned_access() {
        let mut data = vec![1u8, 2, 3, 4, 5, 6];
        let mut reader = BufferReader::new(&mut data[..]);

        reader.read_byte().expect("expected read_byte to return Ok");

        let mut number =
            reader.peek_mut_unaligned::<u32>().expect("expected peek_mut_unaligned to return Ok");
        assert_eq!(0x05040302, number.get());
        number.set(0x0a090807);
        assert_eq!(1, reader.bytes_read());

        let number = reader.peek_unaligned::<u32>().expect("expected peek_unaligned to return Ok");
        assert_eq!(0x0a090807, number.get());
        assert_eq!(1, reader.bytes_read());

        let number = reader.read_unaligned::<u32>().expect("expected read_unaligned to return Ok");
        assert_eq!(0x0a090807, number.get());
        assert_eq!(5, reader.bytes_read());

        assert_eq!(&[1, 7, 8, 9, 10, 6], &data[..]);
    }
}
