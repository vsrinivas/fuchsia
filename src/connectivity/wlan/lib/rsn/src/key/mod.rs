// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod exchange;
pub mod gtk;
pub mod igtk;
pub mod ptk;
use crypto::util::fixed_time_eq;

pub trait Tk {
    fn tk(&self) -> &[u8];

    fn eq_tk(&self, other: &impl Tk) -> bool {
        // Use fixed_time_eq to protect keyframe replays from timing attacks.
        fixed_time_eq(self.tk(), other.tk())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    #[derive(Clone)]
    struct DummyTk {
        tk_field: Vec<u8>,
    }
    impl Tk for DummyTk {
        fn tk(&self) -> &[u8] {
            &self.tk_field[..]
        }
    }

    #[test]
    fn test_eq_tk() {
        let dummy_tk_a =
            DummyTk { tk_field: Vec::from_hex("aaaaaaaa").expect("could not make Vec") };
        let dummy_tk_equivalent_a =
            DummyTk { tk_field: Vec::from_hex("aaaaaaaa").expect("could not make Vec") };
        let dummy_tk_b =
            DummyTk { tk_field: Vec::from_hex("bbbbbbbb").expect("could not make Vec") };

        assert!(dummy_tk_a.eq_tk(&dummy_tk_equivalent_a));
        assert!(!dummy_tk_a.eq_tk(&dummy_tk_b));
    }
}
