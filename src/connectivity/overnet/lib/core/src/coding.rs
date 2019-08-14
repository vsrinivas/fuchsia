// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Message encode/decode helpers

use failure::Error;

pub fn decode_fidl<T: fidl::encoding::Decodable>(bytes: &mut [u8]) -> Result<T, Error> {
    let mut value = T::new_empty();
    fidl::encoding::Decoder::decode_into(bytes, &mut [], &mut value)?;
    Ok(value)
}
