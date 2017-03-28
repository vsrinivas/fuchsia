# Catapult dashboard local setup

Notes on setting up a local instance of the Catapult dashboard for development
purposes. These instructions were only checked to work on a Linux workstation
and might or might not work on a Mac host.

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

`cd dashboard && bin/dev_server --host=0.0.0.0`

By running the dev server on `0.0.0.0` we ensure it is reachable from all
network interfaces.

## Uploading data points

Run Fuchsia. Take note of the ip address of your Fuchsia qemu interface by
running `ifconfig` on the host.

Run `wget http://<ip address>:8080` to ensure that the dashboard is accessible
from Fuchsia. If this works, you're all set - run `trace record` with the result
upload parameters, including `--upload-server-url=<ip address>`.

## Refreshing the test suite names

On the host, visit `http://localhost:8080/update_test_suites` and
`http://localhost:8080/update_test_suites?internal_only=true` to make newly
added test suites appear in the test suite picker.

[prerequisites]: https://github.com/catapult-project/catapult/blob/master/dashboard/docs/getting-set-up.md#prerequisites
