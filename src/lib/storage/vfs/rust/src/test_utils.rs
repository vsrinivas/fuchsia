// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities used by tests in both file and directory modules.

pub mod assertions;
pub mod node;
pub mod run;

pub use run::{run_client, run_server_client, test_client, test_server_client, TestController};

/// Returns a list of flag combinations to test. Returns a vector of the aggregate of
/// every constant flag and every combination of variable flags. For example, calling
/// build_flag_combinations(100, 011) would return [100, 110, 101, 111] (in binary),
/// whereas build_flag_combinations(0, 011) would return [000, 001, 010, 011].
pub fn build_flag_combinations(constant_flags: u32, variable_flags: u32) -> Vec<u32> {
    let mut vec = vec![constant_flags];

    for flag in split_flags(variable_flags) {
        for i in 0..vec.len() {
            vec.push(vec[i] | flag);
        }
    }

    vec
}

/// Splits a bitset into a vector of its component bits. e.g. 1011 becomes [0001, 0010, 1000].
fn split_flags(mut flags: u32) -> Vec<u32> {
    let mut bits = vec![];
    while flags != 0 {
        // x & -x returns the lowest bit set in x. Add it to the vec and then unset that bit.
        let lowest_bit = flags & flags.wrapping_neg();
        bits.push(lowest_bit);
        flags ^= lowest_bit;
    }
    bits
}

#[cfg(test)]
mod tests {
    use super::{build_flag_combinations, split_flags};

    #[test]
    fn test_build_flag_combinations() {
        let constant_flags = 0b100;
        let variable_flags = 0b011;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b100, 0b101, 0b110, 0b111];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_build_flag_combinations_with_empty_constant_flags() {
        let constant_flags = 0;
        let variable_flags = 0b011;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b000, 0b001, 0b010, 0b011];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_build_flag_combinations_with_empty_variable_flags() {
        let constant_flags = 0b011;
        let variable_flags = 0;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b011];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_build_flag_combinations_with_empty_flags() {
        let constant_flags = 0;
        let variable_flags = 0;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_split_flags() {
        assert_eq!(split_flags(0), Vec::<u32>::new());
        assert_eq!(split_flags(0b001), vec![0b001]);
        assert_eq!(split_flags(0b101), vec![0b001, 0b100]);
        assert_eq!(split_flags(0b111), vec![0b001, 0b010, 0b100]);
    }
}
