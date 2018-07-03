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

If any other View-providing processes are running on your system, they should be
killed before running this tool:
```shell
$ killall spinning_square_view
```
