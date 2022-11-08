// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    serde::{Deserialize, Serialize},
    std::hash::Hash,
};

/// A unique identifier for a node in a hash tree.
/// A label is defined by its value, length, and bits_per_level.
/// Just |value| is not sufficient, because the root's length is 0 and thus the
/// understood value is also 0. Because children are 0-indexed, then all nodes
/// on the left-most branch of the root will have the value 0.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, Deserialize, Serialize)]
pub struct Label {
    value: u64,
    length: u8,
}

impl Label {
    pub fn leaf_label(value: u64, length: u8) -> Self {
        Self { value, length }
    }

    pub fn value(&self) -> u64 {
        self.value
    }

    /// Returns a String that represents this Label as a directory name.
    pub fn as_dir_name(&self) -> String {
        // TODO(arkay): Double check if a particular format is required.
        format!("{}_{}", self.value, self.length)
    }
}

/// Generates Labels for a HashTree.
/// Labels are bitstring representations of position of the node in the tree.
#[derive(Debug, Deserialize, Serialize)]
pub struct BitstringLabelGenerator {
    height: u8,
    children_per_node: u8,
    leaf_length: u8,
    bits_per_level: u8,
}

/// Calculate the amount of bits required to hold n children.
/// e.g.: 4 children can fit in 2 bits (00, 01, 10, 11)
fn bits_to_hold_children(n: u8) -> Result<u8, Error> {
    match n {
        0 => Err(anyhow!("cannot calculate bits to hold 0 children")),
        1 => Ok(1),
        n => Ok(8 - ((n - 1).leading_zeros() as u8)),
    }
}

impl BitstringLabelGenerator {
    /// Construct a new BitstringLabelGenerator.
    pub fn new(height: u8, children_per_node: u8) -> Result<Self, Error> {
        if height < 1 || children_per_node < 2 {
            return Err(anyhow!(
                "invalid height {} or children_per_node {}",
                height,
                children_per_node
            ));
        }

        // Calculate the number of bits required to hold |children_per_node|
        // and the total bitstring length of a leaf based on that and height.
        let bits_per_level = bits_to_hold_children(children_per_node)?;
        // Since the root is a 0-length bitstring, we subtract 1 from height to
        // calculate the leaf length.
        // TODO(arkay): Double check that the |height| and |fan_out| we pass to
        // CR50 matches how we interpret height and children_per_node here.
        // This is necessary because the HashTree implementation in chromium does
        // not actually hold the leaf nodes, whereas HashTree does, and thus
        // the heights of the tree might be interpreted differently.
        let leaf_length = height * bits_per_level;

        Ok(Self { leaf_length, bits_per_level, children_per_node, height })
    }

    /// Return the Label representing the root of the tree.
    pub fn root(&self) -> Label {
        Label { value: 0, length: 0 }
    }

    /// Given a parent label, extend it to a child label.
    pub fn child_label(&self, parent: &Label, child_index: u8) -> Result<Label, Error> {
        // Child indices are 0-indexed.
        if child_index > self.children_per_node - 1 {
            return Err(anyhow!("child_index out of range"));
        }

        let child_length = parent.length + self.bits_per_level;
        if child_length > self.leaf_length {
            return Err(anyhow!("cannot make a child label of a leaf label"));
        }
        let child_value = (parent.value << self.bits_per_level) | child_index as u64;

        Ok(Label { value: child_value, length: child_length })
    }
}

#[cfg(test)]
pub const BAD_LABEL: Label = Label { value: 255, length: 0 };
#[cfg(test)]
pub const TEST_LABEL: Label = Label { value: 4, length: 12 };

#[cfg(test)]
mod test {
    use super::*;

    macro_rules! assert_label_value_and_length {
        ($label: expr, $val: expr, $len: expr) => {
            assert_eq!($label.value, $val);
            assert_eq!($label.length, $len);
        };
    }

    #[test]
    fn test_bad_inputs() {
        assert!(BitstringLabelGenerator::new(0, 4).is_err());
        assert!(BitstringLabelGenerator::new(4, 0).is_err());
    }

