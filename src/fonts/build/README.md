# Font-related GN rules and targets

This GN package generates Fuchsia font packages for font files, a font manifest,
and various other build-time font metadata.

## Visibility

`fonts.gni` defines GN templates that can be imported in other `.gn` and `.gni`
files.

The other files in this directory should not be imported outside of the
directory.