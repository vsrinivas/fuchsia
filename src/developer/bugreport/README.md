# bugreport: snapshot the current state of the device

## Overview

`bugreport` is a tool that allows developers to snapshot the current state of
their device and typically attach its output files to issues they file.

It consists of a `bugreport` component running on target and a `bugreport` host
tool to parse the data the target program streams back and to generate the
output files.

## Running it

### In-tree

To run `bugreport` in-tree, you first build it. Most product configurations
include //bundle:tools in their universe so `fx build` should suffice. You can
then simply run:

```sh
fx bugreport
```

This will output to stdout the path to the generated files.

### Out-of-tree

To run `bugreport` out-of-tree, the host tool is exported in the SDK under
tools/. You can then pipe the output of running `bugreport` on the target to
sdk/tools/bugreport, providing an output directory.

```sh
<run bugreport on target> | sdk/tools/bugreport <output dir>
```

This will output to stdout the path to the generated files.

## Question? Bug? Feature request?

Contact donosoc and frousseau, or file a
[bug](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10006&priority=3&components=11880)
or a
[feature request](https://fuchsia.atlassian.net/secure/CreateIssueDetails!init.jspa?pid=11718&issuetype=10005&priority=3&components=11880).
