// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Header, Id},
    crate::buffer_reader::BufferReader,
    std::mem::size_of,
    zerocopy::ByteSlice,
};

pub struct Reader<B>(BufferReader<B>);

impl<B: ByteSlice> Reader<B> {
    pub fn new(bytes: B) -> Self {
        Reader(BufferReader::new(bytes))
    }
}

impl<B: ByteSlice> Iterator for Reader<B> {
    type Item = (Id, B);

    fn next(&mut self) -> Option<Self::Item> {
        let header = self.0.peek::<Header>()?;
        let body_len = header.body_len as usize;
        if self.0.bytes_remaining() < size_of::<Header>() + body_len {
            None
        } else {
            // Unwraps are OK because we checked the length above
            let header = self.0.read::<Header>().unwrap();
            let body = self.0.read_bytes(body_len).unwrap();
            Some((header.id, body))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn empty() {
        assert_eq!(None, Reader::new(&[][..]).next());
    }

    #[test]
    pub fn less_than_header() {
        assert_eq!(None, Reader::new(&[0][..]).next());
    }

    #[test]
    pub fn body_too_short() {
        assert_eq!(None, Reader::new(&[0, 2, 10][..]).next());
    }

    #[test]
    pub fn empty_body() {
        let elems: Vec<_> = Reader::new(&[7, 0][..]).collect();
        assert_eq!(&[(Id(7), &[][..])], &elems[..]);
    }

    #[test]
    pub fn two_elements() {
        let bytes = vec![0, 2, 10, 20, 1, 3, 11, 22, 33];
        let elems: Vec<_> = Reader::new(&bytes[..]).collect();
        assert_eq!(&[(Id(0), &[10, 20][..]), (Id(1), &[11, 22, 33][..])], &elems[..]);
    }
}
