# Supply build-time configuration data to components

## Terminology

Base: A collection of software that constitutes the core of the system and is
updated atomically as part of a system update.

Component: A unit of execution started by the component framework which
constructs its sandbox environment.

Package: A unit of distribution in Fuchsia which is a collection of files.
See [the Fuchsia package manager](/src/sys/pkg/bin/pm/README.md#structure-of-a-fuchsia-package){:.external}.

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

If you want your package to insert configuration data into another package,
you need to create a `config_data` rule and use the `for_pkg` attribute to
indicate the target package.

The following parameters are supported:

*   **`for_pkg`** (Required): Indicates the name of the package for which this
    configuration is intended.
*   **`sources`** (Required): Zero or more files to include in the
    configuration.
*   **`outputs`** (Optional): If provided, a list containing exactly one
    pattern to indicate the output file name(s).
    If a single source is provided, then the pattern can be a simple file name.
    If multiple sources are provided, then the pattern should use
    [GN placeholders][gn-placeholders] syntax.

```
config_data("tennis_sysmgr_config") {
  for_pkg = "sysmgr"
  # The file "tennis_sysmgr.config" must be present in the same directory.
  sources = [ "tennis_sysmgr.config" ]
  # The file will be available at runtime as "tennis.config".
  outputs = [ "tennis.config" ]
}
```

### Consuming

#### Components v1

Include the following in your component manifest (`.cmx`) file:

```json
{
    "sandbox": {
        "features": [
            "config-data"
        ]
    }
}
```

At runtime your component will be able to access the config files at the path
`/config/data`. The files will be read-only.

#### Components v2

Include the following in your component manifest (`.cml`) file:

```json
{
    use: [
        {
            directory: "config-data",
            rights: [ "r*" ],
            path: "/config/data",
        },
    ],
}
```

At runtime your component will be able to access the config files at the path
`/config/data`. The files will be read-only.

For the above to work, `"config-data"` must be offered to your component.
For instance your parent may have a declaration that looks like this:

```json
{
    children: [
        {
            name: "foo-component",
            url: "fuchsia-pkg://fuchsia.com/foo-package#meta/foo-component.cm",
        },
    ],
    offer: [
        {
            directory: "config-data",
            from: "parent",
            to: [ "#foo-component" ],
            subdir: "foo-package",
        },
    ],
}
```

Note that by convention a sub-directory of the parent's `"config-data"`, named
according to the child's package name, is offered to the child component.

### Updating

The `config-data` package is part of the base set.
Base packages are part of the system image and can only be updated by a system
OTA or by repaving. As such, configs cannot be updated at runtime.

For instance, if working with an emulator, rebuild the system image and restart
the emulator for your configuration changes to take effect.

[gn-placeholders]: https://gn.googlesource.com/gn/+/HEAD/docs/reference.md#placeholders
