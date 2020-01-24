// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Message encode/decode helpers

use anyhow::Error;

pub fn decode_fidl<T: fidl::encoding::Decodable>(bytes: &mut [u8]) -> Result<T, Error> {
    let mut value = T::new_empty();
    // WARNING: Since we are decoding without a transaction header, we have to
    // provide a context manually. This could cause problems in future FIDL wire
    // format migrations, which are driven by header flags.
    let context = fidl::encoding::Context {};
    fidl::encoding::Decoder::decode_with_context(&context, bytes, &mut [], &mut value)?;
    Ok(value)
}

pub fn encode_fidl(value: &mut impl fidl::encoding::Encodable) -> Result<Vec<u8>, Error> {
    let (mut bytes, mut handles) = (Vec::new(), Vec::new());
    fidl::encoding::Encoder::encode(&mut bytes, &mut handles, value)?;
    assert_eq!(handles.len(), 0);
    Ok(bytes)
}
