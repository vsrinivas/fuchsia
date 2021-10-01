# Time Zone Version Parrot

The TZ Version Parrot showcases loading ICU time zone data (TZData) from various
sources, then simply announces the release version (e.g. `2015d`, `2019a`) of
the current TZData.

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

*  Initializing ICU with the system default TZData
*  Initializing ICU with TZData from the build configuration (`icu_tzdata_config_data`)
*  Initializing ICU with TZData from the local package (`resource`)

You can find the platform's TZDara `.res` files in
[`//third_party/icu/tzres`](/third_party/icu/tzres).
