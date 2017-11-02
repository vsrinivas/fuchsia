# Getting Started with Modular

## Prerequisities

1. Follow instructions in [fuchsia] for getting the source, setup, build and
   running Fuchsia.
2. Follow instructions in the [Ledger User Guide] to setup the Ledger and its
   dependencies. In particular, make sure to do the "minfs setup" as linked
   under the Prerequisites.

To run modules from the command line, you need to modify the startup behavior of
Fuchsia to prevent `device_runner` and Armadillo from starting by default. Use
fset and rebuild as shown below:

``` sh
fx set x86-64 --packages packages/gn/boot_test_modular
fx full-build
```

(If `fx` is not in your `PATH`, you should add `.jiri_root/bin` to your
`PATH`.)

## Running

You can start the basic test application like this.

```sh
device_runner --account_provider=dev_token_manager \
  --device_shell=dev_device_shell --user_shell=test_user_shell \
  --story_shell=dev_story_shell
```

A single application can be run using `dev_user_shell`. For example, to run
`example_flutter_counter_parent_story_shell`, use this command:

```sh
device_runner --account_provider=dev_token_manager \
  --device_shell=dev_device_shell --user_shell=dev_user_shell \
  --user_shell_args='--root_module=example_flutter_counter_parent_story_shell' \
  --story_shell=dev_story_shell
```

`dev_device_shell` is used to log in a dummy user directly without going through
an authentication dialog. `test_user_shell` runs the test.

Note: if you are running this through `netruncmd` or `fssh` you will need to
double-escape the quotes:

```sh
netruncmd : "device_runner --account_provider=dev_token_manager \
  --device_shell=dev_device_shell --user_shell=dev_user_shell \
  --user_shell_args='--root_module=example_flutter_counter_parent_story_shell,--root_link={\\\"http://schema.domokit.org/counter\\\":5}' \
  --story_shell=dev_story_shell"

fssh "$($FUCHSIA_OUT_DIR/build-zircon/tools/netaddr --fuchsia :)" \
  "device_runner --device_shell=dev_device_shell --user_shell=dev_user_shell \
  --user_shell_args='--root_module=example_flutter_counter_parent_story_shell,--root_link={\\\"http://schema.domokit.org/counter\\\":5}' \
  --story_shell=dev_story_shell"
```

The flags `--user_shell` and `--user_shell_args` are read by `device_runner`.
The value of `--user_shell` is the application that is run as the user shell.
The value of `--user_shell_args` is a comma separated list of arguments passed
to the user shell application. In this example, these arguments are in turn more
flags.  Commas inside the value of such arguments are escaped by backslashes.
The value of `--root_module` selects the module to run. The value of
`--root_link` is a JSON representation of the initial data the module is started
with.

The user name provided by `dev_device_shell` can be set with `--user`. It is
used by `device_runner` when opening the Ledger.  However, the `--user`
parameter does not work for `userpicker_device_shell`:

```sh
device_runner --account_provider=dev_token_manager \
  --device_shell=dev_device_shell --device_shell_args=--user=dummy_user \
  --user_shell=test_user_shell --story_shell=dev_story_shell
```

## Module URLs

Applications are generally referenced by URLs. If the application binary is in a
location where application manager expects it (e.g., `/system/bin`)
the URL can be relative. Otherwise, the URL should be relative with an absolute
path, or absolute altogether. For example (from [test runner invocation]):

```
device_runner --account_provider=dev_token_manager --user_shell=dev_user_shell \
  --user_shell_args=--root_module=/system/test/modular_tests/parent_module \
  --story_shell=dev_story_shell
```

or even more generally:

```
device_runner --account_provider=dev_token_manager --user_shell=dev_user_shell \
  --user_shell_args=--root_module=file:///system/test/modular_tests/parent_module \
  --story_shell=dev_story_shell
```

[fuchsia]: https://fuchsia.googlesource.com/docs/+/master/README.md
[Ledger User Guide]: ../ledger/user_guide.md
[test runner invocation]: ../../tests/modular_tests.json
