Modular
=======

Modular is the application platform of Fuchsia.

It provides a post-API programming model that allows applications to cooperate
in a shared context without the need to call each other's APIs directly.

## Prerequisities

1. Follow instructions in [fuchsia](https://fuchsia.googlesource.com/fuchsia/+/HEAD/README.md) for getting the source, setup, build and running Fuchsia.
1. Follow instructions in [ledger](https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md) to setup the Ledger and all its dependencies including netstack and minfs.

## Running

On the Fuchsia command line you can start an example application flow like this:

```sh
@ bootstrap device_runner --user-shell=dummy_user_shell
```

A single application can be run using the `dev_user_shell`, for example:

```sh
@ bootstrap device_runner --user-shell=dev_user_shell --user-shell-args=--root-module=example_recipe,--root-link={"http://schema.domokit.org/counter":5\,"http://schema.org/sender":"dev_user_shell"}
```

The flags `--user-shell` and `--user-shell-args` are read by `device_runner`.
The value of `--user-shell` is the application run as user shell. The value of
`--user-shell-args` is a comma separated list of arguments passed to the user
shell application. In this example, these arguments are in turn more flags.
Commas inside the value of such arguments are escaped by backslashes. The value
of `--root-module` selects the module to run. The value of `--root-link` is a
JSON representation of the initial data the module is started with.

## Contents

 - [docs](docs) - documentation
 - [services](services) - fidl API
 - [src](src) - implementation