    #[test]
    fn test_bitstring_label_generator_binary_tree() -> Result<(), Error> {
        let label_gen = BitstringLabelGenerator::new(4, 2)?;
        assert_eq!(label_gen.bits_per_level, 1);
        assert_eq!(label_gen.leaf_length, 4);
        let root = label_gen.root();
        // Root node
        assert_label_value_and_length!(root, 0, 0);
        // Level 2 nodes
        let child_0 = label_gen.child_label(&root, 0)?;
        assert_label_value_and_length!(child_0, 0, 1);
        let child_1 = label_gen.child_label(&root, 1)?;
        assert_label_value_and_length!(child_1, 1, 1);
        // Level 3 nodes
        let child_00 = label_gen.child_label(&child_0, 0)?;
        assert_label_value_and_length!(child_00, 0, 2);
        let child_01 = label_gen.child_label(&child_0, 1)?;
        assert_label_value_and_length!(child_01, 1, 2);
        let child_10 = label_gen.child_label(&child_1, 0)?;
        assert_label_value_and_length!(child_10, 2, 2);
        let child_11 = label_gen.child_label(&child_1, 1)?;
        assert_label_value_and_length!(child_11, 3, 2);
        // Leaf nodes
        let child_000 = label_gen.child_label(&child_00, 0)?;
        assert_label_value_and_length!(child_000, 0, 3);
        let child_001 = label_gen.child_label(&child_00, 1)?;
        assert_label_value_and_length!(child_001, 1, 3);
        let child_010 = label_gen.child_label(&child_01, 0)?;
        assert_label_value_and_length!(child_010, 2, 3);
        let child_011 = label_gen.child_label(&child_01, 1)?;
        assert_label_value_and_length!(child_011, 3, 3);
        let child_100 = label_gen.child_label(&child_10, 0)?;
        assert_label_value_and_length!(child_100, 4, 3);
        let child_101 = label_gen.child_label(&child_10, 1)?;
        assert_label_value_and_length!(child_101, 5, 3);
        let child_110 = label_gen.child_label(&child_11, 0)?;
        assert_label_value_and_length!(child_110, 6, 3);
        let child_111 = label_gen.child_label(&child_11, 1)?;
        assert_label_value_and_length!(child_111, 7, 3);
        Ok(())
    }

    #[test]
    fn test_bitstring_label_generator_3_ary_tree() -> Result<(), Error> {
        let label_gen = BitstringLabelGenerator::new(3, 3)?;
        assert_eq!(label_gen.bits_per_level, 2);
        assert_eq!(label_gen.leaf_length, 6);
        let root = label_gen.root();
        // Root node
        assert_label_value_and_length!(root, 0, 0);
        // Level 2 nodes
        let child_00 = label_gen.child_label(&root, 0)?;
        assert_label_value_and_length!(child_00, 0, 2);
        let child_01 = label_gen.child_label(&root, 1)?;
        assert_label_value_and_length!(child_01, 1, 2);
        let child_10 = label_gen.child_label(&root, 2)?;
        assert_label_value_and_length!(child_10, 2, 2);
        // Leaf nodes
        let child_0000 = label_gen.child_label(&child_00, 0)?;
        assert_label_value_and_length!(child_0000, 0, 4);
        let child_0001 = label_gen.child_label(&child_00, 1)?;
        assert_label_value_and_length!(child_0001, 1, 4);
        let child_0010 = label_gen.child_label(&child_00, 2)?;
        assert_label_value_and_length!(child_0010, 2, 4);
        let child_0100 = label_gen.child_label(&child_01, 0)?;
        assert_label_value_and_length!(child_0100, 4, 4);
        let child_0101 = label_gen.child_label(&child_01, 1)?;
        assert_label_value_and_length!(child_0101, 5, 4);
        let child_0110 = label_gen.child_label(&child_01, 2)?;
        assert_label_value_and_length!(child_0110, 6, 4);
        let child_1000 = label_gen.child_label(&child_10, 0)?;
        assert_label_value_and_length!(child_1000, 8, 4);
        let child_1001 = label_gen.child_label(&child_10, 1)?;
        assert_label_value_and_length!(child_1001, 9, 4);
        let child_1010 = label_gen.child_label(&child_10, 2)?;
        assert_label_value_and_length!(child_1010, 10, 4);
        Ok(())
    }

