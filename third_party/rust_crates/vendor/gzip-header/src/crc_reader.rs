//! Simple CRC wrappers backed by the crc32 crate.

use std::io::{BufRead, Read};
use std::io;
use std::fmt;

use crc::crc32;

/// A wrapper struct containing a CRC checksum in the format used by gzip and the amount of bytes
/// input to it mod 2^32.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash)]
pub struct Crc {
    // We don't use the Digest struct from the Crc crate for now as it doesn't implement `Display`
    // and other common traits.
    checksum: u32,
    amt: u32,
}

impl fmt::Display for Crc {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "({:#x},{})", self.checksum, self.amt)
    }
}

/// A reader that updates the checksum and counter of a `Crc` struct when reading from the wrapped
/// reader.
#[derive(Debug)]
pub struct CrcReader<R> {
    inner: R,
    crc: Crc,
}

impl Crc {
    /// Create a new empty CRC struct.
    pub fn new() -> Crc {
        Crc {
            checksum: 0,
            amt: 0,
        }
    }

    pub fn with_initial(checksum: u32, amount: u32) -> Crc {
        Crc {
            checksum: checksum,
            amt: amount,
        }
    }

    /// Return the current checksum value.
    pub fn sum(&self) -> u32 {
        self.checksum
    }

    /// Return the number of bytes input.
    pub fn amt_as_u32(&self) -> u32 {
        self.amt
    }

    /// Update the checksum and byte counter with the provided data.
    pub fn update(&mut self, data: &[u8]) {
        self.amt = self.amt.wrapping_add(data.len() as u32);
        self.checksum = crc32::update(self.checksum, &crc32::IEEE_TABLE, data);
    }

    /// Reset the checksum and byte counter.
    pub fn reset(&mut self) {
        self.checksum = 0;
        self.amt = 0;
    }
}

impl<R: Read> CrcReader<R> {
    /// Create a new `CrcReader` with a blank checksum.
    pub fn new(r: R) -> CrcReader<R> {
        CrcReader {
            inner: r,
            crc: Crc::new(),
        }
    }

    /// Return a reference to the underlying checksum struct.
    pub fn crc(&self) -> &Crc {
        &self.crc
    }

    /// Consume this `CrcReader` and return the wrapped `Read` instance.
    pub fn into_inner(self) -> R {
        self.inner
    }

    /// Return a mutable reference to the wrapped reader.
    pub fn inner(&mut self) -> &mut R {
        &mut self.inner
    }

    /// Reset the checksum and counter.
    pub fn reset(&mut self) {
        self.crc.reset();
    }
}

impl<R: Read> Read for CrcReader<R> {
    fn read(&mut self, into: &mut [u8]) -> io::Result<usize> {
        let amt = try!(self.inner.read(into));
        self.crc.update(&into[..amt]);
        Ok(amt)
    }
}

impl<R: BufRead> BufRead for CrcReader<R> {
    fn fill_buf(&mut self) -> io::Result<&[u8]> {
        self.inner.fill_buf()
    }
    fn consume(&mut self, amt: usize) {
        if let Ok(data) = self.inner.fill_buf() {
            self.crc.update(&data[..amt]);
        }
        self.inner.consume(amt);
    }
}

#[cfg(test)]
mod test {
    use super::Crc;
    #[test]
    fn checksum_correct() {
        let mut c = Crc::new();
        c.update(b"abcdefg12345689\n");
        assert_eq!(c.sum(), 0x141ddb83);
        assert_eq!(c.amt_as_u32(), 16);
    }
}
