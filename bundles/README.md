# Package bundles

This directory contains top-level bundles of packages.

# Special Handling Required!

Note:  These files require special handling, or GN will attempt to define all
targets (for all products/configurations) in the generated rules for Ninja.

This slows down both GN and Ninja, including the cost of Ninja starting for
every build.

Before these rules were introduced, the //bundles files contained targets from
multiple product configurations, resulting in all product configurations being
roughly as slow to generate as a maximal build configuration.

The tables below lay out how much extra work is done by GN and Ninja when extra
targets are being defined by GN.

Before:

| Configuration           | Targets Created | GN Files Read | GN (s) | ninja (s) | Rust crates |
|-------------------------|-----------------|---------------|--------|-----------|-------------|
| bringup.x64 (developer) | 229,977         | 6089          | 22     |           | 8888        |
| bringup.x64 (buildbot)  | 231,762         | 6116          | 22     |           | 8888        |
| core.x64 (developer)    | 230,329         | 6097          | 21     |           | 8923        |
| core.x64 (buildbot)     | 232,166         | 6127          | 23     | 6.5       | 8924        |

After:

| Configuration           | Targets Created | GN Files Read | GN time (s) | ninja (s) | Rust crates |
|-------------------------|-----------------|---------------|-------------|-----------|-------------|
| bringup.x64 (developer) | 165,270         | 4905          | 17          |           | 6874        |
| bringup.x64 (buildbot)  | 165,412         | 4939          | 19          |           | 6874        |
| core.x64 (developer)    | 185,195         | 5383          | 21          |           | 7493        |
| core.x64 (buildbot)     | 230,763         | 6116          | 23          |           | 8920        |



# The Rules

The rules for adding targets within these files is:

- There MUST be only one "entry-point" target in each `BUILD.gn` file, named
  as the folder it's in.

- This is the only target with "public" visibility in that file (ie
  `visibility = []`)

- All deps that are not in the `default_toolchain` MUST be qualified with the
  expected toolchain, UNLESS the BUILD.gn file has an `assert(is_host)` or
  similar guard to ensure that its own targets are only ever defined in the
  appropriate toolchain other than `default_toolchain`.

## Entry-point Targets

An "entry-point" target is target with public visibility, and is named after the
folder that the `BUILD.gn` file is in (e.g. //bundles/buildbot/core:core).

When referencing targets under `//bundles` from `fx set` or infra recipes, only
"entry_point" targets may be used.

Example: `fx set core.x64 --with //bundles/buildbot/core`


## Existing files don't (yet) follow the rules.

All new changes to these files MUST follow the above rules.

Where files don't follow the above rules, this should be treated as pre-existing
tech debt and should be cleaned up.

# OWNERS are restricted

The OWNERS for this area is now VERY tightly restricted to those will have read,
acknowledged, and agree to help make sure that these guidelines are followed for
future changes to these files.

Global OWNERS are not automatically owners here (through the use of `noparent`).
