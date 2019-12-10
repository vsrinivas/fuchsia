# Time Zone Version Parrot

The TZ Version Parrot simply announces the release version (e.g. `2015d`,
`2019a`) of the ICU time zone data that it loaded.

## Instructions

* Display the version of time zone from the `icudtl.dat` monolithic file:
  ```shell
  fx shell "run fuchsia-pkg://fuchsia.com/tz_version_parrot#meta/tz_version_parrot.cmx"
  ```
  
  Sample output:
  ```text
  Squawk! TZ version (from  icudtl.dat) is:
  2019a
  ```

* Display the version of time zone from time zone `.res` files, if available:
  ```shell
  fx shell "run fuchsia-pkg://fuchsia.com/tz_version_parrot#meta/tz_version_parrot_with_tzdata.cmx"
  ```

## Config data

In order for `tz_version_parrot_with_tzdata.cmx`, which attempts to load `.res`
files, to work, the product being built must include those resource files using
a `icu_tzdata_config_data` GN rule that places them in the expected location
(see `kTzdataDir` in [`main.cc`](main.cc)).

The actual `.res` files live in
[`//third_party/icu/tzres`](../../../third_party/icu/tzres).

## Testing

Run
```shell
fx run-test tz_version_parrot_test
```
