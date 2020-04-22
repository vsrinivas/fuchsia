// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod my_tests {
    #[test]
    fn sample_test_one() {
        println!("My only job is not to panic!()");
    }

    #[test]
    fn passing_test() {
        println!("My only job is not to panic!()");
    }

    #[test]
    fn failing_test() {
        panic!("I'm supposed panic!()");
    }

    #[test]
    fn sample_test_two() {
        println!("My only job is not to panic!()");
    }
}
