// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::{Range, RangeBounds};

use crate::{canonicalize_range, take_back, take_back_mut, take_front, take_front_mut, Buffer,
            BufferMut, BufferView, BufferViewMut, ParsablePacket, ParseBuffer, ParseBufferMut};

/// A byte slice wrapper providing [`Buffer`] functionality.
///
/// A `Buf` wraps a byte slice (a type which implements `AsRef<[u8]>` or
/// `AsMut<[u8]>`) and implements `Buffer` and `BufferMut` by keeping track of
/// prefix, body, and suffix offsets within the byte slice.
pub struct Buf<B> {
    buf: B,
    range: Range<usize>,
}

impl<B: AsRef<[u8]>> Buf<B> {
    /// Constructs a new `Buf`.
    ///
    /// `new` constructs a new `Buf` from a buffer and a range. The bytes within
    /// the range will be the body, the bytes before the range will be the
    /// prefix, and the bytes after the range will be the suffix.
    ///
    /// # Panics
    ///
    /// `new` panics if `range` is out of bounds of `buf`, or if it is
    /// nonsensical (the end precedes the start).
    pub fn new<R: RangeBounds<usize>>(buf: B, range: R) -> Buf<B> {
        let len = buf.as_ref().len();
        Buf {
            buf,
            range: canonicalize_range(len, &range),
        }
    }

    // in a separate method so it can be used in testing
    pub(crate) fn buffer_view(&mut self) -> BufView {
        BufView {
            buf: self.buf.as_ref(),
            range: &mut self.range,
        }
    }
}

impl<B: AsRef<[u8]>> ParseBuffer for Buf<B> {
    fn shrink<R: RangeBounds<usize>>(&mut self, range: R) {
        let len = self.len();
        let mut range = canonicalize_range(len, &range);
        range.start += self.range.start;
        range.end += self.range.start;
        self.range = range;
    }

    fn len(&self) -> usize {
        self.range.end - self.range.start
    }
    fn shrink_front(&mut self, n: usize) {
        assert!(n <= self.len());
        self.range.start += n;
    }
    fn shrink_back(&mut self, n: usize) {
        assert!(n <= self.len());
        self.range.end -= n;
    }
    fn parse_with<'a, ParseArgs, P: ParsablePacket<&'a [u8], ParseArgs>>(
        &'a mut self, args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse(self.buffer_view(), args)
    }
    fn as_buf(&self) -> Buf<&[u8]> {
        // TODO(joshlf): Once we have impl specialization, can we specialize this at all?
        Buf::new(self.buf.as_ref(), self.range.clone())
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> ParseBufferMut for Buf<B> {
    fn parse_with_mut<'a, ParseArgs, P: ParsablePacket<&'a mut [u8], ParseArgs>>(
        &'a mut self, args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse(
            BufViewMut {
                buf: self.buf.as_mut(),
                range: &mut self.range,
            },
            args,
        )
    }
    fn as_buf_mut(&mut self) -> Buf<&mut [u8]> {
        Buf::new(self.buf.as_mut(), self.range.clone())
    }
}

impl<B: AsRef<[u8]>> Buffer for Buf<B> {
    fn capacity(&self) -> usize {
        self.buf.as_ref().len()
    }
    fn prefix_len(&self) -> usize {
        self.range.start
    }
    fn suffix_len(&self) -> usize {
        self.buf.as_ref().len() - self.range.end
    }
    fn grow_front(&mut self, n: usize) {
        assert!(n <= self.range.start);
        self.range.start -= n;
    }
    fn grow_back(&mut self, n: usize) {
        assert!(n <= self.buf.as_ref().len() - self.range.end);
        self.range.end += n;
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> BufferMut for Buf<B> {}

impl<B: AsRef<[u8]>> AsRef<[u8]> for Buf<B> {
    fn as_ref(&self) -> &[u8] {
        &self.buf.as_ref()[self.range.clone()]
    }
}

impl<B: AsMut<[u8]>> AsMut<[u8]> for Buf<B> {
    fn as_mut(&mut self) -> &mut [u8] {
        &mut self.buf.as_mut()[self.range.clone()]
    }
}

// used in testing in a different module
pub(crate) struct BufView<'a> {
    buf: &'a [u8],
    range: &'a mut Range<usize>,
}

impl<'a> BufferView<&'a [u8]> for BufView<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.start += n;
        Some(take_front(&mut self.buf, n))
    }

    fn take_back(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.end -= n;
        Some(take_back(&mut self.buf, n))
    }

    fn into_rest(self) -> &'a [u8] {
        self.buf
    }
}

impl<'a> AsRef<[u8]> for BufView<'a> {
    fn as_ref(&self) -> &[u8] {
        self.buf
    }
}

struct BufViewMut<'a> {
    buf: &'a mut [u8],
    range: &'a mut Range<usize>,
}

impl<'a> BufferView<&'a mut [u8]> for BufViewMut<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a mut [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.start += n;
        Some(take_front_mut(&mut self.buf, n))
    }

    fn take_back(&mut self, n: usize) -> Option<&'a mut [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.end -= n;
        Some(take_back_mut(&mut self.buf, n))
    }

    fn into_rest(self) -> &'a mut [u8] {
        self.buf
    }
}

impl<'a> BufferViewMut<&'a mut [u8]> for BufViewMut<'a> {}

impl<'a> AsRef<[u8]> for BufViewMut<'a> {
    fn as_ref(&self) -> &[u8] {
        self.buf
    }
}

impl<'a> AsMut<[u8]> for BufViewMut<'a> {
    fn as_mut(&mut self) -> &mut [u8] {
        self.buf
    }
}
