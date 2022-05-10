## ACPI host test tables.

These tables are used by the ACPI host tests. They exercise particular corner cases in our ACPI code.

Each subdirectory represents a test case, except for 'fake-fadt' which contains a FADT used by other tests.

### Generating the table bytecode

First, install `iasl` via `sudo apt install acpica-tools` or similar.
Then, run `update-bytecode.py` in this directory.
