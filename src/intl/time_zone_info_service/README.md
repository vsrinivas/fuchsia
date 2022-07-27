# Time Zone Info Service

The Time Zone Info Service provides:

*   Properties of time zones — [TODO(fxbug.dev/81902)](https://fxbug.dev/81902)
*   Conversions between civil and absolute times

For the protocols, see
[`fuchsia.intl.TimeZones`](https://fuchsia.dev/reference/fidl/fuchsia.intl#TimeZones).

For the underlying implementation, see
[`//src/lib/time_zone_info](../../lib/intl/time_zone_info/).

## Usage

To integrate the service into a product target, add dependencies on
`"//src/intl_time_zone_info_service:pkg"` and
`"//src/intl/time_zone_info_service:core-shard"`. The latter will route
`fuchsia.intl.TimeZones` to `#session-manager` in `core.cml`.

### Alternative: intl

On space-constrained devices, consider using the `intl` component,
which exposes several different protocols from a single component containing a
single executable. See [`//src/intl/intl_services`](../intl_services/).
