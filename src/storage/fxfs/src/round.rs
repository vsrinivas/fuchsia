// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Round `offset` up to next multiple of `block_size`.
/// This function will fail if rounding up leads to an integer overflow.
///
/// (Note that unstable rust is currently adding the same function
/// `{integer}::checked_next_multiple_of()` behind the "int_roundings" feature.)
pub fn round_up<T: Into<u64>>(offset: u64, block_size: T) -> Option<u64> {
    let block_size = block_size.into();
    Some(round_down(offset.checked_add(block_size - 1)?, block_size))
}

/// Round `offset` down to the previous multiple of `block_size`.
pub fn round_down<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    let block_size = block_size.into();
    offset - offset % block_size
}

/// Returns how many items of a given size are needed to contain a value.
///
/// TODO(https://github.com/rust-lang/rust/issues/88581): Replace with `{integer}::div_ceil()` when
/// `int_roundings` is available.
pub fn how_many<T: Into<u64>>(value: u64, item_size: T) -> u64 {
    let item_size = item_size.into();
    let items = value / item_size;
    let remainder = value % item_size;
    if remainder != 0 {
        items + 1
    } else {
        items
    }
}

#[cfg(test)]
mod tests {
    use crate::round::how_many;

    #[test]
    fn test_how_many() {
        assert_eq!(how_many(0, 3u64), 0);
        assert_eq!(how_many(1, 3u64), 1);
        assert_eq!(how_many(2, 3u64), 1);
        assert_eq!(how_many(3, 3u64), 1);
        assert_eq!(how_many(4, 3u64), 2);
        assert_eq!(how_many(5, 3u64), 2);
        assert_eq!(how_many(6, 3u64), 2);

        assert_eq!(how_many(17u64, 3u8), 6u64);

        assert_eq!(how_many(u64::MAX, 4096u32), u64::MAX / 4096 + 1);
    }
}
