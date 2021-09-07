// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as io;

/// Set of all rights that are valid to use with the conformance test harness.
const ALL_RIGHTS_FLAGS: u32 = io::OPEN_RIGHT_READABLE
    | io::OPEN_RIGHT_WRITABLE
    | io::OPEN_RIGHT_EXECUTABLE
    | io::OPEN_RIGHT_ADMIN;

/// Helper struct that encapsulates generation of valid/invalid sets of flags based on
/// which rights are supported by a particular node type.
pub struct Rights {
    rights: u32,
}

impl Rights {
    /// Creates a new Rights struct based on a bitset of OPEN_RIGHT_* flags OR'd together.
    pub fn new(rights: u32) -> Rights {
        assert!(rights & !ALL_RIGHTS_FLAGS == 0);
        Rights { rights }
    }

    /// Returns the bitset of all supported OPEN_RIGHT_* flags.
    pub fn all(&self) -> u32 {
        self.rights
    }

    /// Returns a vector of all valid flag combinations.
    pub fn valid_combos(&self) -> Vec<u32> {
        build_flag_combinations(0, self.rights)
    }

    /// Returns a vector of all valid flag combinations that include the specified `with_flags`.
    /// Will be empty if none of the requested rights are supported.
    pub fn valid_combos_with(&self, with_flags: u32) -> Vec<u32> {
        let mut flag_combos = self.valid_combos();
        flag_combos.retain(|&flags| (flags & with_flags) == with_flags);
        flag_combos
    }

    /// Returns a vector of all valid flag combinations that exclude the specified `without_flags`.
    /// Will be empty if none are supported.
    pub fn valid_combos_without(&self, without_flags: u32) -> Vec<u32> {
        let mut flag_combos = self.valid_combos();
        flag_combos.retain(|&flags| (flags & without_flags) == 0);
        flag_combos
    }
}

/// Returns a list of flag combinations to test. Returns a vector of the aggregate of
/// every constant flag and every combination of variable flags.
/// Ex. build_flag_combinations(100, 011) would return [100, 110, 101, 111]
/// for flags expressed as binary. We exclude the no rights case as that is an
/// invalid argument in most cases. Ex. build_flag_combinations(0, 011)
/// would return [010, 001, 011] without the 000 case.
pub fn build_flag_combinations(constant_flags: u32, variable_flags: u32) -> Vec<u32> {
    let mut vec = vec![constant_flags];

    for flag in split_flags(variable_flags) {
        for i in 0..vec.len() {
            vec.push(vec[i] | flag);
        }
    }
    vec.retain(|element| *element != 0);

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
    use super::*;

    #[test]
    fn test_build_flag_combinations() {
        let constant_flags = 0b100;
        let variable_flags = 0b011;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b100, 0b101, 0b110, 0b111];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_build_flag_combinations_without_empty_rights() {
        let constant_flags = 0;
        let variable_flags = 0b011;
        let generated_combinations = build_flag_combinations(constant_flags, variable_flags);
        let expected_result = [0b001, 0b010, 0b011];
        assert_eq!(generated_combinations, expected_result);
    }

    #[test]
    fn test_split_flags() {
        assert_eq!(split_flags(0), vec![]);
        assert_eq!(split_flags(0b001), vec![0b001]);
        assert_eq!(split_flags(0b101), vec![0b001, 0b100]);
        assert_eq!(split_flags(0b111), vec![0b001, 0b010, 0b100]);
    }

    #[test]
    fn test_rights_combos() {
        const TEST_RIGHTS: u32 =
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE;
        // We should get R, W, X, RW, RX, WX, RWX (7 in total).
        const EXPECTED_COMBOS: [u32; 7] = [
            io::OPEN_RIGHT_READABLE,
            io::OPEN_RIGHT_WRITABLE,
            io::OPEN_RIGHT_EXECUTABLE,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_EXECUTABLE,
            io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE,
        ];
        let rights = Rights::new(TEST_RIGHTS);
        assert_eq!(rights.all(), TEST_RIGHTS);

        // Test that all possible combinations are generated correctly.
        let all_combos = rights.valid_combos();
        assert_eq!(all_combos.len(), EXPECTED_COMBOS.len());
        for expected_rights in EXPECTED_COMBOS {
            assert!(all_combos.contains(&expected_rights));
        }

        // Test that combinations including READABLE are generated correctly.
        // We should get R, RW, RX, and RWX (4 in total).
        const EXPECTED_READABLE_COMBOS: [u32; 4] = [
            io::OPEN_RIGHT_READABLE,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_EXECUTABLE,
            io::OPEN_RIGHT_READABLE | io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE,
        ];
        let readable_combos = rights.valid_combos_with(io::OPEN_RIGHT_READABLE);
        assert_eq!(readable_combos.len(), EXPECTED_READABLE_COMBOS.len());
        for expected_rights in EXPECTED_READABLE_COMBOS {
            assert!(readable_combos.contains(&expected_rights));
        }

        // Test that combinations excluding READABLE are generated correctly.
        // We should get W, X, and WX (3 in total).
        const EXPECTED_NONREADABLE_COMBOS: [u32; 3] = [
            io::OPEN_RIGHT_WRITABLE,
            io::OPEN_RIGHT_EXECUTABLE,
            io::OPEN_RIGHT_WRITABLE | io::OPEN_RIGHT_EXECUTABLE,
        ];
        let nonreadable_combos = rights.valid_combos_without(io::OPEN_RIGHT_READABLE);
        assert_eq!(nonreadable_combos.len(), EXPECTED_NONREADABLE_COMBOS.len());
        for expected_rights in EXPECTED_NONREADABLE_COMBOS {
            assert!(nonreadable_combos.contains(&expected_rights));
        }
    }

    #[test]
    #[should_panic]
    fn test_rights_unsupported() {
        // Passing anything other than OPEN_RIGHT_* should fail.
        Rights::new(io::OPEN_FLAG_CREATE);
    }
}
