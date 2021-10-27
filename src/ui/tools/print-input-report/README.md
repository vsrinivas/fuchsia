# `print-input-report`

A utility that can attach itself to an input device and read the device
descriptors or events.

## Building and testing

```
fx set ... --with=//src/ui/tools/print-input-report
fx build
fx test //src/ui/tools/print-input-report
```

## Example use

From the host command line:

```
fx shell run fuchsia-pkg://print-input-report#meta/print-input-report.cmx descriptor /dev/class/input-report/002
```

## Get more help

The invocation below will print usage.

```
fx shell run fuchsia-pkg://print-input-report#meta/print-input-report.cmx
```
