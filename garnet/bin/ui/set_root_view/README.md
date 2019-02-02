# Scenic View Debugger

This directory contains set_root_view, a utility which can debug processes that
provide Views via the ViewProvider interface.

The set_root_view tool takes the URL to a process that exposes the ViewProvider
interface and makes its View the root (fullscreen) View.

This tool is intended for testing and debugging purposes only and may cause
problems if invoked incorrectly.

## USAGE

```shell
$ set_root_view <view_provider_process>
e.g.
$ set_root_view spinning_square_view
```

When a view is presented, it takes over the entire display. To switch between
presentations, type Control-Alt-'[' or Control-Alt-']'.

Alternatively, kill any other view-providing processes like so:

```shell
$ killall spinning_square_view
```

## Views V2

Use the
[`present_view`](https://fuchsia.googlesource.com/fuchsia/+/master/garnet/bin/ui/present_view/README.md)
tool to launch an application via the Views v2 interface.
