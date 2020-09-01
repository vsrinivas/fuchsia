// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Print a + or - based on whether a given field is true.
pub fn is_set(field: bool) -> char {
    match field {
        true => '+',
        false => '-',
    }
}

// Turn bytes into a human readable value.
pub fn format_bytes(size: u64) -> String {
    let mut s = size;
    let mut divs: u8 = 0;
    while s / 1024 > 1 {
        s /= 1024;
        divs += 1;
    }

    format!(
        "{}{}",
        s.to_string(),
        match divs {
            0 => "B",
            1 => "K",
            2 => "M",
            3 => "G",
            4 => "T",
            _ => return size.to_string(),
        }
    )
}
