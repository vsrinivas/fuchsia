// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use std::convert::TryFrom;

/// Represents the 24-bit Model ID assigned to a Fast Pair device upon registration.
#[derive(Debug, Copy, Clone)]
pub struct ModelId(u32);

impl TryFrom<u32> for ModelId {
    type Error = Error;

    fn try_from(src: u32) -> Result<Self, Self::Error> {
        // u24::MAX
        if src > 0xffffff {
            return Err(format_err!("Invalid Model ID: {}", src));
        }

        Ok(Self(src))
    }
}

impl From<ModelId> for [u8; 3] {
    fn from(src: ModelId) -> [u8; 3] {
        let mut bytes = [0; 3];
        bytes[..3].copy_from_slice(&src.0.to_be_bytes()[1..]);
        bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    #[test]
    fn model_id_from_u32() {
        let normal_id = 0x1234;
        let id = ModelId::try_from(normal_id).expect("valid id");
        let id_bytes: [u8; 3] = id.into();
        assert_eq!(id_bytes, [0x00, 0x12, 0x34]);

        let zero_id = 0;
        let id = ModelId::try_from(zero_id).expect("valid id");
        let id_bytes: [u8; 3] = id.into();
        assert_eq!(id_bytes, [0x00, 0x00, 0x00]);

        let max_id = 0xffffff;
        let id = ModelId::try_from(max_id).expect("valid id");
        let id_bytes: [u8; 3] = id.into();
        assert_eq!(id_bytes, [0xff, 0xff, 0xff]);
    }

    #[test]
    fn invalid_model_id_conversion_is_error() {
        let invalid_id = 0x1ffabcd;
        assert_matches!(ModelId::try_from(invalid_id), Err(_));
    }
}
