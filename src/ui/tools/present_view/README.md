# Scenic View Debugger

This directory contains `present_view`, a utility which can debug processes that provide Views via the
`fuchsia.ui.app.ViewProvider` or `fuchsia.ui.views.View` interfaces.

The `present_view` tool takes the URL to a process that exposes one of these interfaces and makes its View the root
(fullscreen) View.

This tool is intended for testing and debugging purposes only and may cause problems if invoked incorrectly.

## Usage

```shell
$ present_view <view_provider_process>
e.g.
$ present_view spinning_square_view
```

If another view-providing process is already using the display, kill it like so:
```shell
$ killall spinning_square_view
```

## Using `fuchsia.intl.PropertyProvider`

`present_view` can optionally start a component that will serve `fuchsia.intl.PropertyProvider` for
the started process.  This is useful for debugging programs that require this functionality.  This
option is not the default as different environments may not have this service available.  The
functionality is invoked by adding a flag `--locale=...`, where the flag value is a comma-separated
list of BCP-47 locale identifiers that we want to be served by `fuchsia.intl.PropertyProvider`.

Example below.

```shell
$ present_view --locale=<locale_id>[,<locale_id>]... <view_provider_process>
e.g.
$ present_view --locale=nl-NL,ru-RU spinning_square_view
```
