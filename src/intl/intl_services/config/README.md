# Configuration bits for `intl_services`

The configuration files available:

- `sysmgr_config.json`: the configuration submitted to `sysmgr`.  The syntax of this file
  is described in [Sysmgr configuration documentation](/src/sys/sysmgr/sysmgr-configuration.md)

- `sysmgr_config_small.json`: the configuration submitted to `sysmgr` for systems with small storage
  space.  In such systems, some of the functionality that is normally served by separate components
  has been fused into a single component.  This reduces the storage overhead.
