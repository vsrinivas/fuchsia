// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    println!("Hippo: Hello World!");
}

#[cfg(test)]
mod tests {
    #[test]
    fn assert_0_is_0() {
        assert_eq!(0, 0);
    }
}
