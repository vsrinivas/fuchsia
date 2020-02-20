# Graphical Session Example

Reviewed on: 2020-02-04

This directory contains an example implementation of a session that uses [`scenic`](//src/ui/scenic) to render and animate an image to the screen.

## Building `graphical_session`

The example sessions are included in the build when you include `//src/session` with your `fx set`:

```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config
```

To see a list of possible products, run: `fx list-products`.

To see a list of possible boards, run: `fx list-boards`.

## Running `graphical_session`
### Boot into `graphical_session`

To boot into `graphical_session`, edit the [session manager cml](//src/session/bin/session_manager/meta/session_manager.cml) file to set the input session's component url as the argument:
```
args: [ "-s", "fuchsia-pkg://fuchsia.com/graphical_session#meta/graphical_session.cm" ],
```
and run
```
fx update
```

To build the relevant components and boot into the session, follow the instructions in [//src/session/README.md](//src/session/README.md).

### Launch `graphical_session` from Command Line

To instruct a running `session_manager` to launch the session, run:
```
fx shell session_control -s fuchsia-pkg://fuchsia.com/graphical_session#meta/graphical_session.cm
```

## Testing

Add `--with //src/session:tests` to your existing `fx set` command to include the `graphical_session` unit tests in the build. The resulting `fx set` looks like:
```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```
To see a list of possible products, run: `fx list-products`.

To see a list of possible boards, run: `fx list-boards`.

The tests are available in the `graphical_session_tests` package. To run the tests, use:
```
$ fx run-test graphical_session_tests
```

## Source Layout

The entry point and session units tests are located in `src/main.rs`. `src/views.rs` contains a struct that represents the graphical elements and manages the interaction with `scenic`. Creating the connection to `scenic` and handling the timing of updates is done in `src/app.rs`. `src/graphics_util.rs` contains some useful helper functions.

The images that is rendered on the screen is in `/resources`.
