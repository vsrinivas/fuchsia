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

   Note: on Linux, `pkg-config` needs to be installed.

1. Do a build test. For example:

   ```
   fx set core.x64 && fx build
   ```
1. Run the following command to update crate-map:

   ```
   fx update-rustc-crate-map --output third_party/rust_crates/crate_map.json
   ```
   This command updates `crate_map.json` with information about the Rust crates
   available for each target (Fuchsia and host).
   Note that this step uses information from the build step - make sure that the
   build for the `third_party` folder has succeeded first before running this
   command.  This would be part of the `fx build` you are expected to run in the
   previous step.
1. Identify all the crates to be brought
   (see the diff in `//third_party/rust_crates/vendor/`).
   Do not submit the CL for code review. Get OSRB approval first.
   If there are any files in the source repository that are not included when
   vendored, make a note of that for the OSRB reviewer. For example, font files
   that are only used for testing but are excluded when the crate is vendored.
   If you are not a Google employee, you will need to ask a Google employee to
   do this part for you.
1. After the OSRB approval, upload the change for review to Gerrit.
1. Get `code-review+2` and merge the change into [third_party/rust_crates][3p-crates].

## Steps to update a third-party crate

Updating is very similar to adding a crate.

To update a third-party crate, do the following:

1. Start by bumping the version number of the crate in
   [`third_party/rust_crates/Cargo.toml`][3p-cargo-toml] and rerunning
   `fx update-rustc-third-party` as above.
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
[osrb-process]: https://docs.google.com/document/d/1X3eNvc4keQxOpbkGUiyYBMtr3ueEnVQCPW61FT96o_E/edit#heading=h.7mb7m2qs89th
[jiri-manifest]: https://fuchsia.googlesource.com/manifest/+/master/runtimes/rust "Jiri manifest"

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

