// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use lazy_static::lazy_static;
use regex::Regex;
use std::env;
use std::fs::File;
use std::io::Write;
use unic_ucd_block::{Block, BlockIter};

lazy_static! {
    static ref REGEX_NON_IDENTIFIER: Regex = Regex::new(r"[^A-Za-z0-9]+").unwrap();
}

const PREAMBLE: &str = r#"// Generated using //garnet/bin/fonts/unicode_blocks:generate_unicode_blocks. Do not edit.

mod blocks {
    use lazy_static::lazy_static;
    use unic_char_range::CharRange;
    use unic_ucd_block::{Block, BlockIter};
"#;

const BLOCKS_VEC: &str = r#"
    lazy_static! {
        static ref BLOCKS: Vec<Block> = BlockIter::new().collect();
    }

"#;

pub fn generate_unicode_blocks_enum() -> String {
    let mut s = String::new();
    s.push_str(
        r#"
    /// Compile-time identifiers for a [Unicode Block](unic_ucd_block::Block).
    ///
    /// Use `.block()` to get a reference to the actual `Block` instance.
    #[derive(Debug)]
    pub enum UnicodeBlockId {
"#,
    );
    for block in BlockIter::new() {
        s.push_str(&format!("        {},\n", block.enum_id()));
    }
    s.push_str("    }\n");
    s
}

pub fn generate_impl() -> String {
    let mut s = String::new();
    s.push_str(
        r#"
    impl UnicodeBlockId {
        /// Get the block's name.
        pub fn name(&self) -> &str {
            self.block().name
        }

        /// Get the block's `CharRange`.
        pub fn char_range(&self) -> CharRange {
            self.block().range
        }

        /// Get a reference to the `Block` that this ID represents.
        pub fn block(&self) -> &'static Block {
            &BLOCKS[self.idx()]
        }

        fn idx(&self) -> usize {
            match self {
"#,
    );
    let mut idx: usize = 0;
    for block in BlockIter::new() {
        s.push_str(&format!("                UnicodeBlockId::{} => {},\n", block.enum_id(), idx));
        idx += 1;
    }
    s.push_str("            }\n"); // match
    s.push_str("        }\n"); // fn idx
    s.push_str("    }\n"); // impl UnicodeBlockId

    s.push_str(
        r#"
    impl From<&UnicodeBlockId> for CharRange {
        fn from(source: &UnicodeBlockId) -> Self {
          source.char_range()
        }
    }
"#,
    );

    s
}

const EPILOGUE: &str = r#"
}

pub use blocks::UnicodeBlockId;

"#;

trait BlockExt {
    fn enum_id(&self) -> String;
}

impl BlockExt for Block {
    fn enum_id(&self) -> String {
        let segments: Vec<&str> = REGEX_NON_IDENTIFIER.split(self.name).collect();
        (&segments).join("")
    }
}

fn main() -> Result<(), Error> {
    let args: Vec<String> = env::args().collect();
    let mut out_file = File::create(args[1].clone())?;
    write!(out_file, "{}", PREAMBLE)?;
    write!(out_file, "{}", BLOCKS_VEC)?;
    write!(out_file, "{}", generate_unicode_blocks_enum())?;
    write!(out_file, "{}", generate_impl())?;
    write!(out_file, "{}", EPILOGUE)?;
    Ok(())
}
