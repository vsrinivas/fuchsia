Modular
=======

Modular provides a framework for ephemerally downloaded applications which may
have been implemented in different programming languages to run in a shared
context and provide a composed experience to the user. The framework is also
responsible for application lifecycle, resource management, view hierarchy,
authentication, etc.

## Prerequisities

1. Follow instructions in [fuchsia](https://fuchsia.googlesource.com/fuchsia/+/HEAD/README.md) for getting the source, setup, build and running Fuchsia.
1. Follow instructions in [ledger](https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md) to setup the Ledger and all its dependencies including netstack and minfs.

## Running

On the Fuchsia command line you can start an example application flow like this:

```sh
device_runner --user_shell=dummy_user_shell
```

A single application can be run using the `dev_user_shell`, for example:

```sh
device_runner --user_shell=dev_user_shell --user_shell_args='--root_module=example_flutter_counter_parent,--root_link={"http://schema.domokit.org/counter":5}'
```

Note: if you are running this through *netruncmd* you will need to escape the quotes:

```sh
device_runner --user_shell=dev_user_shell --user_shell_args='--root_module=example_flutter_counter_parent,--root_link={\\\"http://schema.domokit.org/counter\\\":5}'
```

The flags `--user_shell` and `--user_shell_args` are read by `device_runner`.
The value of `--user_shell` is the application run as user shell. The value of
`--user_shell_args` is a comma separated list of arguments passed to the user
shell application. In this example, these arguments are in turn more flags.
Commas inside the value of such arguments are escaped by backslashes. The value
of `--root_module` selects the module to run. The value of `--root_link` is a
JSON representation of the initial data the module is started with.

The dummy user name used by the dummy user shell can be set with `--user`:

```sh
device_runner --user_shell=dummy_user_shell --user_shell_args=--user=dummy_user
```

Applications are generally referenced by URLs. If the application binary is in a
location where application manager expects it (specifically `/system/apps`)
the URL can be relative. Otherwise, the URL should be relative with an absolute
path, or absolute altogether. For example
(from [test runner invocation](tests/parent_child/test.sh)):

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=/tmp/tests/parent_child/parent_module
```

or even more generally:

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=file:///tmp/tests/parent_child/parent_module
```

## Contents

 - [docs](docs) - documentation
 - [services](services) - fidl API
 - [src](src) - implementation
 - [tests](tests) - testing
