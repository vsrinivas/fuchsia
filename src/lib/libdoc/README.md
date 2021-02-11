Doclib parses the documentation in FIDL files and tries to validate it as much as possible.

It ensures that the documentation syntax and grammar is consistent. It checks that all references
are corrects.

# Source file

Doclib doesn't parse directly the FIDL files. Instead, it uses the JSON IR generated from the FIDL
files.
