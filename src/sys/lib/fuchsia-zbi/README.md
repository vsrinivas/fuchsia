Rust ZBI parsing library
==============================================================

This library allows parsing the Zircon boot image.

This is zero copy until ZBI items are requested. If only specific items are required, any pages
that only contain non-required items will be decommited to conserve memory.