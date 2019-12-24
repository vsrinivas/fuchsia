// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    wlan_common::mac::{Aid, MAX_AID},
};

#[derive(Debug)]
pub struct Map {
    // bitmap representing the "claimed" association IDs; examples:
    // Claimed AIDs: [0] -- array 1st element is 1, remaining are zeroes
    // Claimed AIDs: [0, 1] -- array 1st element is 3, remaining are zeroes
    // Claimed AIDs: [64] -- array 2nd element is 1, remaining are zeroes
    //
    // Type u64 was chosen since it's the largest unsigned integer type that provides methods
    // like `count_zeroes` and `trailing_zeros`. Array has 32 elements since 64 * 32 provides
    // enough bits to cover the maximum number of clients (2008).
    aids: [u64; 32],
}

impl Map {
    const ELEM_BITS: u16 = 64;

    pub fn assign_aid(&mut self) -> Result<Aid, anyhow::Error> {
        for (i, bitmap) in self.aids.iter_mut().enumerate() {
            if bitmap.count_zeros() > 0 {
                let first_unset_bit_pos = (!*bitmap).trailing_zeros() as u16;
                let aid = first_unset_bit_pos + Map::ELEM_BITS * (i as u16);
                if aid <= MAX_AID {
                    *bitmap |= 1 << first_unset_bit_pos;
                    return Ok(aid);
                } else {
                    return Err(format_err!("no available association ID"));
                }
            }
        }
        // control flow should never reach here since once we reach the max AID, we should already
        // return in the above loop with an error (i.e., the last element in `aids` array is never
        // all 1's)
        panic!("unexpected error assigning association ID")
    }

    pub fn release_aid(&mut self, aid: Aid) {
        let index = (aid / Map::ELEM_BITS) as usize;
        self.aids[index] &= !(1 << (aid % Map::ELEM_BITS));
    }
}

impl Default for Map {
    fn default() -> Self {
        let mut map = Map { aids: [0u64; 32] };
        map.aids[0] = 1;
        map
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_map_never_assign_zero() {
        let mut aid_map: Map = Default::default();
        for i in 1..=2007u16 {
            assert_eq!(aid_map.assign_aid().unwrap(), i);
        }
        let result = aid_map.assign_aid();
        assert!(result.is_err());
        assert_eq!(format!("{}", result.unwrap_err()), "no available association ID");
    }

    #[test]
    fn test_map_no_available_assoc_id() {
        let mut aid_map: Map = Default::default();
        // Set all the bits in the first 31 elements to 1's (so 64 * 31 = 1984 aids claimed)
        for i in 0..31 {
            aid_map.aids[i] = u64::max_value();
        }
        // Set the remaining 24 aids in the last array positions
        for i in 0..24 {
            aid_map.aids[31] += 1 << i;
        }
        let result = aid_map.assign_aid();
        assert!(result.is_err());
        assert_eq!(format!("{}", result.unwrap_err()), "no available association ID");
    }

    #[test]
    fn test_map_mixed_case() {
        let mut aid_map: Map = Default::default();
        for i in 1..=1000u16 {
            assert_eq!(aid_map.assign_aid().unwrap(), i);
        }
        aid_map.release_aid(157);
        aid_map.release_aid(792);
        aid_map.release_aid(533);
        assert_eq!(aid_map.assign_aid().unwrap(), 157);
        assert_eq!(aid_map.assign_aid().unwrap(), 533);
        assert_eq!(aid_map.assign_aid().unwrap(), 792);

        for i in 1001..=2007u16 {
            assert_eq!(aid_map.assign_aid().unwrap(), i);
        }
        let result = aid_map.assign_aid();
        assert!(result.is_err());
        assert_eq!(format!("{}", result.unwrap_err()), "no available association ID");
        aid_map.release_aid(666);
        aid_map.release_aid(222);
        aid_map.release_aid(111);
        assert_eq!(aid_map.assign_aid().unwrap(), 111);
        assert_eq!(aid_map.assign_aid().unwrap(), 222);
        assert_eq!(aid_map.assign_aid().unwrap(), 666);
    }
}
