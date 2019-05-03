// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

// NOTE: the contents of this file have been copied into documentation located at
//       //docs/development/languages/rust/testing.md. Please update the snippets in that file if
//       the contents below are changed.

fn main() {
    println!("Hello, world!");
}

#[cfg(test)]
mod tests {
    use fuchsia_async as fasync;

    #[fasync::run_until_stalled(test)]
    async fn my_test() {
        let some_future = async { 4 };
        assert_eq!(await!(some_future), 4);
    }
}
