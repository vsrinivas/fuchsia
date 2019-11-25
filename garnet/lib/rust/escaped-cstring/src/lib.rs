// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::CStr;
use std::fmt;
use std::io::{self, Write};
use std::ops;

type EscapedBuf = smallvec::SmallVec<[u8; 128]>;

pub struct EscapedWriter {
    buf: EscapedBuf,
}

impl EscapedWriter {
    pub fn new() -> Self {
        EscapedWriter {
            buf: EscapedBuf::new(),
        }
    }

    pub fn finish(mut self) -> EscapedCString {
        // Make sure to add the trailing nul.
        self.buf.push(0);

        EscapedCString { buf: self.buf }
    }
}

impl Write for EscapedWriter {
    fn write(&mut self, mut buf: &[u8]) -> io::Result<usize> {
        let len = buf.len();
        self.buf.reserve(len);

        while let Some(pos) = memchr::memchr(b'\0', buf) {
            self.buf.extend_from_slice(&buf[..pos]);
            self.buf.extend_from_slice(b"\\0");

            // Skip over the nul character.
            buf = &buf[pos + 1..];
        }

        self.buf.extend_from_slice(buf);

        Ok(len)
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

#[derive(PartialEq, PartialOrd, Eq, Ord, Hash, Clone)]
pub struct EscapedCString {
    buf: EscapedBuf,
}

impl EscapedCString {
    pub fn as_bytes_with_nul(&self) -> &[u8] {
        &self.buf
    }
}

impl fmt::Debug for EscapedCString {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.as_ref().fmt(f)
    }
}

impl AsRef<CStr> for EscapedCString {
    #[inline]
    fn as_ref(&self) -> &CStr {
        self
    }
}

impl ops::Deref for EscapedCString {
    type Target = CStr;

    #[inline]
    fn deref(&self) -> &CStr {
        unsafe { CStr::from_bytes_with_nul_unchecked(self.as_bytes_with_nul()) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    #[test]
    fn test_without_nuls() {
        for s in &["", "foobar", "foo\\0bar"] {
            let mut w = EscapedWriter::new();
            w.write(s.as_bytes()).unwrap();

            assert_eq!(
                w.finish().as_ref(),
                CString::new(s.as_bytes()).unwrap().as_ref()
            );
        }
    }

    #[test]
    fn test_with_nuls() {
        for (s, expected) in &[("\0", "\\0"), ("foo\0bar", "foo\\0bar")] {
            let mut w = EscapedWriter::new();
            w.write(s.as_bytes()).unwrap();

            assert_eq!(
                w.finish().as_ref(),
                CString::new(expected.as_bytes()).unwrap().as_ref()
            );
        }
    }
}
