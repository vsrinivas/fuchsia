# Time Zone Info Service

The Time Zone Info Service provides:

*   Properties of time zones — [TODO(fxbug.dev/81902)](https://fxbug.dev/81902)
*   Conversions between civil and absolute times

For the protocols, see
[`fuchsia.intl.TimeZones`](https://fuchsia.dev/reference/fidl/fuchsia.intl#TimeZones).

For the underlying implementation, see
[`//src/lib/time_zone_info](../../lib/intl/time_zone_info/).

## Usage

To integrate the service into a product target, add a dependency on
`"//src/intl/time_zone_info_service:pkg"`. That target will also pull in the
`sysmgr` configuration needed to map `fuchsia.intl.TimeZones` to this service
component.
