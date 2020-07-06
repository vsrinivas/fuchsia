# FIDL Dangerous Identifiers Tests

The script `generate.py` reads a list of lower\_camel\_case formatted list of
identifiers from `dangerous_identifiers.txt`. Those are identifiers that we
suspect could trigger edge cases in the FIDL compilers, binding generators and
libraries.

The `generate.py` tool generates FIDL libraries that use various forms of each
identifier in various places that identifiers can appear. It generates a C++
program that compiles (though does not use) every generated FIDL library.
