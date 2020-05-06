# Define an index of components which use storage

Note: This guide uses the [components v1](/docs/glossary.md#components-v1)
architecture.

## Terminology

- [Component Moniker](/docs/glossary.md#moniker)
- [Component Instance ID](/docs/glossary.md#component-instance-id)

## Scope

This document describes how to define an index that maps instance ids to
monikers for components that use isolated storage.

## Overview

The goal of an index of component instance IDs is to assign stable identifiers
to component instances. This is done by mapping an instance ID to a moniker.
When a component instance is assigned an ID, its persistent resources are
identified using this instance ID. This allows the component's URL or realm to
be changed while its resources still remain attributed to it, so long as this
index is also updated.

When the component runtime discovers an instance ID -> moniker mapping, it
automatically moves the component instance's existing storage directory to be
keyed under its instance ID.

Only components which use storage capabilities must to be included in the
index. The following class of components need not be included in the
index:

* Test components
* Components whose storage is not managed by appmgr.

## Defining an index

An index file is a JSON5 formatted file, mapping a component's instance ID to
its moniker. There may be multiple index files, but a system build will merge
them together into a single index file and make it available to the component
runtime. This merged index file is immutable, and can only be updated through
another system update.

The schema for an index file is described in the following example:

```json
// Index files are written in JSON5, so you may use comments!
{
  // A list of entries, where each entry maps an instance ID to a moniker.
  instances: [
    // An entry, mapping an instance ID to a moniker.
    {
      // Instance IDs are randomly generated, 256-bits of base-16 encoded
      // strings (in lower case).
      instance_id: "11601233aef81741f7251907d4d2a1a33aa6fec6b2e54abffc21bec29f95fec2",
      // The `instance_id` above is associated to the following moniker:
      appmgr_moniker: {
        // This the URL of the component.
        url: "fuchsia-pkg://example.com/my_package#meta/my_component.cmx",

        // This is the realm path where this component runs.
        realm_path: [
          "sys",     // This the parent realm of "session"
          "session"  // This is the realm the component runs under
        ]
      }
    },

    // More than one entry can be included. However, all entries must be distinct:
    // * Two entries cannot reference the same `instance_id`
    // * Two entries cannot reference the same `realm`
    {
      instance_id: "644a7f0f66f8994d894c5f78b5b879911fee6c185c6aadd29d52888812d20ac4",
      appmgr_moniker: {
        url: "fuchsia-pkg://example.com/my_other_package#meta/my_other_component.cmx",
        realm_path: [
          "sys"
        ]
      }
    }
  ]
}
```

To supply an index file to the build, use the
[component_id_index()](/build/component/component_id_index.gni) GN template:

```gn
component_id_index("my_component_id_index") {
  source = "my_component_id_index.json"
}
```

## Including a Component ID Index in a System Assembly {#system-assembly}

_The target audience for this section are product owners who are
setting up a system assembly_

This section describes how to include the component ID index in a system
assembly.

A system assembly should include a component ID index if it contains components
which use isolated storage. Any product which builds on top of the
`core` product already includes a component ID index, so the following
instructions are not necessary.

### `component_id_index_config_package()`
All component_id_index()s in a system
build are merged together using the `component_id_index_config_package()`
template, which produces a `config_data(for_pkg=appmgr)`. `appmgr` then
consumes the merged index using this mechanism.

To include a `component_id_index_config_package()` target in a system assembly:

**a)** Define it with a dependency on any `component_id_index()` targets which you
want included in the system. For example, //build/images:universe_packages is a
good dependency candidate because it transitively includes all
`component_id_index()` specified in the build.

**b)** Add your `component_id_index_config_package()` target to the system assembly.
Currently, a good method is to include your `component_id_index_config_package()`
target as a dependency to your system assembly's `config_package()`.