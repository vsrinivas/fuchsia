// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// assert is like core::assert!, except it returns Error in the calling function.
#[macro_export]
macro_rules! assert {
    ($e:expr, $($arg:tt)+) => ({
        match (&$e) {
            e_val => {
                if !*e_val {
                    anyhow::ensure!(false, format!("assertion failed ({}:{}): {}", file!(), line!(), format!($($arg)+)));
                }
            }
        }
    });
    ($e:expr) => ({ $crate::assert!($e, ""); });
    ($e:expr,) => ({ $crate::assert!($e, ""); });
}

/// assert_eq is like core::assert_eq!, except it returns Error in the calling function.
#[macro_export]
macro_rules! assert_eq {
    ($left:expr, $right:expr, $($arg:tt)+) => ({
        match ((&$left), (&$right)) {
            (left_val, right_val) => {
                if *left_val != *right_val {
                    anyhow::ensure!(false, format!("assertion failed ({}:{}): {}\nassert_eq!({}, {})\n{:?}\n{:?}", file!(), line!(), format!($($arg)+), stringify!($left), stringify!($right), left_val, right_val));
                }
            }
        }
    });
    ($left:expr, $right:expr,) => ({ $crate::assert_eq!($left, $right) });
    ($left:expr, $right:expr) => ({ $crate::assert_eq!($left, $right, ""); });
}

/// assert_ne is like core::assert_ne!, except it returns Error in the calling function.
#[macro_export]
macro_rules! assert_ne {
    ($left:expr, $right:expr, $($arg:tt)+) => ({
        match ((&$left), (&$right)) {
            (left_val, right_val) => {
                if *left_val == *right_val {
                    anyhow::ensure!(false, format!("assertion failed ({}:{}): {}\nassert_ne!({}, {})\n{:?}\n{:?}", file!(), line!(), format!($($arg)+), stringify!($left), stringify!($right), left_val, right_val));
                }
            }
        }
    });
    ($left:expr, $right:expr,) => ({ $crate::assert_ne!($left, $right) });
    ($left:expr, $right:expr) => ({ $crate::assert_ne!($left, $right, ""); });
}

#[cfg(test)]
mod test {
    use core::assert as asrt;

    #[test]
    fn test_assert_eq() {
        let res1 = (|| Ok(assert_eq!(false, true, "a message")))();
        asrt!(matches!(res1, Err(x) if x.to_string().contains("a message")));

        let res2 = (|| Ok(assert_eq!(true, true)))();
        asrt!(res2.is_ok());
    }

    #[test]
    fn test_assert_ne() {
        let res1 = (|| Ok(assert_ne!(true, true, "a message")))();
        asrt!(matches!(res1, Err(x) if x.to_string().contains("a message")));

        let res2 = (|| Ok(assert_ne!(false, true)))();
        asrt!(res2.is_ok());
    }

    #[test]
    fn test_assert() {
        let res = (|| Ok(assert!(true)))();
        asrt!(res.is_ok());

        let res = (|| Ok(assert!(false)))();
        asrt!(res.is_err());
    }
}
