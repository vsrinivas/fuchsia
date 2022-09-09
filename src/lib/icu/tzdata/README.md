# tzdata packages and templates

This directory contains the GN templates that provide data files for packages
and underlying programs that need to load IANA timezone data as provided by
the [ICU](https://home.unicode.org) library.

## `icu_tzdata_resource`

NOTE: Most components should not use this rule to consume tzdata. Instead,
use the [tzdata directory capability][icu-data] to get the latest tzdata
provided by the platform.

This rule adds the files `metaZones.res`, `timezoneTypes.res`, and
`zoneinfo64.res` to a package under the path 
`/pkg/data/tzdata/icu/{data_version}/{format}/`.

There will also be a file at `/pkg/data/tzdata/revision.txt` containing the
time zone database revision ID, e.g. `2019c`.

## `icu_tzdata_config_data`

NOTE: This rule is deprecated. Use the [tzdata directory capability][icu-data]
instead.

This rule provides the tzdata files `metaZones.res`, `timezoneTypes.res`,
and `zoneinfo64.res` to a component via [config-data] under the path
`/config/data/tzdata/icu/{data_version}/{format}/`.

There will also be a file at `/config/data/tzdata/revision.txt` containing the
time zone database revision ID, e.g. `2019c`.

## Common parameters

Common parameters for GN templates:

-  `data_version` (optional) \
   [string] The ICU version number of the time zone data. \
   Currently supported: { `"44"` } \
   Default: `"44"`

-  `format` (optional) \
   [string] The format name. \
   Currently supported: { `"le"` }. le = "Little-endian" \
   Default: `"le"`

In the future, these templates may support newer versions or different formats
(e.g. `txt`).

Example:

```
icu_tzdata_config_data("icu_tzdata_for_web_runner") {
  for_pkg = "web_runner"
  data_version = "44"
  format = "le"
}
```

Additionally, `testonly` packages can be declared like so:

```
icu_tzdata_config_data("icu_tzdata_for_web_runner_test") {
  for_pkg = "web_runner_test"
  data_version = "44"
  format = "le"
  testonly = true
}
```

`testonly = true` injects an additional file `/config/data/FUCHSIA_IN_TREE_TEST`
or `/pkg/data/FUCHSIA_IN_TREE_TEST`.

[config-data]: /docs/development/components/configuration/config_data.md
[icu-data]: /docs/development/internationalization/icu_data.md
