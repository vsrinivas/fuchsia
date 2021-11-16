use std::io::Write;

use crate::Result;
use crate::der::{self, Tag};

/// Helper for writing DER that automattically encoes tags and content lengths.
pub struct Der<'a, W: Write + 'a> {
    writer: &'a mut W,
}

impl<'a, W: Write> Der<'a, W> {
    /// Create a new `Der` structure that writes values to the given writer.
    pub fn new(writer: &'a mut W) -> Self {
        Der { writer: writer }
    }

    fn write_len(&mut self, len: usize) -> Result<()> {
        if len >= 128 {
            let n = der::length_of_length(len);
            self.writer.write_all(&[0x80 | n])?;

            for i in (1..n + 1).rev() {
                self.writer.write_all(&[(len >> ((i - 1) * 8)) as u8])?;
            }
        } else {
            self.writer.write_all(&[len as u8])?;
        }

        Ok(())
    }

    /// Write a `NULL` tag.
    pub fn null(&mut self) -> Result<()> {
        Ok(self.writer.write_all(&[Tag::Null as u8, 0])?)
    }

    /// Write an arbitrary element.
    pub fn element(&mut self, tag: Tag, input: &[u8]) -> Result<()> {
        self.writer.write_all(&[tag as u8])?;
        self.write_len(input.len())?;
        self.writer.write_all(input)?;
        Ok(())
    }

    /// Write the given input as an integer.
    pub fn integer(&mut self, input: &[u8]) -> Result<()> {
        self.writer.write_all(&[Tag::Integer as u8])?;
        self.write_len(input.len())?;
        self.writer.write_all(input)?;
        Ok(())
    }

    /// Write the given input as a positive integer.
    pub fn positive_integer(&mut self, input: &[u8]) -> Result<()> {
        self.writer.write_all(&[Tag::Integer as u8])?;

        let push_zero = if input.len() > 0 {
            input[0] & 0x80 == 0x80
        } else {
            false
        };

        self.write_len(input.len() + push_zero as usize)?;

        if push_zero {
            self.writer.write_all(&[0x00])?;
        }

        self.writer.write_all(input)?;
        Ok(())
    }

    /// Write a nested structure by passing in a handling function that writes to an intermediate
    /// `Vec` before writing the whole sequence to `self`.
    pub fn nested<F: FnOnce(&mut Der<Vec<u8>>) -> Result<()>>(
        &mut self,
        tag: Tag,
        func: F,
    ) -> Result<()> {
        let mut buf = Vec::new();

        {
            let mut inner = Der::new(&mut buf);
            func(&mut inner)?;
        }

        self.writer.write_all(&[tag as u8])?;
        self.write_len(buf.len())?;
        Ok(self.writer.write_all(&buf)?)
    }

    /// Write a `SEQUENCE` by passing in a handling function that writes to an intermediate `Vec`
    /// before writing the whole sequence to `self`.
    pub fn sequence<F: FnOnce(&mut Der<Vec<u8>>) -> Result<()>>(&mut self, func: F) -> Result<()> {
        self.nested(Tag::Sequence, func)
    }

    /// Write an `OBJECT IDENTIFIER`.
    pub fn oid(&mut self, input: &[u8]) -> Result<()> {
        self.writer.write_all(&[Tag::Oid as u8])?;
        self.write_len(input.len())?;
        self.writer.write_all(&input)?;
        Ok(())
    }

    /// Write raw bytes to `self`. This does not calculate length or apply. This should only be used
    /// when you know you are dealing with bytes that are already DER encoded.
    pub fn raw(&mut self, input: &[u8]) -> Result<()> {
        Ok(self.writer.write_all(input)?)
    }

    /// Write a `BIT STRING`.
    pub fn bit_string(&mut self, unused_bits: u8, bit_string: &[u8]) -> Result<()> {
        self.writer.write_all(&[Tag::BitString as u8])?;
        self.write_len(bit_string.len() + 1)?;
        self.writer.write_all(&[unused_bits])?;
        self.writer.write_all(&bit_string)?;
        Ok(())
    }

    /// Write an `OCTET STRING`.
    pub fn octet_string(&mut self, octet_string: &[u8]) -> Result<()> {
        self.writer.write_all(&[Tag::OctetString as u8])?;
        self.write_len(octet_string.len())?;
        self.writer.write_all(&octet_string)?;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use untrusted::Input;
    use crate::Error;

    static RSA_2048_PKCS1: &'static [u8] = include_bytes!("../tests/rsa-2048.pkcs1.der");

    #[test]
    fn write_pkcs1() {
        let input = Input::from(RSA_2048_PKCS1);
        let (n, e) = input
            .read_all(Error::Read, |input| {
                der::nested(input, Tag::Sequence, |input| {
                    let n = der::positive_integer(input)?;
                    let e = der::positive_integer(input)?;
                    Ok((n.as_slice_less_safe(), e.as_slice_less_safe()))
                })
            })
            .unwrap();

        let mut buf = Vec::new();
        {
            let mut der = Der::new(&mut buf);
            der.sequence(|der| {
                der.positive_integer(n)?;
                der.positive_integer(e)
            })
            .unwrap();
        }

        assert_eq!(buf.as_slice(), RSA_2048_PKCS1);
    }

    #[test]
    fn write_octet_string() {
        let mut buf = Vec::new();
        {
            let mut der = Der::new(&mut buf);
            der.octet_string(&[]).unwrap();
        }

        assert_eq!(&buf, &[0x04, 0x00]);

        let mut buf = Vec::new();
        {
            let mut der = Der::new(&mut buf);
            der.octet_string(&[0x0a, 0x0b, 0x0c]).unwrap();
        }

        assert_eq!(&buf, &[0x04, 0x03, 0x0a, 0x0b, 0x0c]);
    }
}
