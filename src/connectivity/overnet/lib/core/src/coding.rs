// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Message encode/decode helpers

use failure::Error;

pub fn decode_fidl<T: fidl::encoding::Decodable>(bytes: &mut [u8]) -> Result<T, Error> {
    let mut value = T::new_empty();
    // This is OK because overnet interfaces do not use static unions.
    let context = fidl::encoding::Context { unions_use_xunion_format: true };
    fidl::encoding::Decoder::decode_with_context(&context, bytes, &mut [], &mut value)?;
    Ok(value)
}
