# Configuration bits for `intl_services`

The configuration files available:

- `sysmgr_config.json`: the configuration submitted to `sysmgr`.  The syntax of this file
  is described in [Sysmgr configuration documentation](/src/sys/sysmgr/sysmgr-configuration.md)

- `sysmgr_config_small.json`: the configuration submitted to `sysmgr` for systems with small storage
  space.  In such systems, some of the functionality that is normally served by separate components
  has been fused into a single component.  This reduces the storage overhead.

- `sysmgr_config_small_timezones.json`: the configuration submitted to sysmgr for systems that
need to serve `fuchsia.intl.TimeZones` from the `intl_services` binary rather than from the
time zones binary.  This happens mostly due to space constrains. `f.i.TimeZones` is normally
served by [`time-zone-info-service`][tzis].

[tzis]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/intl/time_zone_info_service