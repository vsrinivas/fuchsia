# FIDL scripts

This directory contains scripts used by FIDL contributors. When writing a
script, consider saving it here if there is any chance it could be reused,
including being used as a reference for future scripts. All scripts must be
documented in this file.

## find_nullable_union_fields.py

This script analyzes all `.fidl` files in the fuchsia repository and reports
occurrences of nullable fields in unions.

## fix_rust_exhaustive_tables.sh

This script (1) changes FIDL table initializers to use the functional update
syntax `MyTable { /* ... */, ..MyTable::empty() }`, and (2) changes patterns to
use `MyTable { /* ... */, .. }`. It does this by parsing build errors with awk
and then processing them with a Rust program executed using
[rust-script](https://rust-script.org/).

## simple_unions.py, single_use.py, ir.py
These are scripts that are useful for analyzing the corpus of FIDL libraries in
the Fuchsia tree. They operate on the `.fidl.json` IR in the out directory. They
should be run with `fx exec SCRIPTNAME` so that they can find the out directory.

The library `ir.py` finds and parses the IR files and makes them available in a
somewhat pythonic interface.