# Catapult Dashboard

Catapult Dashboard a.k.a. Chrome Performance Dashboard is a system for tracking
performance data over time and detecting regressions. Fuchsia `trace` tool
supports uploading results of a benchmark run to a Catapult Dashboard.

This document describes how to set up a local instance of the Catapult Dashboard
for development purposes.

The instructions assume that the local instance runs on a **Linux host** and is
used to receive data points from a `trace` tool running on a Fuchsia device.
This might or might not work on a Mac host.

[TOC]

## Catapult

Get a copy of Catapult from https://github.com/catapult-project/catapult .

## Google Cloud SDK

Download Google Cloud SDK and put it in `PATH` as demanded in Catapult
[prerequisites]. Then hack it up - edit
`platform/google_appengine/google/appengine/tools/devappserver2/dispatcher.py`
and in `_resolve_target()`, replace:

```
try:
  _module, inst = self._port_registry.get(port)
except KeyError:
  raise request_info.ModuleDoesNotExistError(hostname)
```

with

```
try:
  _module, inst = self._port_registry.get(port)
except KeyError:
  _module, inst = None, None
```

This is a workaround for
https://code.google.com/p/googleappengine/issues/detail?id=10114 and allows
dashboard's task queue to work when the host name is other than localhost.

## Local server

In the local checkout of Catapult, run:

```sh
# on host
cd dashboard
bin/dev_server --host=0.0.0.0`
```

By running the dev server on `0.0.0.0`, we ensure that it is reachable from all
network interfaces.

Take note of the ip address of your host machine visible to the Fuchsia device
(e.g. by running `ifconfig` on the host).

Verify that the dashboard server is reachable from the Fuchsia device:

```sh
# on Fuchsia device
wget http://<ip address>:8080
```

## Upload data points

Run a benchmark with results upload enabled:

```sh
# on Fuchsia device
trace record \
  --spec-file=<path to spec file> \
  --upload-server-url=http://<ip address>:8080 \
  --upload-master=fuchsia_master \
  --upload-bot=fuchsia_bot \
  --upload-point-id=1
```

To verify that the new data points were registered by dashboard, take a look at
[/new_points]. If the new data points are not there, check the server log for
error messages.


## Inspect the graphs

To make sure that the test suites appear in the test suite picker, visit
[/update_test_suites] and [/update_test_suites?internal_only=true].

Make sure to upload more that one data point, with different
`--upload-point-id=` values. After that, [/report] should contain the new
graphs.


[prerequisites]: https://github.com/catapult-project/catapult/blob/master/dashboard/docs/getting-set-up.md#prerequisites
[/new_points]: http://localhost:8080/new_points
[/report]: http://localhost:8080/report
[/update_test_suites?internal_only=true]: http://localhost:8080/update_test_suites?internal_only=true
[/update_test_suites]: http://localhost:8080/update_test_suites
