# botanist

**botanist** is a tool used by the infrastructure to set up the bot environment
before running tests. It handles setting some environment variables used by the
other tools it invokes and also handles the initial starting up of the target(s)
it will run the tests on.

`botanist run` is the subcommand which is used by most of the builders run by
the infrastructure. It takes a device config and image manifest and starts up
the corresponding target in the device config. It also takes a command to run
after starting up the targets. Usually this will be
[testrunner](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/testrunner)
which takes in a test manifest containing the tests to run.

## Logging

The continuous integration infrastructure sets several flags on botanist to
configure it to output logs to a special directory that will be uploaded to
cloud storage upon task completion, to be downloaded by the top-level tryjob for
presentation to users.

### Syslog

When running against a target that supports SSH, botanist streams syslogs from
the target for the duration of the subprocess that it starts.

The syslog is streamed from the target device by running the
`log_listener` command (see //garnet/bin/log_listener/README.md) over SSH, and
writing it to the local file specified by `-syslog`.

### Serial log

botanist also collects serial logs from devices that support a serial
connection, outputting the logs to the local path specified by `-serial-log`.
