# The ffx tool

Future Fuchsia experience (FFX) provides a service oriented interface to many
common development and integration workflow operations that users may wish to
perform against one or more Fuchsia target devices.

It is both a service runtime, and a collection of utilities, and it is
intended for both users and infrastructure integrators alike.

## Getting started

- [Using ffx command line tool](/docs/development/tools/ffx/getting-started.md)
- [Developing for ffx](/docs/development/tools/ffx/development/plugins.md)

## [CLI](/docs/development/tools/ffx/architecture/cli.md)

The Command Line Interface (CLI) provides the UX for FFX. It is responsible for:

- Parsing user parameters (CLI Parameters)
- Communicating with the daemon (starting it if necessary)
- Routing parsed parameters and requested FIDL proxies to the proper code path
  for execution

## Daemon

The daemon runs in the background on the host device and manages:

- Target discovery
- Target lifecycle management (flashing, provisioning, and package serving)
- Facilitating communication with target devices

## Remote control service

The remote control service runs on target devices and is responsible
for providing access to the FIDL services running on the target.