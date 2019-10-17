# bugreport: snapshot the current state of the device

## Overview

`bugreport` is a tool that allows developers to snapshot the current state of
their device and typically attach its output zip file on the host to issues they
file.

## Invoking the target program from the host

### In-tree

To invoke `bugreport` in-tree, you can simply run:

```sh
(host)$ fx bugreport
```

This will output to stdout the path to the generated zip file.

### Out-of-tree

To invoke `bugreport` out-of-tree, you need to SSH into the target and run the
target program `bugreport`, which will dump the zip file into stdout.

```sh
(host)$ <SSH into the target and run `bugreport` on target> > /some/host/path/to/bugreport.zip
```

## Question? Bug? Feature request?

Contact OWNERS.
