// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    println!("{}, world!", greeting());
    eprintln!("{}, world!", greeting());
}

fn greeting() -> String {
    return String::from("Hello");
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(true, true);
    }
}

#[cfg(test)]
mod hello_tests {
    use crate::greeting;
    use fuchsia_async as fasync;

    #[fasync::run_until_stalled(test)]
    async fn my_test() {
        let some_future = async { 4 };
        assert_eq!(some_future.await, 4);
    }

    #[test]
    fn greeting_test() {
        let expected = String::from("Hello");
        assert_eq!(greeting(), expected)
    }
}
