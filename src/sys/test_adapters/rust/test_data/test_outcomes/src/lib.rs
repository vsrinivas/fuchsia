// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    // The tests are sorted before they are run so I am prefixing a_, b_, and c_ to ensure that
    // they are run in this order so we can verify that the adapter doesn't crash after running
    // a failing test.
    #[test]
    fn a_passing_test() {
        println!("My only job is not to panic!()");
    }

    #[test]
    fn b_failing_test() {
        panic!("I'm supposed panic!()");
    }

    #[test]
    fn c_passing_test() {
        println!("My only job is not to panic!()");
    }
}
