// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub trait MacFmt {
    fn to_mac_str(&self) -> String;
}

impl MacFmt for [u8; 6] {
    fn to_mac_str(&self) -> String {
        format!(
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            self[0], self[1], self[2], self[3], self[4], self[5]
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn format_mac_str() {
        let mac: [u8; 6] = [0x00, 0x12, 0x48, 0x9a, 0xbc, 0xdf];
        assert_eq!(mac.to_mac_str(), "00:12:48:9a:bc:df");
    }
}
