// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Initializes the given fields of a struct or union and returns the bytes of the
/// resulting object as a byte array.
///
/// `struct_with_union_into_bytes` is invoked like so:
///
/// ```rust,ignore
/// union Foo {
///     a: u8,
///     b: u16,
/// }
///
/// struct Bar {
///     a: Foo,
///     b: u8,
///     c: u16,
/// }
///
/// struct_with_union_into_bytes!(Bar { a.b: 1, b: 2, c: 3 })
/// ```
///
/// Each named field is initialized with a value whose type must implement
/// `zerocopy::AsBytes`. Any fields which are not explicitly initialized will be left as
/// all zeroes.
macro_rules! struct_with_union_into_bytes {
    ($ty:ident { $($($field:ident).*: $value:expr,)* }) => {{
        use std::mem::MaybeUninit;

        const BYTES: usize = std::mem::size_of::<$ty>();

        struct AlignedBytes {
            bytes: [u8; BYTES],
            _align: MaybeUninit<$ty>,
        }

        let mut bytes = AlignedBytes { bytes: [0; BYTES], _align: MaybeUninit::uninit() };

        $({
            // Evaluate `$value` once to make sure it has the same type
            // when passed to `type_check_as_bytes` as when assigned to
            // the field.
            let value = $value;
            if false {
                fn type_check_as_bytes<T: zerocopy::AsBytes>(_: T) {
                    unreachable!()
                }
                type_check_as_bytes(value);
            } else {
                // SAFETY: We only treat these zeroed bytes as a `$ty` for the purposes of
                // overwriting the given field. Thus, it's OK if a sequence of zeroes is
                // not a valid instance of `$ty` or if the sub-sequence of zeroes is not a
                // valid instance of the type of the field being overwritten. Note that we
                // use `std::ptr::write`, not normal field assignment, as the latter would
                // treat the current field value (all zeroes) as an initialized instance of
                // the field's type (in order to drop it), which would be unsound.
                //
                // Since we know from the preceding `if` branch that the type of `value` is
                // `AsBytes`, we know that no uninitialized bytes will be written to the
                // field. That, combined with the fact that the entire `bytes.bytes` is
                // initialized to zero, ensures that all bytes of `bytes.bytes` are
                // initialized, so we can safely return `bytes.bytes` as a byte array.
                unsafe {
                    std::ptr::write(&mut (&mut *(&mut bytes.bytes as *mut [u8; BYTES] as *mut $ty)).$($field).*, value);
                }
            }
        })*

        bytes.bytes
    }};
}

pub(crate) use struct_with_union_into_bytes;
