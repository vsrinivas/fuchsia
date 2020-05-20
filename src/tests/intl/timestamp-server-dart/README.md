# A simple time server

This directory contains an ever-so-tiny FIDL server written in dart whose only purpose in life is to
respond to client requests with the current year, month, day and hour, expressed in the local time
zone of the Dart VM it is running in.

So for example, when asked for date and time on May 19, 2020 at 4:30pm in Los Angeles, it will
respond with "2020-05-19-16".

The server is used in tests that compare the local time that the Dart VM sees with the local time on
the device itself.  Read more [in the test README.md](../timezone/README.md).

# Build

```
fx set ... --with=//src/tests/intl:tests
```

# Run (as a stand-alone program, should you need that)

```
fx serve & \
fx shell run \
  fuchsia-pkg://fuchsia.com/timestamp-server-dart#meta/timestamp-server-dart.cmx
```


