# Banjo

## About

TODO(surajmalhotra): Explain banjo's purpose and usage.

## Building

Run the following commands:

```
fx build src/devices/tools/banjo
fx build
```

## Testing

Run the following commands:

```
fx set ${product}.${board} --with //bundles:tests
fx test //src/devices/tools/banjo
```
