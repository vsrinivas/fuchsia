# Getting Started with Modular

## Prerequisities

1. Follow instructions in [fuchsia] for getting the source, setup, build and
   running Fuchsia.
2. Follow instructions in [ledger] to setup the Ledger and all its dependencies
   including netstack and minfs.

To run modules from the command line, you need to modify the startup behavior of
Fuchsia to prevent `device_runner` and Armadillo from starting by default. Use
fset and rebuild as shown below:

``` sh
source fuchsia/scripts/env.sh
fset x86-64 --modules packages/gn/boot_headless
fbuild
```

## Running

You can start the basic test application like this.

```sh
device_runner --device_shell=dev_device_shell --user_shell=test_user_shell
```

A single application can be run using `dev_user_shell`. For example, to run
`example_flutter_counter_parent`, use this command:

```sh
device_runner --device_shell=dev_device_shell --user_shell=dev_user_shell --user_shell_args='--root_module=example_flutter_counter_parent,--root_link={"http://schema.domokit.org/counter":5}'
```

`dev_device_shell` is used to log in a dummy user directly without going through
an authentication dialog. `test_user_shell` runs the test.

Note: if you are running this through `netruncmd` you will need to double-escape
the quotes:

```sh
netruncmd : "device_runner --device_shell=dev_device_shell --user_shell=dev_user_shell --user_shell_args='--root_module=example_flutter_counter_parent,--root_link={\\\"http://schema.domokit.org/counter\\\":5}'"
```

The flags `--user_shell` and `--user_shell_args` are read by `device_runner`.
The value of `--user_shell` is the application that is run as the user shell.
The value of `--user_shell_args` is a comma separated list of arguments passed
to the user shell application. In this example, these arguments are in turn more
flags.  Commas inside the value of such arguments are escaped by backslashes.
The value of `--root_module` selects the module to run. The value of
`--root_link` is a JSON representation of the initial data the module is started
with.

The user name provided by `dev_device_shell` can be set with `--user`. It is used
by `device_runner` when opening the Ledger.  However, the `--user` parameter does
not work for `userpicker_device_shell`:

```sh
device_runner --device_shell=dev_device_shell --device_shell_args=--user=dummy_user --user_shell=test_user_shell
```

## Module URLs

Applications are generally referenced by URLs. If the application binary is in a
location where application manager expects it (specifically `/system/apps`)
the URL can be relative. Otherwise, the URL should be relative with an absolute
path, or absolute altogether. For example
(from [test runner invocation](tests/parent_child/test.sh)):

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=/system/apps/modular_tests/parent_child/parent_module
```

or even more generally:

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=file:///system/apps/modular_tests/parent_child/parent_module
```

[fuchsia]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/README.md
[ledger]: https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md
