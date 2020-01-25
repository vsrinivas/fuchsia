// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::CtrlSubtype, zerocopy::ByteSlice};

mod fields;

pub use fields::*;

#[derive(Debug)]
pub enum CtrlBody<B: ByteSlice> {
    PsPoll,
    Unsupported {
        subtype: CtrlSubtype,
        // TODO: Remove this marker once other fields actually have a body.
        _phantom: std::marker::PhantomData<B>,
    },
}

impl<B: ByteSlice> CtrlBody<B> {
    pub fn parse(subtype: CtrlSubtype, _bytes: B) -> Option<Self> {
        match subtype {
            CtrlSubtype::PS_POLL => Some(CtrlBody::PsPoll),
            subtype => Some(CtrlBody::Unsupported { subtype, _phantom: std::marker::PhantomData }),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::assert_variant};

    #[test]
    fn parse_ps_poll_frame() {
        let bytes = vec![
            0b10100100, 0b00000000, // Frame Control
            0b00000001, 0b11000000, // Masked AID
            2, 2, 2, 2, 2, 2, // addr1
            4, 4, 4, 4, 4, 4, // addr2
        ];
        assert_variant!(
            CtrlBody::parse(CtrlSubtype::PS_POLL, &bytes[..]),
            Some(CtrlBody::PsPoll),
            "expected PS-Poll frame"
        );
    }
}
