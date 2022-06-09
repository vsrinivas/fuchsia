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
    while s / 1024 >= 1 {
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

const SLICE_SIZE: usize = 16;
pub struct Hexdumper<'a> {
    pub bytes: &'a [u8],
    pub show_header: bool,
    pub show_ascii: bool,
    pub offset: Option<u64>,
}

impl std::fmt::Display for Hexdumper<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let size = std::cmp::min(self.bytes.len(), SLICE_SIZE);
        // Weep for those who do not use monospace terminal fonts.
        if self.show_header {
            write!(f, "        ")?;
            for col in 0..size {
                write!(f, " {:1x} ", col)?;
                if col == 7 {
                    write!(f, " ")?;
                }
            }
            writeln!(f)?;
        }

        let start_addr = self.offset.unwrap_or(0) as usize;
        for (addr, slice) in self.bytes.chunks(SLICE_SIZE).enumerate() {
            write!(f, "  {:04x}: ", start_addr + (addr * SLICE_SIZE))?;
            for (i, byte) in slice.iter().enumerate() {
                write!(f, "{:02x} ", byte)?;
                if i == 7 {
                    write!(f, " ")?;
                }
            }
            if self.show_ascii {
                write!(f, " ")?;
                for byte in slice {
                    if byte.is_ascii_graphic() {
                        write!(f, "{}", *byte as char)?;
                    } else {
                        write!(f, ".")?;
                    }
                }
            }
            writeln!(f)?;
        }
        Ok(())
    }
}
