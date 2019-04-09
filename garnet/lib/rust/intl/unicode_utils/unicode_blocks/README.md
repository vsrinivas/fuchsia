# Unicode Block Generator

Builds a Rust crate called `unicode_blocks`. This contains an enum called `UnicodeBlockId`, which 
lists all* of the assigned [Unicode blocks](https://en.wikipedia.org/wiki/Unicode_block).

The source of the data is `unic_ucd_blocks::BlockIter`, which in turn is generated from official
Unicode data files.


---
\* All blocks, except for:
- U+D800..U+DB7F, High Surrogates
- U+DB80..U+DBFF, High Private Use Surrogates
- U+DC00..U+DFFF, Low Surrogates
 