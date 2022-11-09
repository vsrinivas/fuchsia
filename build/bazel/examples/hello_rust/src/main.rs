// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub fn say_hello() -> &'static str {
    "Hello, Rust!"
}

fn main() {
    println!("{}", say_hello())
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_hello() {
        assert_eq!("Hello, Rust!", say_hello());
    }
}
