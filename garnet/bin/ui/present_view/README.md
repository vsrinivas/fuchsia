# Scenic View Debugger

This directory contains present_view, a utility which can debug processes that
provide Views via the ViewProvider interface.

The present_view tool takes the URL to a process that exposes the ViewProvider
interface and makes its View the root (fullscreen) View.

This tool is intended for testing and debugging purposes only and may cause
problems if invoked incorrectly.

## USAGE

```shell
$ present_view <view_provider_process>
e.g.
$ present_view spinning_square_view
```

When a view is presented, it takes over the entire display. To switch between
presentations, type Control-Alt-'[' or Control-Alt-']'.

Alternatively, kill any other view-providing processes like so:
```shell
$ killall spinning_square_view
```
