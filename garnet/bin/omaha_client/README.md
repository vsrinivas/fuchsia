# Omaha Client

Omaha Client is a service that checks for new update with Omaha server.

## Requirement

Omaha Client requires app id and version to run.

The default app id is `fuchsia-test:no-update`, it can be overridden with
`--args omaha_app_id='"appid"'` appended to your `fx set` arguments.

The default version for local build is a date string, it needs to be set to a
version number in [Omaha format](https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#version-numbers),
for example: `--args build_info_version='"0.0.0.1"'`