# Graphical Session Example

Reviewed on: 2021-09-22

This directory contains an example implementation of a session that uses
[`scenic`](//src/ui/scenic) to render and animate an image to the screen.

## Building `graphical-session`

The example sessions are included in the build when you include `//src/session`
with your `fx set`:

```
fx set <PRODUCT>.<BOARD> --with //src/session
```

## Running `graphical-session`

To launch the session, run:

```
ffx session launch fuchsia-pkg://fuchsia.com/graphical-session#meta/graphical-session.cm
```

## Testing

Add `--with //src/session:tests` to your `fx set` command:

```
fx set <PRODUCT>.<BOARD> --with //src/session --with //src/session:tests
```

The tests are available in the `graphical-session-unittests` package. To run the
tests, use:

```
fx test graphical-session-unittests
```

## Source Layout

The entry point and session units tests are located in `src/main.rs`.
`src/views.rs` contains a struct that represents the graphical elements and
manages the interaction with `scenic`. Creating the connection to `scenic` and
handling the timing of updates is done in `src/app.rs`. `src/graphics_util.rs`
contains some useful helper functions.

The images that is rendered on the screen is in `/resources`.
