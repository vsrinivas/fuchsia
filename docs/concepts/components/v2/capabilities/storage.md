# Storage capabilities

[Storage capabilities][glossary-storage] are a way for components to define,
[offer][offer], and [use][use] directories, but they have different semantics
than [directory capabilities][directory-capabilities]. Storage capabilities are
not directly provided from a component instance's
[outgoing directory][outgoing-directory], but are created from preexisting
directory capabilities that are declared as a
[`storage` `capability`][manifests-storage] in a component manifest.

Directories provided by storage capabilities are guaranteed to be unique and
non-overlapping for each [component instance][component-instance], preventing
any component instances from accessing files belonging to any other component
instance (including their own children).

There are different types of storage capabilities, each with different
semantics. For more information, see [storage types](#storage-types).

## Storage types {#storage-types}

Storage capabilities are identified by types. The following storage types
are supported:

-   `data`: A mutable directory the component may store its state in. This
    directory is guaranteed to be unique and non-overlapping with directories
    provided to other components.
-   `cache`: Identical to the `data` storage type, but the framework may delete
    items from this directory to reclaim space.
-   `meta`: A directory where the framework can store metadata for the component
    instance. Features such as persistent collections must use this capability
    as they require component manager to store data on the component's behalf.
    The component cannot directly access this directory.

## Directory vs storage capabilities

As an example, if component instance `a` receives a _directory_ capability from
its [realm][realm] and both [uses][use] it and [offers][offer] it to `b`, which
also uses the directory, both component instances can see and interact with the
same directory.

```
<a's realm>
    |
    a
    |
    b

a.cml:
{
    use: [
        {
            directory: "example_dir",
            rights: ["rw*"],
            path: "/example_dir",
        },
    ],
    offer: [
        {
            directory: "example_dir",
            from: "parent",
            to: [ "#b" ],
        },
    ],
}

b.cml:
{
    use: [
        {
            directory: "example_dir",
            rights: ["rw*"],
            path: "/example_dir",
        },
    ],
}
```

In this example if component instance `a` creates a file named `hippos` inside
`/example_dir` then `b` will be able to see and read this file.

If the component instances use storage capabilities instead of directory
capabilities, then component instance `b` cannot see and read the `hippos` file.

```
<a's realm>
    |
    a
    |
    b

a.cml:
{
    use: [
        {
            storage: "data",
            path: "/example_dir",
        },
    ],
    offer: [
        {
            storage: "data",
            from: "parent",
            to: [ "#b" ],
        },
    ],
}

b.cml:
{
    use: [
        {
            storage: "data",
            path: "/example_dir",
        },
    ],
}
```

In this example any files that `a` creates are not be visible to `b`, as storage
capabilities provide unique non-overlapping directories to each component
instance.

## Creating storage capabilities

Storage capabilities can be created with a
[`storage` declaration][storage-syntax] in a [component manifest][manifests].
Once storage capabilities have been declared, they can then be offered to other
component instances by referencing the declaration by name.

A `storage` declaration must include a reference to a directory capability,
which is the directory from which the component manager will create isolated
directories for each component instance using the storage capability.

For example, the following manifest describes new storage capabilities backed by
the `memfs` directory exposed by the child named `#memfs`. From this storage
declaration a data storage capability is offered to the child named
`storage_user`.

```
{
    capabilities: [
        {
            storage: "mystorage",
            from: "#memfs",
            backing_dir: "memfs",
        },
    ],
    offer: [
        {
            storage: "data",
            from: "#mystorage",
            to: [ "#storage-user" ],
        },
    ],
    children: [
        { name: "memfs", url: "fuchsia-pkg://..." },
        { name: "storage-user", url: "fuchsia-pkg://...", },
    ],
}
```

## Storage capability semantics

A directory capability that backs storage capabilities can be used to access the
files of any component that uses the resulting storage capabilities. This type
of directory capability should be routed carefully to avoid exposing this
capability to too many component instances.

When a component instance attempts to access the directory provided to it
through a storage capability, the framework binds to and generates
sub-directories in the component instance that provides the backing directory
capability. Then, the framework provides the component instance access to a
unique sub-directory.

The sub-directory to which a component instance is provided access is determined
by the type of storage and its location in the component topology. This means
that if a component instance is renamed in its parent manifest or moved to a
different parent then it will receive a different sub-directory than it did
before the change.

[component-instance]: /docs/glossary.md#component-instance
[directory-capabilities]: /docs/glossary.md#directory-capability
[glossary-storage]: /docs/glossary.md#storage-capability
[manifests]: /docs/concepts/components/v2/component_manifests.md
[manifests-storage]: /docs/concepts/components/v2/component_manifests.md#capability-storage
[offer]: /docs/glossary.md#offer
[outgoing-directory]: /docs/concepts/system/abi/system.md#outgoing_directory
[realm]: /docs/glossary.md#realm
[storage-syntax]: /docs/concepts/components/v2/component_manifests.md#storage
[use-syntax]: /docs/concepts/components/v2/component_manifests.md#use
[use]: /docs/glossary.md#use
