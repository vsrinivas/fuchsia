// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Message encode/decode helpers

use anyhow::Error;

/// Context derived from initial overnet handshake.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct Context {
    // If enabled, FIDL byte are framed with the persistence header.
    pub use_persistent_header: bool,
}

pub const DEFAULT_CONTEXT: Context = Context { use_persistent_header: false };

/// Decode some bytes into a FIDL type
pub fn decode_fidl_with_context<T: fidl::encoding::Persistable>(
    ctx: Context,
    bytes: &mut [u8],
) -> Result<T, Error> {
    if ctx.use_persistent_header {
        fidl::encoding::decode_persistent(bytes).map_err(Into::into)
    } else {
        let mut value = T::new_empty();
        // WARNING: Since we are decoding without a transaction header, we have to
        // provide a context manually. This could cause problems in future FIDL wire
        // format migrations, which are driven by header flags.
        let context =
            fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V1 };
        fidl::encoding::Decoder::decode_with_context(&context, bytes, &mut [], &mut value)?;
        Ok(value)
    }
}

/// Encode a FIDL type into some bytes
pub fn encode_fidl_with_context(
    ctx: Context,
    value: &mut impl fidl::encoding::Persistable,
) -> Result<Vec<u8>, Error> {
    if ctx.use_persistent_header {
        fidl::encoding::encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V1 },
            value,
        )
        .map_err(Into::into)
    } else {
        let (mut bytes, mut handles) = (Vec::new(), Vec::new());
        fidl::encoding::Encoder::encode_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V1 },
            &mut bytes,
            &mut handles,
            value,
        )?;
        assert_eq!(handles.len(), 0);
        Ok(bytes)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::fidl_struct;

    #[derive(Debug, PartialEq)]
    struct Foo {
        byte: u8,
    }

    impl fidl::encoding::Persistable for Foo {}

    fidl_struct! {
        name: Foo,
        members: [
            byte {
                ty: u8,
                offset_v1: 0,
                offset_v2: 0,
            },
        ],
        padding_v1: [
            {
                ty: u64,
                offset: 0,
                mask: 0xffffffffffffff00,
            },
        ],
        padding_v2: [
            {
                ty: u64,
                offset: 0,
                mask: 0xffffffffffffff00,
            },
        ],
        size_v1: 8,
        size_v2: 8,
        align_v1: 8,
        align_v2: 8,
    }

    #[test]
    fn encode_decode_without_persistent_header() {
        let coding_context = Context { use_persistent_header: false };
        let mut bytes =
            encode_fidl_with_context(coding_context, &mut Foo { byte: 5 }).expect("encoding fails");
        let result: Foo =
            decode_fidl_with_context(coding_context, &mut bytes).expect("decoding fails");
        assert_eq!(result.byte, 5);
    }

    #[test]
    fn encode_decode_with_persistent_header() {
        let coding_context = Context { use_persistent_header: true };
        let mut bytes =
            encode_fidl_with_context(coding_context, &mut Foo { byte: 5 }).expect("encoding fails");
        let result: Foo =
            decode_fidl_with_context(coding_context, &mut bytes).expect("decoding fails");
        assert_eq!(result.byte, 5);
    }
}
