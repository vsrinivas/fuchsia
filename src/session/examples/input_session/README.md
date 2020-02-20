# Input Session Example

Reviewed on: 2020-02-04

This directory contains an example implementation of a session that opens input devices and simply logs some of the data from the input reports (keyboard key, touch coordinates, mouse coordinates, etc.).

## Building `input_session`

The example sessions are included in the build when you include `//src/session` with your `fx set`:

```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config
```

To see a list of possible products, run: `fx list-products`.

To see a list of possible boards, run: `fx list-boards`.

## Running `input_session`
### Boot into `input_session`

To boot into `input_session`, first edit the [session manager cml](//src/session/bin/session_manager/meta/session_manager.cml) file to set the input session's component url as the argument:

```
args: [ "-s", "fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm" ],
```
and run
```
fx update
```

To build the relevant components and boot into the session, follow the instructions in [//src/session/README.md](//src/session/README.md).

### Launch `input_session` from Command Line

To instruct a running `session_manager` to launch the session, run:
```
fx shell session_control -s fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm
```

The last command should output messages to the system log for each connected (known) input device. Some device types (such as keyboard and mouse) have multiple devices under certain circumstances, such as when running from a Chrome Remote Desktop session. Typically only one of each device type produces events.

Once connected, type on the keyboard, click and drag a mouse, or touch the touch screen (depending on the device). These actions should print messages to the system log.

## Testing

Add `--with //src/session:tests` to your existing `fx set` command to include the `input_session` unit tests in the build. The resulting `fx set` looks like:
```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```
To see a list of possible products, run: `fx list-products`.

To see a list of possible boards, run: `fx list-boards`.

The tests are available in the `input_session_tests` package. To run the tests, use:
```
$ fx run-test input_session_tests
```

## Source Layout

The entry point and units tests are located in `src/main.rs`.
