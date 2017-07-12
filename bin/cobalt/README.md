# Cobalt Fuchsia Client

FIDL service exposing access to the Cobalt service.

## Cobalt Test App

The Cobalt test app `cobalt_testapp.cc` serves as an example usage of the Cobalt
FIDL service.

## Building the test app:

At the root of the fuchsia repo type:

```
$ source scripts/env.sh && envprompt
$ fset x86-64 --modules default,cobalt_client
$ ./buildtools/ninja -C out/debug-x86-64 -j 1000
```

## Running the test app:

Run qemu:

```
$ frun -m 3000 -k -N -u ./scripts/start-dhcp-server.sh
```

From within the running instance of fuchsia type:

```
$ system/test/cobalt_testapp
```

If all goes fine, you should see "CobaltTestApp" then "Done".

If you have access to the Cobalt production cluster, look at the logs in the
shuffler for 7 observations.
