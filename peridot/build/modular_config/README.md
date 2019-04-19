# Modular Configuration

Modular can be configured through a JSON file that is packaged through
modular_config().

modular_config.gni defines the build rule that does the following:

1. Validates the client provided configuration against
   modular_config_schema.json.
2. Adds the client provided configuration to basemgr's /config/data.
3. Adds basemgr.config to sysmgr's /config/data. This tells sysmgr to launch
   basemgr whenever a package created by modular_config() has been included in a
   device image.