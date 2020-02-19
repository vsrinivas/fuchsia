// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use bytes::{Bytes, BytesMut};

/// Convenience method to create a new `Bytes` with the supplied input padded to the specified
/// `length` using with zeros.
pub fn pad(data: &[u8], length: u16) -> Result<Bytes, Error> {
    // The fact that FIDL bindings require mutable data when sending a packet means we need to
    // define packets as the full length instead of only defining a packet as the meaningful
    // payload and iterating over additional fixed zero bytes to form the full length.
    //
    // Defining padded packets requires a copy operatation (unlike the other zero-copy
    // initializers) and so we define padding methods that use byte array references.
    if data.len() > length as usize {
        return Err(format_err!("Data to pad exceeded requested length"));
    }
    let mut bytes = BytesMut::with_capacity(length as usize);
    bytes.extend(data);
    bytes.extend(&vec![0; length as usize - data.len()]);
    Ok(bytes.freeze())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pad_to_longer_length() -> Result<(), Error> {
        let padded = pad(&vec![0x99, 0x88, 0x77], 5)?;
        assert_eq!(padded, &vec![0x99, 0x88, 0x77, 0x00, 0x00]);
        Ok(())
    }

    #[test]
    fn pad_to_current_length() -> Result<(), Error> {
        let padded = pad(&vec![0x99, 0x88, 0x77], 3)?;
        assert_eq!(padded, &vec![0x99, 0x88, 0x77]);
        Ok(())
    }

    #[test]
    fn pad_to_shorter_length() {
        assert!(pad(&vec![0x99, 0x88, 0x77], 2).is_err());
    }
}
