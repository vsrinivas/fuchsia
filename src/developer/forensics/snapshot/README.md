# snapshot: snapshot the current state of the device

## Overview

`snapshot` is a tool that allows developers to snapshot the current state of
their device and typically attach its output zip file on the host to issues they
file.

## Invoking the target program from the host

### In-tree

To invoke `snapshot` in-tree, you can simply run:

```sh
(host)$ fx snapshot
```

This will output to stdout the path to the generated zip file.

### Out-of-tree

To invoke `snapshot` out-of-tree, you need to SSH into the target and run the
target program `snapshot`, which will dump the zip file into stdout.

```sh
(host)$ <SSH into the target and run `snapshot` on target> > /some/host/path/to/snapshot.zip
```

## Question? Bug? Feature request?

Contact OWNERS.
