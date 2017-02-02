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
@ bootstrap device_runner --user_shell=dummy_user_shell
```

A single application can be run using the `dev_user_shell`, for example:

```sh
@ bootstrap device_runner --user_shell=dev_user_shell --user_shell_args='--root_module=example_flutter_counter_parent,--root_link={"http://schema.domokit.org/counter":5}'
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
@ bootstrap device_runner --user_shell=dummy_user_shell --user_shell_args=--user=dummy_user
```

Applications are generally referenced by URLs. If the application binary is in a
location where application manager expects it (specifically `/system/apps`)
the URL can be relative. Otherwise, the URL should be relative with an absolute
path, or absolute altogether. For example
(from [test runner invocation](tests/parent_child/test.sh)):

```
bootstrap device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=/tmp/tests/parent_child/parent_module
```

or even more generally:

```
bootstrap device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=file:///tmp/tests/parent_child/parent_module
```

See also the [bootstrap](src/bootstrap/README.md) documentation.

## Testing

Testing support is currently under development.
Please use our example as an integration test and wait for it to complete.

```sh
$ @ bootstrap device_runner --user_shell=dummy_user_shell
...
...
[00091.616] 03539.03566> [INFO:../../apps/modular/src/user_runner/dummy_user_shell.cc(256)] DummyUserShell DELETE STORY DONE
[00091.616] 03071.03110> [INFO:../../apps/modular/src/user_runner/user_runner.cc(138)] UserRunner::Terminate: Terminating UserRunner.
[00091.621] 02886.02905> [INFO:../../apps/modular/src/device_runner/dummy_device_shell.cc(62)] User logged out. Starting shutdown.
[00091.622] 02794.02810> [INFO:../../apps/modular/src/device_runner/device_runner.cc(167)] Shutting down DeviceRunner..
```

## Contents

 - [docs](docs) - documentation
 - [services](services) - fidl API
 - [src](src) - implementation
