## Overview

This directory contains an example implementation of a session that
opens input devices and simply logs some of the data from the input reports
(keyboard key, touch coordinates, mouse coordinates, etc.).

## Run the Session

### Boot into Session

To boot into the example, first edit the session manager cml file to set the
input session's component url as the argument:

```
"args": [ "-s", "fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm" ],
```

To build the relevant components and boot into the session, follow the
instructions in [//src/session/README.md](../../README.md).

### Launch the Session from Command Line

To instruct a running `session_manager` to launch the session, run:

```
fx shell session_control -s fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm
```

The last command should output messages to the system log for each connected (known) input
device. Some device types (such as keyboard and mouse) have multiple devices under certain
circumstances, such as when running from a Chrome Remote Desktop session. Typically only
one of each device type will actually produce events.

Once connected, type on the keyboard, click and drag a mouse, or touch the touch screen
(depending on the device). These actions should print messages to the system log.
