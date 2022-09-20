The "license" package handles reading the license patterns, and applying those
patterns to a given license text to detect what type of license it is.

**patterns**: Holds all of the license pattern files. They are simple text files
with a "lic" file extension. The contents of each file are regex strings.

**allowlists**: JSON files that define which projects are allowed to have
license texts that match patterns in the restricted categories.

