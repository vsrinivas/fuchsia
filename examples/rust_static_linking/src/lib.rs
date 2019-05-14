// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[link(name = "static", kind = "static")]
extern "C" {
    pub fn returns_two() -> u32;
}

#[cfg(test)]
mod tests {
    use super::returns_two;

    #[test]
    fn linking_works() {
        assert_eq!(unsafe { returns_two() }, 2);
    }
}
