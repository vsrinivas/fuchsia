# Peridot Standalone Build

To get the source code for the Peridot layer, using the following commands
(see [Getting Source](https://fuchsia.googlesource.com/docs/+/master/getting_source.md)
for more information):

```
curl -s "https://fuchsia.googlesource.com/scripts/+/master/bootstrap?format=TEXT" | base64 --decode | bash -s peridot
```

To build the Peridot layer, use the following commands:

## x64

```
fx set x64
fx full-build
```

## arm64

```
fx set arm64
fx full-build
```
