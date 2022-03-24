// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub fn clear_buffer(buffer: &mut [u8], clear_color: [u8; 4]) {
    for color in buffer.chunks_exact_mut(4) {
        color.copy_from_slice(&clear_color);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const RED: [u8; 4] = [0xFF, 0x00, 0x00, 0xFF];
    const GREEN: [u8; 4] = [0x00, 0xFF, 0x00, 0xFF];

    #[test]
    fn clear_to_red() {
        let mut buffer = [GREEN; 3].concat();
        clear_buffer(&mut buffer, RED);
        assert_eq!(buffer, [RED, RED, RED].concat());
    }
}
