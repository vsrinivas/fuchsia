# bugreport: snapshot the current state of the device

## Overview

`bugreport` is a tool that allows developers to snapshot the current state of
their device and typically attach its output files on the host to issues they
file.

It consists of a `bugreport` target component and a `bugreport` host tool to
parse the JSON data the target program streams back and to generate the output
files on the host.

## Running the host tool

### In-tree

To run `bugreport` in-tree, you first build it. Most product configurations
include //bundle:tools in their universe so `fx build` should suffice. You can
then simply run:

```sh
(host)$ fx bugreport
```

This will output to stdout the path to the generated files.

#### No ssh access to the device? Try serial!

By default, `fx bugreport` ssh into the device like most other fx commands. In
case this is not possible and the host is connected to the device via serial,
you can run:

```sh
(host)$ fx bugreport --serial
```

This will output to stdout the path to the generated files.

NOTE: the serial mode is slow and not robust. If at any point another program on
the target writes something to the kernel logs then it will mess up the JSON
data that is being streamed back to the host, resulting in a failure. Re-running
the command might help if it was a spurious kernel log message, but not if the
kernel logs are getting constantly spammed by some other program. In the latter
case, you might want to try the [minimal mode](#minimal).

### Out-of-tree

To run `bugreport` out-of-tree, the host tool is exported in the SDK under
tools/. You can then pipe the output of running `bugreport` on the target to
sdk/tools/bugreport, providing an output directory.

```sh
(host)$ <run bugreport on target> | sdk/tools/bugreport <output dir>
```

This will output to stdout the path to the generated files.

## Minimal output {#minimal}

By default, two of the generated files are the kernel and system logs, which can
be quite big. The target component `bugreport` has a minimal mode that will
limit its output to the build information and the Inspect data.

On the host you can simply run (in-tree):

```sh
(host)$ fx bugreport -- --minimal
(host)$ fx bugreport --serial -- --minimal
```

On the target you can run:

```sh
(fx shell)$ bugreport --minimal
(fx serial)$ run bugreport.cmx --minimal
```

## Question? Bug? Feature request?

Contact donosoc and frousseau, or file a
[bug](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10006&priority=3&components=11880)
or a
[feature request](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10005&priority=3&components=11880).
