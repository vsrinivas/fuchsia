// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the test section of the Rust Book
// https://doc.rust-lang.org/book/second-edition/ch11-03-test-organization.html

pub fn mult_two(a: i32) -> i32 {
    a * 2
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        assert_eq!(mult_two(2), 4);
    }
}
