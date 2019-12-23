# Supplying Build-time Configuration Data to Components

## Terminology

**Base** - collection of software that constitutes the core of the system and is
updated atomically as part of a system update.

**component** - a unit of execution started by the component framework which
constructs its sandbox environment.

**[package](/src/sys/pkg/bin/pm/README.md#structure-of-a-fuchsia-package)** - a unit of distribution in Fuchsia which is a collection of files

## Scope

This document describes how to provide product-specific configuration data to
components on a Fuchsia system. The mechanism described here is designed for
use with components that are part of Base. This mechanism **is** **not**
suitable for things which are not components.

## Overview

The goal of the mechanism documented here is to provide a way for configuration
for Base components to be decentralized in the source. This means that
configuration data for a Base component can come from anywhere in the source tree
as Base is being built. This mechanism also makes it easier for component
configuration to vary by product.

The config-data mechanism could be used to provide configuration for
components that are not part of Base, but using it for this is not recommended.
Among other things, the configuration itself is updated as part of Base.
Configuration implies an API and the different update timings create the
potential for mismatches between the implied API of the configuration data and
the component consuming the configuration.

During the build of Base a package is constructed called `config-data`. This
package contains a set of directories whose names are matched against the names
of the packages of running components. When the component manager starts a
component that has requested the `config-data` feature, component manager will
examine the `config-data` package for a directory matching the name of the
package whose component is being started. If a match is found, the configuration
files appear in the component's namespace at `/config/data/`.

The `config-data` package is a package included in Base itself and is therefore
updated exactly when Base is updated. This fact reduces the security concerns of
access based only on string matching since we should have a fair amount of trust
that the software in Base is designed to work properly together.

## Using config-data

Files supplied via config-data are made available to all components within a
package to which the configuration is targeted if those components request
access to configuration data. It is not possible to restrict access of the
configuration data to anything finer than a package.

### Supplying Configuration

For things wishing to add configuration into another package the should create a
`config_data` rule. The rule has a `for_pkg` attribute which should be the
package for which this configuration is intended. The outputs and sources are an
order-matched set of inputs and outputs. If no outputs set is specified the
file(s) will be given the same name as appears in sources. If the outputs list is
supplied it must contain exactly one item. Multiple build rules may not supply
the same output file for the same package, doing so will result in a build
failure. For this reason it makes sense to consider namespacing the output either
by file name or directory conventions for each component.

```
config_data("tennis_sysmgr_config") {
  for_pkg = "sysmgr"
  outputs = [
    "tennis.config",
  ]
  sources = [
    "tennis_sysmgr.config",
  ]
}
```

### Consuming

The component that wants to consume configuration data must request this feature
in its component manifest, which might look something like the below.

```
{
    "program": {
        "binary": "bin/myapp"
        },
        "sandbox": {
            "features": [
                "config-data"
            ]
        }
    }
}
```

The component consuming the configuration can look in its `/config/data`
directory to see all the configuration files supplied to it.
