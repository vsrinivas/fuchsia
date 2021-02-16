# ffx

```
Usage: ffx [-c <config>] [-e <env>] [-t <target>] [-T <timeout>] [-v] [<command>] [<args>]

Fuchsia's developer tool

```

## Options:

```
  -c, --config      override default configuration
  -e, --env         override default environment settings
  -t, --target      apply operations across single or multiple targets
  -T, --timeout     override default proxy timeout
  -v, --verbose     use verbose output
  --help            display usage information

```

## Commands:

```
  component         Discover and manage components
  config            View and switch default and user configurations
  daemon            Interact with/control the ffx daemon
  doctor            Run common checks for the ffx tool and host environment
  vdl               Start and manage Fuchsia emulators
  overnet           Interact with the Overnet mesh
  package           Create and publish Fuchsia packages
  platform          Manage platform build prerequisites
  scrutiny          Audit the security of Fuchsia
  sdk               Modify or query the installed SDKs
  self-test         Execute the ffx self-test (e2e) suite
  target            Interact with a target device or emulator
  trace             Interact with the tracing subsystem
  version           Print out ffx tool and daemon versions
  debug             Start a debugging session.

See 'ffx help <command>' for more information on a specific command.
```
