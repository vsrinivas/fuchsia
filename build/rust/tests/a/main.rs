// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

mod other;

fn main() {
    println!("{}", other::foo());
    println!("{}", b::RequiredB::default().0 .0);
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let x = 3.14;
        assert!(x > 0.0);
    }
}
