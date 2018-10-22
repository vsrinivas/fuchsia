# Getting Started with Modular

## Prerequisities

1. Follow instructions under Development in [Fuchsia Documentation] to get the
   source, setup, build and run Fuchsia.
2. Follow instructions in the [Ledger User Guide] to setup the Ledger and its
   dependencies. In particular, make sure to do the "minfs setup" as linked
   under the Prerequisites.

To be able to run modules from the Fuchsia command line, you need to select a
Fuchsia build configuration that does not start `basemgr` and shows a
graphical user shell right at boot time. You can use for example the
`test_modular` configuration in `fx set` and build as follows:

``` sh
fx set x64 --packages peridot/packages/products/test_modular
fx full-build
```

If `fx` is not in your `PATH`, you should add `.jiri_root/bin` to your
`PATH`.

## Running

An application can be run as a module in a story using `dev_user_shell`. For
example (from [test runner invocation]):

```
basemgr --test --account_provider=dev_token_manager \
  --base_shell=dev_base_shell --user_shell=dev_user_shell \
  --story_shell=dev_story_shell \
  --user_shell_args=--root_module=parent_child_test_parent_module
```

`dev_base_shell` is used to log in a dummy user directly without going through
an authentication dialog. `dev_user_shell` runs the module given to it in
`--user_shell_args` in a story.

The flags `--user_shell` and `--user_shell_args` are read by `basemgr`.
The value of `--user_shell` is the application that is run as the user shell.
The value of `--user_shell_args` is a comma separated list of arguments passed
to the user shell application. In this example, these arguments are in turn more
flags. Commas inside the value of such arguments are escaped by backslashes. The
value of `--root_module` selects the module to run. The value of `--root_link`
is a JSON representation of the initial data the module is started with.

The user name provided by `dev_base_shell` can be set with `--user`. It is
used by `basemgr` when opening the Ledger. However, the `--user` parameter
does not work for `userpicker_base_shell`, because that shell displays a GUI
to select the user to login.

## Module URLs

Applications are generally referenced by URLs. If the application binary is in a
location where application manager expects it, e.g. `/pkgfs/packages`, the URL
can be relative. Otherwise, the URL should be relative with an absolute path, or
absolute altogether.

[Fuchsia Documentation]: https://fuchsia.googlesource.com/docs/+/master/README.md
[Ledger User Guide]: ../ledger/user_guide.md
[test runner invocation]: ../../tests/modular_tests.json
