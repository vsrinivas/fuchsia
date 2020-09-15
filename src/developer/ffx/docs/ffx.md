# Welcome to FFX

FFX is a command line tool providing FIDL communication with multiple Fuchsia
devices via overnet. It is written in Rust and is made of three parts:

- [CLI](cli.md)
- [Daemon](daemon.md)
- [Remote Control Service (RCS)](rcs.md)

## [CLI](cli.md)

The Command Line Interface (CLI) provides the UX for FFX. It is responsible for:

- Parsing user parameters (CLI Parameters)
- Communicating with the daemon (starting it if necessary)
- Routing parsed parameters and requested FIDL proxies to the proper code path
  for execution

## [Daemon](daemon.md)

The daemon runs in the background on the host device and manages:

- Target discovery
- Target lifecycle management (flashing, provisioning, and package serving)
- Facilitating communication with target devices

## [Remote Control Service](rcs.md)

The remote control service runs on target devices and is responsible
for providing access to the FIDL services running on the target.
