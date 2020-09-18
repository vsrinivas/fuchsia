# Third-party Rust crates

## Overview

Fuchsia uses third-party Rust crates. They are placed in
[`//third-party/rust_crates/vendor`][3p-vendor].
This set of crates is based on the dependencies listed in
[`//third_party/rust_crates/Cargo.toml`][3p-cargo-toml].
If you don't find a crate that you want to use, you may bring that into Fuchsia.

To add a third-party crate, the steps are:

-  Calculate dependencies.
-  Get Open Source Review Board (OSRB) approval.
-  Upload the change for code review.

Pay attention to transitive dependencies: A third-party crate may depend on
other third-party crates. List all the new crates that end up with being
brought in, in the OSRB review. For OSRB, follow the instructions under the
"Process for 3rd Party Hosted Code" section in [this document][osrb-process].

Note: You need to get OSRB approval first before uploading a CL for review.

## Steps to add a third-party crate

1. Change directory to Fuchsia repo base directory
   (For example, `cd ~/fuchsia`).
1. Add an entry in
   [`third_party/rust_crates/Cargo.toml`][3p-cargo-toml]
   for the crate you want to add.
1. Run the following command to calculate the dependencies and download the
   crates:

   ```
   fx update-rustc-third-party
   ```
   This command downloads all crates listed in
   [`rust_crates/Cargo.toml`][3p-cargo-toml] as well as their dependencies,
   places them in the `vendor` directory, and updates `Cargo.toml` and
   `Cargo.lock`.

   You may need to provide additional configuration in a `[gn.package.<crate>]` section inside
   the Cargo.toml file. For crates that use a `build.rs` script this configuration replaces the
   script, which is (intentionally) unsupported by our build system. This configuration is used
   by cargo-gnaw, which generates the GN rules from the Cargo.toml file. See [cargo-gnaw's
   README][cargo-gnaw-readme] for more details.

   Note: on Linux, `pkg-config` needs to be installed.


1. Do a build test. For example:

   ```
   fx set core.x64 && fx build
   ```
1. Identify all the crates to be brought
   (see the diff in `//third_party/rust_crates/vendor/`).
   Do not submit the CL for code review. Get OSRB approval first.
   If there are any files in the source repository that are not included when
   vendored, make a note of that for the OSRB reviewer. For example, font files
   that are only used for testing but are excluded when the crate is vendored.
   If you are not a Google employee, you will need to ask a Google employee to
   do this part for you.

   Note: As part of OSRB review, you may be asked to import only a subset
   of the files in a third-party crate. See [Importing a subset of files in a crate](#importing-a-subset-of-files-in-a-crate) for how to do this.

1. After the OSRB approval, upload the change for review to Gerrit.
1. Get `code-review+2` and merge the change into [third_party/rust_crates][3p-crates].



## Steps to update a third-party crate

Updating is very similar to adding a crate.

To update a third-party crate, do the following:

1. Start by bumping the version number of the crate in
   [`third_party/rust_crates/Cargo.toml`][3p-cargo-toml] and rerunning
   `fx update-rustc-third-party` as above.

   You may need to update or provide additional configuration in `[gn.package.<crate>]` sections
   inside the Cargo.toml file. For crates that use a `build.rs` script this configuration
   replaces the script, which is (intentionally) unsupported by our build system. This
   configuration is used by cargo-gnaw, which generates the GN rules from the Cargo.toml file.
   See [cargo-gnaw's README][cargo-gnaw-readme] for more details.
1. Identify all new library dependencies brought in
   (see the diff in `//third_party/rust_crates/vendor/`).
   Again, do not submit the CL for code review until you've received OSRB
   approval for any new dependencies added.
1. After OSRB approval, upload the change for review to Gerrit and merge as
   above.

## Adding a new mirror

1. Request the addition of a mirror on *fuchsia.googlesource.com*.
1. Add the mirror to the [Jiri manifest][jiri-manifest] for the Rust runtime.
1. Add a patch section for the crate to the workspace.
1. Run the update script.

[3p-crates]: /third_party/rust_crates/
[3p-cargo-toml]: /third_party/rust_crates/Cargo.toml
[3p-vendor]: /third_party/rust_crates/vendor
[cargo-gnaw-readme]: /tools/cargo-gnaw/README.md
[osrb-process]: https://docs.google.com/document/d/1X3eNvc4keQxOpbkGUiyYBMtr3ueEnVQCPW61FT96o_E/edit#heading=h.7mb7m2qs89th
[jiri-manifest]: https://fuchsia.googlesource.com/manifest/+/master/runtimes/rust "Jiri manifest"

## Importing a subset of files in a crate

In some cases, you may want to import only a subset of files in a crate. For example, there may be an optional license in the
third-party repo that's incompatible with Fuchsia's license requirements. Here's [an example](https://fuchsia-review.googlesource.com/c/fuchsia/+/369174) OSRB review in which this happened.

To do this, you'll need to add the crate's files to `/third_party/rust_crates/tiny_mirrors`.

1. Follow the [instructions for adding a third-party crate](#steps-to-add-a-third-party-crate).
1. After running `fx update-rustc-third-party`, move the downloaded copy of your crate from `/third_party/rust_crates/vendor/<my_crate>` to `/third_party/rust_crates/tiny_mirrors`.
1. Make the changes you need to make to the imported files.
1. Add a line to the `[patch.crates-io]` section of `/third_party/rust_crates/Cargo.toml` to point to your new crate:
   ```
   [patch.crates-io]
   ...
   my_crate = { path = "tiny_mirrors/my_crate" }
   ...
   ```
1. Re-run `fx update-rustc-third-party` and `fx build`.

## Unicode crates

If the project requires importing a new third-party crate to handle
functionality related to Unicode and internationalization, prefer crates from
the [UNIC project](https://crates.io/crates/unic) when available.

### Grandfathered non-UNIC crates

The following non-UNIC crates are already vendored and are grandfathered, but we
will aim to migrate to UNIC equivalents when possible.

* unicode-bidi
* unicode-normalization
* unicode-segmentation
* unicode-width
* unicode-xid

We should encourage upstream dependencies to migrate to UNIC as well.

### Rationale for standardization

UNIC crates have distinct advantages over other crates:

* UNIC crates are developed in a single repo, with shared common code and a
  single version scheme.

  * Independently developed crates do not share a common release schedule,
    versioning scheme, or adherence to any particular version of the Unicode
    standard.

* UNIC crates are generated from a consistent set of Unicode data files.

  * Each of the independent crates uses an arbitrary version and subset of
    the data. For example, different crates might have different assumptions
    about whether a particular code point is assigned, what its properties
    are, etc.

* The UNIC project is aiming for comprehensive feature coverage, to be like
  [ICU](http://site.icu-project.org/) for Rust. If the project succeeds, our
  dependencies on unrelated Unicode crates should be reduced over time.

