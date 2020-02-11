# Mirror: keep files in sync between the host and the target.

## Overview

It can be useful to copy files back and forth from the host machine to a
Fuchsia target.  The utility in this directory keeps files in sync between
the two.

## Host side

To use this on the host side, add

```
"//src/developer/shell/mirror:sh_mirror_host"
```

To your build.  You can then invoke the server on your host:

```
host-tools/sh_mirror --path=<path-to-be-mirrored> --port=<port>
```

It will await connections on the port, and send the contents of `path` to the
client.

## Client side

To use this on the client side, include

```
"//src/developer/shell/mirror:client"
```

In the target from which you want to call the server.

You can then `client.h` in your C++ program.  See the APIs in that directory
for more information.
