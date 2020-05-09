# FIDL scripts

This directory contains scripts used by FIDL contributors. When writing a
script, consider saving it here if there is any chance it could be reused,
including being used as a reference for future scripts. All scripts must be
documented in this file.

## find_nullable_union_fields.py

This script analyzes all `.fidl` files in the fuchsia repository and reports
occurrences of nullable fields in unions.

