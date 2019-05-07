// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Traits for use with the rust client library for cobalt.

/// `AsEventCodes` is any type that can be converted into a `Vec<u32>` for the purposes of storing
/// in a `fidl_fuchsia_cobalt::CobaltEvent`.
pub trait AsEventCodes {
    /// Converts the source type into a `Vec<u32>` of event codes.
    fn as_event_codes(&self) -> Vec<u32>;
}

impl AsEventCodes for () {
    fn as_event_codes(&self) -> Vec<u32> {
        Vec::new()
    }
}

impl AsEventCodes for u32 {
    fn as_event_codes(&self) -> Vec<u32> {
        vec![*self]
    }
}

impl AsEventCodes for Vec<u32> {
    fn as_event_codes(&self) -> Vec<u32> {
        self.to_owned()
    }
}

impl AsEventCodes for [u32] {
    fn as_event_codes(&self) -> Vec<u32> {
        Vec::from(self)
    }
}

macro_rules! array_impls {
    ($($N:expr)+) => {
        $(
            impl AsEventCodes for [u32; $N] {
                fn as_event_codes(&self) -> Vec<u32> {
                    self[..].as_event_codes()
                }
            }
        )+
    }
}

array_impls! {0 1 2 3 4 5 6}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_as_event_codes() {
        assert_eq!(().as_event_codes(), vec![]);
        assert_eq!([].as_event_codes(), vec![]);
        assert_eq!(1.as_event_codes(), vec![1]);
        assert_eq!([1].as_event_codes(), vec![1]);
        assert_eq!(vec![1].as_event_codes(), vec![1]);
        assert_eq!([1, 2].as_event_codes(), vec![1, 2]);
        assert_eq!(vec![1, 2].as_event_codes(), vec![1, 2]);
        assert_eq!([1, 2, 3].as_event_codes(), vec![1, 2, 3]);
        assert_eq!(vec![1, 2, 3].as_event_codes(), vec![1, 2, 3]);
        assert_eq!([1, 2, 3, 4].as_event_codes(), vec![1, 2, 3, 4]);
        assert_eq!(vec![1, 2, 3, 4].as_event_codes(), vec![1, 2, 3, 4]);
        assert_eq!([1, 2, 3, 4, 5].as_event_codes(), vec![1, 2, 3, 4, 5]);
        assert_eq!(vec![1, 2, 3, 4, 5].as_event_codes(), vec![1, 2, 3, 4, 5]);
    }

}