    #[test]
    fn test_bitstring_label_generator_4_ary_tree() -> Result<(), Error> {
        let label_gen = BitstringLabelGenerator::new(3, 4)?;
        assert_eq!(label_gen.bits_per_level, 2);
        assert_eq!(label_gen.leaf_length, 6);
        let root = label_gen.root();
        // Root node
        assert_label_value_and_length!(root, 0, 0);
        // Level 2 nodes
        let child_00 = label_gen.child_label(&root, 0)?;
        assert_label_value_and_length!(child_00, 0, 2);
        let child_01 = label_gen.child_label(&root, 1)?;
        assert_label_value_and_length!(child_01, 1, 2);
        let child_10 = label_gen.child_label(&root, 2)?;
        assert_label_value_and_length!(child_10, 2, 2);
        let child_11 = label_gen.child_label(&root, 3)?;
        assert_label_value_and_length!(child_11, 3, 2);
        // Leaf nodes
        let child_0000 = label_gen.child_label(&child_00, 0)?;
        assert_label_value_and_length!(child_0000, 0, 4);
        let child_0001 = label_gen.child_label(&child_00, 1)?;
        assert_label_value_and_length!(child_0001, 1, 4);
        let child_0010 = label_gen.child_label(&child_00, 2)?;
        assert_label_value_and_length!(child_0010, 2, 4);
        let child_0011 = label_gen.child_label(&child_00, 3)?;
        assert_label_value_and_length!(child_0011, 3, 4);
        let child_0100 = label_gen.child_label(&child_01, 0)?;
        assert_label_value_and_length!(child_0100, 4, 4);
        let child_0101 = label_gen.child_label(&child_01, 1)?;
        assert_label_value_and_length!(child_0101, 5, 4);
        let child_0110 = label_gen.child_label(&child_01, 2)?;
        assert_label_value_and_length!(child_0110, 6, 4);
        let child_0111 = label_gen.child_label(&child_01, 3)?;
        assert_label_value_and_length!(child_0111, 7, 4);
        let child_1000 = label_gen.child_label(&child_10, 0)?;
        assert_label_value_and_length!(child_1000, 8, 4);
        let child_1001 = label_gen.child_label(&child_10, 1)?;
        assert_label_value_and_length!(child_1001, 9, 4);
        let child_1010 = label_gen.child_label(&child_10, 2)?;
        assert_label_value_and_length!(child_1010, 10, 4);
        let child_1011 = label_gen.child_label(&child_10, 3)?;
        assert_label_value_and_length!(child_1011, 11, 4);
        let child_1100 = label_gen.child_label(&child_11, 0)?;
        assert_label_value_and_length!(child_1100, 12, 4);
        let child_1101 = label_gen.child_label(&child_11, 1)?;
        assert_label_value_and_length!(child_1101, 13, 4);
        let child_1110 = label_gen.child_label(&child_11, 2)?;
        assert_label_value_and_length!(child_1110, 14, 4);
        let child_1111 = label_gen.child_label(&child_11, 3)?;
        assert_label_value_and_length!(child_1111, 15, 4);
        Ok(())
    }

    #[test]
    fn test_child_index_out_of_range() -> Result<(), Error> {
        let label_gen = BitstringLabelGenerator::new(4, 2)?;
        let root = label_gen.root();
        assert_label_value_and_length!(root, 0, 0);
        assert!(label_gen.child_label(&root, 2).is_err());
        Ok(())
    }

    #[test]
    fn test_child_of_leaf_label() -> Result<(), Error> {
        let label_gen = BitstringLabelGenerator::new(1, 2)?;
        let root = label_gen.root();
        assert_label_value_and_length!(root, 0, 0);
        let child_0 = label_gen.child_label(&root, 0)?;
        assert_label_value_and_length!(child_0, 0, 1);
        assert!(label_gen.child_label(&child_0, 0).is_err());
        Ok(())
    }

    #[test]
    fn test_bits_to_hold_children() -> Result<(), Error> {
        assert!(bits_to_hold_children(0).is_err());
        assert_eq!(bits_to_hold_children(1)?, 1);
        assert_eq!(bits_to_hold_children(2)?, 1);
        assert_eq!(bits_to_hold_children(3)?, 2);
        assert_eq!(bits_to_hold_children(4)?, 2);
        assert_eq!(bits_to_hold_children(5)?, 3);
        assert_eq!(bits_to_hold_children(8)?, 3);
        assert_eq!(bits_to_hold_children(9)?, 4);
        assert_eq!(bits_to_hold_children(16)?, 4);
        assert_eq!(bits_to_hold_children(17)?, 5);
        assert_eq!(bits_to_hold_children(32)?, 5);
        assert_eq!(bits_to_hold_children(33)?, 6);
        assert_eq!(bits_to_hold_children(64)?, 6);
        assert_eq!(bits_to_hold_children(65)?, 7);
        assert_eq!(bits_to_hold_children(128)?, 7);
        assert_eq!(bits_to_hold_children(129)?, 8);
        assert_eq!(bits_to_hold_children(255)?, 8);
        Ok(())
    }

    #[test]
    fn into_dir_name() {
        let label = Label { value: 4, length: 10 };
        assert_eq!(label.as_dir_name(), "4_10");
    }
}
