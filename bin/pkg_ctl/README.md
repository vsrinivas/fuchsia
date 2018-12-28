# Fuchsia Package Resolver Control

This repository provides basic control of the package resolver and package cache
components.

## Examples

To resolve a package, run:

```
% pkgctl resolve fuchsia-pkg://fuchsia.com/fonts
```

To open a package (and display its contents) by merkle root, run:

```
% pkgctl open $MERKLE_ROOT
```
