# rust_icu: low-level rust language bindings for the ICU library

| Item | Description |
| ---- | ----------- |
| ICU 64/65/66 | [![Build Status `master`](https://travis-ci.org/google/rust_icu.svg?branch=master)](https://travis-ci.org/google/rust_icu) |
| Source | https://github.com/google/rust_icu |
| README | https://github.com/google/rust_icu/blob/master/README.md |
| Coverage | [View report](/coverage/report.md)
| Docs | https://github.com/google/rust_icu/blob/master/docs/README.md |

This is a library of low level native rust language bindings for the
International Components for Unicode (ICU) library for C (a.k.a. ICU4C).

See: http://icu-project.org for details about the ICU library.  The library
source can be viewed on Github at https://github.com/unicode-org/icu.

The latest version of this file is available at https://github.com/google/rust_icu.

> This is not an officially supported Google product.


## Why wrap ICU (vs. doing anything else)?

* The rust language
  [Internationalisation](https://www.arewewebyet.org/topics/i18n/) page
  confirms that ICU support in rust is spotty.

* Projects such as [Fuchsia OS](https://fuchsia.dev) already depend on ICU,
  and having rust bindings allow for an easy way to use Unicode algorithms
  without taking on more dependencies.

* Cooperation on the interface with projects such as the [I18N
  concept](https://github.com/i18n-concept/rust-discuss) could allow seamless
  transition to an all-rust implementation in the future.

# Structure of the repository

The repository is organized as a cargo workspace of rust crates.  Each crate
corresponds to the respective header in the ICU4C library's C API.  For
example, `rust_icu_uenum` implements the functionality that one would find in
the [uenum.h](http://www.icu-project.org/apiref/icu4c/uenum_8h.html) header
file.

Please consult the [coverage report](/coverage/report.md) for details about
function coverage in the headers given above.

| Crate | Description |
| ----- | ----------- |
| [rust_icu_sys](https://crates.io/crates/rust_icu_sys)| Low-level bindings code |
| [rust_icu_common](https://crates.io/crates/rust_icu_common)| Commonly used low-level wrappings of the bindings. |
| [rust_icu_ucal](https://crates.io/crates/rust_icu_ucal)| Implements `ucal.h` C API header from the ICU library. |
| [rust_icu_udat](https://crates.io/crates/rust_icu_udat)| Implements `udat.h` C API header from the ICU library. |
| [rust_icu_udata](https://crates.io/crates/rust_icu_udata)| Implements `udata.h` C API header from the ICU library. |
| [rust_icu_uenum](https://crates.io/crates/rust_icu_uenum)| Implements `uenum.h` C API header from the ICU library. Mainly `UEnumeration` and friends. |
| [rust_icu_uloc](https://crates.io/crates/rust_icu_uloc)| Implements `uloc.h` C API header from the ICU library. |
| [rust_icu_ustring](https://crates.io/crates/rust_icu_ustring)| Implements `ustring.h` C API header from the ICU library. |
| [rust_icu_utext](https://crates.io/crates/rust_icu_utext)| Implements `utext.h` C API header from the ICU library. |

# Limitations

The generated rust language binding methods of today limit the availability of
language bindings to the available C API.  The ICU library's C API (sometimes
referred to as ICU4C in the documentation) is distinct from the ICU C++ API.

The bindings offered by this library have somewhat limited applicability, which
means it may sometimes not work for you out of the box.  If you come across
such a case, feel free to [file a
bug](https://github.com/google/rust_icu/issues) for us to fix.  [Pull
requests](https://github.com/google/rust_icu/pulls) are welcome.

The limitations we know of today are as follows:

* *There isn't a guaranted feature parity.*  Some algorithms that are implemented
  in C++ don't have a C equivalent, and vice-versa.  This is usually not a
  problem if you are using the library from C++, since you are free to choose
  whichever API surface works for you.  But it is an issue for rust bindings,
  since we can only use the C API at the moment.

* *A C++ implementation of a new algorithm is not necessarily always reflected
  in the C API*, leading to feature disparity between the C and C++ API surfaces.
  See for example [this
  bug](https://unicode-org.atlassian.net/browse/ICU-20931) as an illustration.

* While using `icu_config` feature will likely allow you some freedom to
  auto-generate bindings for your own library version, we still need to keep
  a list of explicitly supported ICU versions to ensure that the wrappers are
  stable.

# Compatibility

The table below shows the support matrix that has been verified so far. Any
versions not mentioned explicitly have not been tested.  Feel free to test a
version and send a pull request to add to this matrix once you confirm the
functionality.  Each row is for a particular ICU library version.  The column
headers of columns 2 and onwards are features set combos.  The coverage
reflects the feature set and version points that we needed up to this point.
The version semver in each cell denotes the version point that was tested.

| ICU version | `default` | `renaming` | `renaming`, `icu_version_in_env`|
| ----------- | ------------------- | ---------------------- | ----- |
| 63.x        | ???                   | ???                      | ??? |
| 64.2        | 0.0.{3,4,5}             | ???                      | ??? |
| 65.1        | 0.0.5                 | 0.0.5                    | 0.0.5 |
| 66.0.1      | 0.0.5                 | ???                      | ??? |

> API versions that differ in the minor version number only should be
> compatible; but since it is time consuming to test all versions and
> relatively easy to keep only the last major edition of the library around, we
> keep only one minor version per library in the table, until need arises to do
> something else.

# Features

The `rust_icu` library is intended to be compiled with `cargo`,  with one of
several features enabled.  Compilation with `cargo` allows us to do some
library detection in a custom `build.rs` file in the `rust_icu_sys` library and
adapt the build process to your build environment.  However, since not every
development environment will use the same settings, we opted to offer certain
features (below) as configuration options.

While our intention is to keep the list of features below up to date with the
[actual list in
`Cargo.toml`](https://github.com/google/rust_icu/blob/master/Cargo.toml), the
list may periodically go out of date.

To use any of the features, you will need to activate the feature in *all* the
`rust_icu_*` crates that you intend to use.  Failing to do this will result in
confusing compilation end result.

| Feature | Default? | Description |
| ------- | -------- | ----------- |
| `bindgen` | Yes | If set, cargo will run `bindgen` to generate bindings based on the installed ICU library.  The program `icu-config` must be in $PATH for this to work. In the future there may be other approaches for auto-detecting libraries, such as via `pkg-config`. |
| `renaming` | Yes | If set, ICU bindings are generated with version numbers appended.  This is called "renaming" in ICU, and is normally needed only when linking against specific ICU version is required, for example to work around having to link different ICU versions.  See [the ICU documentation](http://userguide.icu-project.org/design) for a discussion of renaming. |
| `icu_config` | Yes | If set, the binary icu-config will be used to configure the library.  Turn this feature off if you do not want `build.rs` to try to autodetect the build environment.  You will want to skip this feature if your build environment configures ICU in a different way. |
| `icu_version_in_env` | No | If set, ICU bindings are made for the ICU version specified in the environment variable `RUST_ICU_MAJOR_VERSION_NUMBER`, which is made available to cargo at build time. See section below for details on how to use this feature. |

# Prerequisites

## Required

* `rust_icu` source code

  Clone with `git`:

  ```
  git clone https://github.com/google/rust_icu.git
  ```

* `rustup`

  Install from https://rustup.rs.  Used to set toolchain defaults.  This will
  install `cargo` as well.

* `rust` nightly toolchain

  Two options exist here:

  1. Set the global default: `rustup toolchain set nightly`

  2. Set the default toolchain just for `rust_icu`.  Go to the directory you
	 cloned `rust_icu` into, then issue:

  ```
  rustup override set nightly
  ```

* The ICU library development environmnet

  You will need access to the ICU libraries for the `rust_icu` bindings to link
  against.  Download and installation of ICU is out of scope of this document.
  Please read through the [ICU
  introduction](https://userguide.icu-project.org/intro) to learn how to build
  and install.

  Sometimes, the ICU library will be preinstalled on your system,
  or you can pull the library in from your package management program.
  However, this library won't necessarily be the one that you need to link into
  the program you are developing.  In short, it is your responsibility to have
  a developer version of ICU handy somewhere on your system.

  We have a [quickstart
  install](https://github.com/google/rust_icu#icu-installation-instructions)
  that *may* get you well on the way in case your environment happens to be
  configured very similarly to ours and you want to build ICU from source.

## Optional

* GNU Make, if you want to use the make-based build and test.

   Installing GNU Make is beyond the scope of this file. Please refer to your
   OS instructions for installation.

* `docker`, if you decide to use docker-based build and test.

   Installing `docker` is beyond the scope of this file, please see the [docker
   installation instructions](https://docs.docker.com/install/) for details.
   As installing `docker` is intrusive to the host machine, your company may
   have internal documentation on how to install `docker` properly.

* `icu-config` utility, if `icu_config` feature is used.

  You need to install the ICU library on your system, such that the binary
  `icu-config` is somewhere in your `$PATH`.  The build script will use it to
  discover the library settings and generate correct link scripts.  If you use
  the feature but `icu-config` is not found,

* `bindgen` utility, if `bindgen` feature is used.

  [bindgen user
  guide](https://rust-lang.github.io/rust-bindgen/command-line-usage.html) for
  instructions on how to install it.

* `rustfmt` utility, if `bindgen` feature is used.

  See https://github.com/rust-lang/rustfmt for instructions on how to install.

# Testing

There are a few options to run the test for `rust_icu`.

## Cargo

Building and testing using `cargo` is the 

The following tests should all build and pass.  Note that because the libraries
needed are in a custom location, we need to set `LD_LIBRARY_PATH` when running
the tests.

```bash
env LD_LIBRARY_PATH="$(icu-config --libdir)" cargo test
```

## GNU Make

The easiest way is to use GNU Make and run:

```
make test
```

You may want to use this method if you are working on `rust_icu`, have your
development environment all set up and would like a shorthand to run the tests.

## Docker-based

> See [optional dependencies section](#optional) above.

To run a hermetic build and test of the `rust_icu` source code, issue the
following command:

```bash
make docker-test
```

This will run docker-based build and test of the source code on your local
machine.  This is a good way to test that your code works with a specific
reference version of ICU.

# Prior art

There is plenty of prior art that has been considered:

* https://github.com/servo/rust-icu
* https://github.com/open-i18n/unic
* https://github.com/fullcontact/icu-sys
* https://github.com/rust-locale
* https://github.com/unicode-rs

The current state of things is that I'd like to do a few experiments on my own
first, then see if the work can be folded into any of the above efforts.

See also:

* https://github.com/rust-lang/rfcs/issues/797
* https://unicode-rs.github.io
* https://github.com/i18n-concept/rust-discuss

# Assumptions

There are a few competing approaches for ICU bindings.  However, it seems, at
least based on [information available in rust's RFC
repos](https://github.com/rust-lang/rfcs/issues/797), that the work on ICU
support in rust is still ongoing.

These are the assumptions made in the making of this library:

* We need a complete, reusable and painless ICU low-level library for rust.

  This, for example, means that we must rely on an external ICU library, and not
  lug the library itself with the binding code.  Such modularity allows the end
  user of the library to use an ICU library of their choice, and incorporate it
  in their respective systems.

* No ICU algorithms will be reimplemented as part of the work on this library.

  An ICU reimplementation will likely take thousands of engineer years to
  complete.  For an API that is as subtle and complex as ICU, I think that it
  is probably a better return on investment to maintain a single central
  implementation.

  Also, the existence of this library doesn't prevent reimplementation. If
  someone else wants to try their hand at reimplementing ICU, that's fine too.

* This library should serve as a low-level basis for a rust implementation.

  A low level ICU API may not be an appropriate seam for the end users. A
  rust-ful API should be layered on top of these bindings.  It will probably be
  a good idea to subdivide that functionality into crates, to match the
  expectations of rust developers.

  I'll gladly reuse the logical subdivision already made in some of the above
  mentioned projects.

* I'd like to explore ways to combine with existing implementations to build a
  complete ICU support for rust.

  Hopefully it will be possible to combine the good parts of all the rust
  bindings available today into a unified rust library. I am always available to
  discuss options.

  The only reason I started a separate effort instead of contributing to any of
  the projects listed in the "Prior Art" section is that I wanted to try what
  a generated library would look like in rust.

# Additional instructions

## ICU installation instructions

These instructions follow the "out-of-tree" build instructions from [the ICU
repository](https://github.com/unicode-org/icu/blob/master/icu4c/readme.html).

### Assumptions

The instructions below are not self-contained. They assume that:

* you have your system set up such that you can follow the ICU build
  instructions effectively.  This requires some upfront time investment.
* you can build ICU from source, and your project has access to ICU source.
* your setup is Linux, with some very specific settings that worked for me. You
  may be able to adapt them to work on yours.

### Compilation

```
mkdir -p $HOME/local
mkdir -p $HOME/tmp
cd $HOME/tmp
git clone https://github.com/unicode-org/icu.git
mkdir icu4c-build
cd icu4c-build
../icu/icu4c/source/runConfigureICU Linux \
  --prefix=$HOME/local \
  --enable-static
make
make install
make doc
```

If the compilation finishes with success, the directory `$HOME/local/bin` will
have the file `icu-config` which is necessary to discover the library
configuration.

You can also do a

```bash
make check
```

to run the unit tests.

If you add `$HOME/local/bin` to `$PATH`, or move `icu-config` to a directory
that is listed in your `$PATH` you should be all set to compile `rust_icu`.

## ICU rebuilding instructions

If you change the configuration of the ICU library with an intention to rebuild
the library from source you should probably add an intervening `make clean`
command.

Since the ICU build is not hermetic, this ensures there are no remnants of the
old compilation process sitting around in the build directory.  You need to do
this for example if you upgrade the major version of the ICU library.  If you
forget to do so, you may see unexpected errors while compiling ICU, or while
linking or running your programs.

## Compiling for a set version of ICU

### Assumptions

* You have selected the feature set `[renaming,icu_version_in_env]`o

**OR**:

* You have manually verified that the [compatibility
  matrix](https://github.com/google/rust_icu#compatibility) has a "Yes" for the
  ICU version and feature set you want to use.

The following is a tested example.

```bash
env LD_LIBRARY_PATH=$HOME/local/lib RUST_ICU_MAJOR_VERSION_NUMBER=65 bash -c `cargo test`
```

The following would be an as of yet *untested* example of compiling `rust_icu` against
a preexisting ICU version 66.

```bash
env LD_LIBRARY_PATH=$HOME/local/lib RUST_ICU_MAJOR_VERSION_NUMBER=66 bash -c `cargo test`
```

## Adding support for a new version of ICU.

In general, as long as `icu-config` approach is supported, it should be possible
to generate the library wrappers for newer versions of the ICU library, assuming
that the underlying C APIs do not diverge too much.

An approach that yielded easy support for ICU 65.1 consisted of the following
steps.  Below, `$RUST_ICU_SOURCE_DIR` is the directory where you extracted the
ICU source code.

* Download the new ICU version from source to `$RUST_ICU_SOURCE_DIR`.
* Build the ICU library following for example the [compilation](#compilation)
  steps above with the new version.
* Get the file `lib.rs` from the output directory
  `$RUST_ICU_SOURCE_DIR/target/debug/build/rust_icu_sys-...`, rename it to
  `lib_66.rs` (if working with ICU version 66, otherwise append the version you
  are using).
* Save the file to the directory `$RUST_ICU_SOURCE_DIR/rust_icu_sys/bindgen`,
  this is the directory that contains the pre-generated sources.

These files `lib_XX.rs` may need to be generated again if `build.rs` is changed
to include more features.

## Adding more bindings

When adding more ICU wrappers, make sure to do the following:

* Check `build.rs` to add appropriate lines into `bindgen_source_modules`, then
  `whitelist_types_regexes` and `whitelist_functions_regexes`.

