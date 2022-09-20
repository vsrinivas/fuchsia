# Time Zone Version Parrot

The TZ Version Parrot showcases loading ICU time zone data (tzdata) from various
sources, then simply announces the release version (e.g. `2015d`, `2019a`) of
the current tzdata.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

If you do not already have one running, start a package server so the example
components can be resolved from your device:

```bash
$ fx serve
```

## Running

The time zone data loading examples are implemented as test cases. To run one of
the test components defined here, provide the package name to `fx test`:

-  **C++**

    ```bash
    $ fx test tz-version-parrot-cpp
    ```

-  **Rust**

    ```bash
    $ fx test tz-version-parrot-rust
    ```

## Use cases

This example showcases the following cases:

*  Initializing ICU without tzdata files
*  Initializing ICU with tzdata from the platform

You can find the platform's tzdata `.res` files in `${icu_tzres_path}`. The
[`tzdata-provider`](/src/intl/tzdata_provider) component provides these files
via directory capabilities.

(The value of `icu_root` is usually `//third_party/icu`.)
