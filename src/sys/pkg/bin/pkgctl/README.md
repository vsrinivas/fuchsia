# Fuchsia Package Resolver Control

This repository provides basic control of the package resolver and package cache
components.

## Examples

To resolve a package, run:

```
% pkgctl resolve fuchsia-pkg://example.com/fonts
```

To open a package (and display its contents) by merkle root, run:

```
% pkgctl open $MERKLE_ROOT
```

## URI Rewrite Rules

Prior to resolving a fuchsia-pkg URI into a package directory, the package
resolver will first iterate through its rewrite rules in sequence looking for
the first rule that matches the given URI and that, when applied to the given
URI, produces another valid URI. This rewritten URI is the resolved instead and
the given URI is discarded. If no rules match, the given URI is used as-is.

A rewrite rule must match the entire host of the URI and must prefix match the
URI's path at a `/` boundary. If the rewrite rule's path doesn't end in a `/`,
it must match the entire URI path.

## Examples

View all configured static and dynamic rewrite rules:
```
% pkgctl rule list
```

Remove all configured dynamic rewrite rules. Any static rewrite rules cannot be
removed.
```
% pkgctl rule clear
```

Replace the configured dynamic rewrite rules with the rule config present on
a device at `/tmp/rules.json`:
```
% pkgctl rule replace file /tmp/rules.json
```

View the configured rewrite rules:
```
% pkgctl rule dump-dynamic
```

**Note**: The following set of rewrite rules will redirect the "/rolldice" package 
from "example.com" to the repository with the hostname "devhost.example".

```json
{
    "version": "1",
    "content": [
        {
            "host_match": "example.com",
            "host_replacement": "devhost.example",
            "path_prefix_match": "/rolldice",
            "path_prefix_replacement": "/rolldice"
        }
    ]
}
```
