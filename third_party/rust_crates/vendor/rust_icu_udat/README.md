# rust_icu: low-level rust language bindings for the ICU library

| Item | Description |
| ---- | ----------- |
| ICU 64..67 | [![Test status](https://github.com/google/rust_icu/workflows/Test/badge.svg)](https://github.com/google/rust_icu/workflows/Test/badge.svg) |
| Source | https://github.com/google/rust_icu |
| README | https://github.com/google/rust_icu/blob/master/README.md |
| Coverage | [View report](/coverage/report.md)
| Docs | https://docs.rs/crate/rust_icu |

This is a library of low level native rust language bindings for the
International Components for Unicode (ICU) library for C (a.k.a. ICU4C).

If you just want quick instructions to contribute, see the [quickstart
guide](https://github.com/google/rust_icu#icu-installation-instructions).

See: http://icu-project.org for details about the ICU library.  The library
source can be viewed on Github at https://github.com/unicode-org/icu.

The latest version of this file is available at https://github.com/google/rust_icu.

> This is not an officially supported Google product.

## Why wrap ICU (vs. doing anything else)?

* The rust language
  [Internationalisation](https://www.arewewebyet.org/topics/i18n/) page
  confirms that ICU support in rust is spotty, so having a functional wrapper
  helps advance the state of the art.

* Projects such as [Fuchsia OS](https://fuchsia.dev) already depend on ICU,
  and having rust bindings allow for an easy way to use Unicode algorithms
  without taking on more dependencies.

* Cooperation on the interface with projects such as the [I18N
  concept](https://github.com/unicode-org/rust-discuss) could allow seamless
  transition to an all-rust implementation in the future.

# Structure of the repository

The repository is organized as a cargo workspace of rust crates.  Each crate
corresponds to the respective header in the ICU4C library's C API.  Please
consult the [coverage report](/coverage/report.md) for details about function
coverage in the headers.

| Crate | Description |
| ----- | ----------- |
| [rust_icu](https://crates.io/crates/rust_icu)| Top-level crate.  Include this if you just want to have all the functionality available for use. |
| [rust_icu_common](https://crates.io/crates/rust_icu_common)| Commonly used low-level wrappings of the bindings. |
| [rust_icu_intl](https://crates.io/crates/rust_icu_intl)| Implements ECMA 402 recommendation APIs. |
| [rust_icu_sys](https://crates.io/crates/rust_icu_sys)| Low-level bindings code |
| [rust_icu_ucal](https://crates.io/crates/rust_icu_ucal)| ICU Calendar. Implements [`ucal.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ucal_8h.html) C API header from the ICU library. |
| [rust_icu_ucol](https://crates.io/crates/rust_icu_ucol)| Collation support. Implements [`ucol.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ucol_8h.html) C API header from the ICU library. |
| [rust_icu_udat](https://crates.io/crates/rust_icu_udat)| ICU date and time. Implements [`udat.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/udat_8h.html) C API header from the ICU library. |
| [rust_icu_udata](https://crates.io/crates/rust_icu_udata)| ICU binary data. Implements [`udata.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/udata_8h.html) C API header from the ICU library. |
| [rust_icu_uenum](https://crates.io/crates/rust_icu_uenum)| ICU enumerations. Implements [`uenum.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/uenum_8h.html) C API header from the ICU library. Mainly `UEnumeration` and friends. |
| [rust_icu_uformattable](https://crates.io/crates/rust_icu_uformattable)| Locale-sensitive list formatting support. Implements [`uformattable.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/uformattable_8h.html) C API header from the ICU library. Since 0.3.1. |
| [rust_icu_ulistformatter](https://crates.io/crates/rust_icu_ulistformatter)| Locale-sensitive list formatting support. Implements [`ulistformatter.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ulistformatter_8h.html) C API header from the ICU library. |
| [rust_icu_uloc](https://crates.io/crates/rust_icu_uloc)| Locale support. Implements [`uloc.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/uloc_8h.html) C API header from the ICU library. |
| [rust_icu_umsg](https://crates.io/crates/rust_icu_umsg)| MessageFormat support. Implements [`umsg.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/umsg_8h.html) C API header from the ICU library. |
| [rust_icu_unum](https://crates.io/crates/rust_icu_unum)| Number formatting support. Implements [`unum.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/unum_8h.html) C API header from the ICU library. |
| [rust_icu_unumberformatter](https://crates.io/crates/rust_icu_unumberformatter)| Number formatting support (modern). Implements [`unumberformatter.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/unumberformatter_8h.html) C API header from the ICU library. |
| [rust_icu_upluralrules](https://crates.io/crates/rust_icu_upluralrules)| Locale-sensitive plural rules support. Implements [`upluralrules.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/upluralrules_8h.html) C API header from the ICU library. |
| [rust_icu_ustring](https://crates.io/crates/rust_icu_ustring)| ICU strings. Implements [`ustring.h`]() C API header from the ICU library. |
| [rust_icu_utext](https://crates.io/crates/rust_icu_utext)| Text operations. Implements [`utext.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/utext_8h.html) C API header from the ICU library. |
| [rust_icu_utrans](https://crates.io/crates/rust_icu_utrans)| Transliteration support. Implements [`utrans.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/utrans_8h.html) C API header from the ICU library. |

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
versions not mentioned explicitly have not been tested. No guarantees are made
for those versions.

## Feature sets

For an explanation of features, see the [Features section](#features) below.

* 1: default
* 2: "renaming"
* 3: "icu_version_in_env"

## Compatibility matrix

Each cell in the table shows which feature set combination has been tested for
this particular ICU library and `rust_icu` version combination.

| `rust_icu` version | ICU 63.x | ICU 64.2 | ICU 65.1  | ICU 66.0.1 | ICU 67.1 |
| ------------------ | -------- | -------- | --------- | ---------- | -------- |
| 0.1                | 1        | 1        | 1;2;2+3   | 1          | 1        |
| 0.2                | 1;2;2+3  | 1;2;2+3  | 1;2;2+3   | 1;2;2+3    | 1;2;2+3  |
| 0.3                | 1;2;2+3  | 1;2;2+3  | 1;2;2+3   | 1;2;2+3    | 1;2;2+3  |

> Prior to a 1.0.0 release, API versions that only differ in the patch version
> number (0.x.**y**) only should be compatible.

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
| `use-bindgen` | Yes | If set, cargo will run `bindgen` to generate bindings based on the installed ICU library.  The program `icu-config` must be in $PATH for this to work. In the future there may be other approaches for auto-detecting libraries, such as via `pkg-config`. |
| `renaming` | Yes | If set, ICU bindings are generated with version numbers appended.  This is called "renaming" in ICU, and is normally needed only when linking against specific ICU version is required, for example to work around having to link different ICU versions.  See [the ICU documentation](http://userguide.icu-project.org/design) for a discussion of renaming. **This feature MUST be used when `bindgen` is NOT used.** |
| `icu_config` | Yes | If set, the binary icu-config will be used to configure the library.  Turn this feature off if you do not want `build.rs` to try to autodetect the build environment.  You will want to skip this feature if your build environment configures ICU in a different way. **This feature is only meaningful when `bindgen` feature is used; otherwise it has no effect.** |
| `icu_version_in_env` | No | If set, ICU bindings are made for the ICU version specified in the environment variable `RUST_ICU_MAJOR_VERSION_NUMBER`, which is made available to cargo at build time. See section below for details on how to use this feature. **This feature is only meaningful when `bindgen` feature is NOT used; otherwise it has no effect.** |

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

Building and testing using `cargo` is the canonical way of building and testing
rust code.

In the case of the `rust_icu` library you may find that your
system's default ICU development package is ancient, in which case you will
need to build your own ICU4C library (see below for that). That will make
it necessary to pass in `PKG_CONFIG_PATH` and `LD_LIBRARY_PATH` environment
variables to help the bulid code locate and use the library you built, instead
of the system default.

The following tests should all build and pass.  Note that because the libraries
needed are in a custom location, we need to set `LD_LIBRARY_PATH` when running
the tests, as well as `PKG_CONFIG_PATH`.

If you find that you are able to use your system's default ICU installation,
you can safely omit the two libraries.

```bash
env PKG_CONFIG_PATH="$HOME/local/lib/pkgconfig" \
    LD_LIBRARY_PATH="$HOME/local/lib" \
        bash -c 'cargo test'
```

If you think that the above approach is too much of a hassle, consider trying
out the [Docker-based approach](#docker-based).

## GNU Make

If you happen to like the GNU way of doing things, you may appreciate 
the GNU Make approach.

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
env PKG_CONFIG_PATH="$HOME/local/lib/pkgconfig" \
    LD_LIBRARY_PATH="$HOME/local/lib" \
    RUST_ICU_MAJOR_VERSION_NUMBER=65 \
	    bash -c 'cargo test'
```

The following would be an as of yet *untested* example of compiling `rust_icu` against
a preexisting ICU version 66.

```bash
env PKG_CONFIG_PATH="$HOME/local/lib/pkgconfig" \
    LD_LIBRARY_PATH="$HOME/local/lib" \
    RUST_ICU_MAJOR_VERSION_NUMBER=66 \
	    bash -c 'cargo test'
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

* Check `rust_icu_sys/build.rs` and `rust_icu_sys/bindgen/run_bindgen.sh` to add
  appropriate lines into `BINDGEN_SOURCE_MODULES`, then
  `BINDGEN_ALLOWLIST_FUNCTIONS` and `BINDGEN_ALLOWLIST_TYPES`.

## Testing with a specific feature set turned on

Here's an example of running a docker test on ICU 67, with features
`icu_version_in_env` and `renaming` turned on instead of the default.  Note that
the parameters are mostly passed into the container that runs `docker-test` via
environment variables.

```bash
make DOCKER_TEST_ENV=rust_icu_testenv-67 \
  RUST_ICU_MAJOR_VERSION_NUMBER=67 \
  DOCKER_TEST_CARGO_TEST_ARGS='--no-default-features --features icu_version_in_env,renaming' \
  docker-test
```

Some clarification:

* The environment variable `RUST_ICU_MAJOR_VERSION_NUMBER` is used for the
 feature `icu_version_in_env` to instruct `cargo` to use the file
 `rust_icu_sys/bindgen/lib_67.rs` as a prebuilt bindgen source file instead of
 trying to generate one on the fly.
* The environment variable `DOCKER_TEST_CARGO_TEST_ARGS` is used to pass the
  command line arguments to the `cargo test` which is used in the docker container.
  The environment is passed in verbatim to `cargo test` without quoting, so separate
  words in the environment end up being separate args to `cargo test`.
* The environment variable `DOCKER_TEST_ENV` is the base name of the Docker container
  used to run the test in.  The container `rust_icu_testenv-67` is a container image
  that contains preinstalled environment with a compiled version of ICU 67.

## Refreshing static bindgen files

Requires docker.

Run `make static-bindgen` periodically, to refresh the statically generated
bindgen files (named `lib_XX.rs`, where `XX` is an ICU version, e.g. 67) in the
directory [`rust_icu_sys/bindgen`](./rust_icu_sys/bindgen) which are used when
`bindgen` features are turned off.

Invoking this make target will modify the local checkout with the newer versions
of the files `lib_XX.rs`.  Make a pull request and check them in.

For more information on why this is needed, see the [bindgen
README.md](rust_icu_sys/bindgen/README.md).

