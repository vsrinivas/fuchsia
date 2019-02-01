// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::ByteSlice;

// This macro is used in combination with 802.11 definitions.
// These definitions have the same memory layout as their 802.11 silbing due to repr(C, packed)).
// Thus, we can simply reinterpret the bytes of these header as one of the corresponding structs,
// and can safely access its fields.
// Note the following caveats:
// - We cannot make any guarantees about the alignment of an instance of these
//   structs in memory or of any of its fields. This is true both because
//   repr(packed) removes the padding that would be used to ensure the alignment
//   of individual fields, but also because we are given no guarantees about
//   where within a given memory buffer a particular packet (and thus its
//   header) will be located.
// - Individual fields are all either u8 or [u8; N] rather than u16, u32, etc.
//   This is for two reasons:
//   - u16 and larger have larger-than-1 alignments, which are forbidden as
//     described above
//   - We are not guaranteed that the local platform has the same endianness as a header is defined
//     in, so simply treating a sequence of bytes
//     as a u16 or other multi-byte number would not necessarily be correct.
//     Instead, we use explicitly specify the endianess for its reader and writer methods
//     to correctly access these fields.
macro_rules! unsafe_impl_zerocopy_traits {
    ($type:ty) => {
        unsafe impl FromBytes for $type {}
        unsafe impl AsBytes for $type {}
        unsafe impl Unaligned for $type {}
    };
}

pub fn skip<B: ByteSlice>(bytes: B, skip: usize) -> Option<B> {
    if bytes.len() < skip {
        None
    } else {
        Some(bytes.split_at(skip).1)
    }
}
