# fidlgen_banjo tests

Data files in `fidl/` and golden files in e.g. `c/` are symbolic links to files
under `//src/tools/devices/banjo/tests`. This makes it so that `fidlgen_banjo` is
tested against the exact same expectations as the tool it aims to replace.
