// Copyright 2012-2015 The Rust Project Developers.
// Copyright 2017 The UNIC Project Developers.
//
// See the COPYRIGHT file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use unic_char_property::{tables::CharDataTableIter, PartialCharProperty};
use unic_char_range::CharRange;

/// A Unicode Block.
///
/// Blocks are contiguous range of code points, uniquely named, and have no overlaps.
///
/// All Assigned characters have a Block property value, but Reserved characters may or may not
/// have a Block.
#[derive(Clone, Copy, Debug)]
pub struct Block {
    /// The Character range of the Block.
    pub range: CharRange,

    /// The unique name of the Block.
    pub name: &'static str,

    // Private field to keep struct expandable.
    _priv: (),
}

impl Block {
    /// Find the character `Block` property value.
    pub fn of(ch: char) -> Option<Block> {
        match data::BLOCKS.find_with_range(ch) {
            None => None,
            Some((range, name)) => Some(Block {
                range,
                name,
                _priv: (),
            }),
        }
    }
}

impl PartialCharProperty for Block {
    fn of(ch: char) -> Option<Self> {
        Self::of(ch)
    }
}

/// Iterator for all assigned Unicode Blocks, except:
/// - U+D800..U+DB7F, High Surrogates
/// - U+DB80..U+DBFF, High Private Use Surrogates
/// - U+DC00..U+DFFF, Low Surrogates
#[derive(Debug)]
pub struct BlockIter<'a> {
    iter: CharDataTableIter<'a, &'static str>,
}

impl<'a> BlockIter<'a> {
    /// Create a new Block Iterator.
    pub fn new() -> BlockIter<'a> {
        BlockIter {
            iter: data::BLOCKS.iter(),
        }
    }
}

impl<'a> Default for BlockIter<'a> {
    fn default() -> Self {
        BlockIter::new()
    }
}

impl<'a> Iterator for BlockIter<'a> {
    type Item = Block;

    fn next(&mut self) -> Option<Block> {
        match self.iter.next() {
            None => None,
            Some((range, name)) => Some(Block {
                range,
                name,
                _priv: (),
            }),
        }
    }
}

mod data {
    use unic_char_property::tables::CharDataTable;
    pub const BLOCKS: CharDataTable<&str> = include!("../tables/blocks.rsv");
}
