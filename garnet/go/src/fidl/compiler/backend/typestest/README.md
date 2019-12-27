# Regenerating Golden Files

Ensure `fidlc` and `fidlgen` are built, for instance

    fx clean-build

Then run the `regen.sh` script, e.g.

    ./regen.sh

It is safe to run the script from anywhere. The script will also produce a
manifest of golden files, goldens.txt, to be read in by the build system in
order to copy them to the build directory for testing.
