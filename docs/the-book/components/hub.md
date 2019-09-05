# Hub

## Definition

The hub is a portal for tools to access detailed structural information about
component instances at runtime, such as their names, job and process ids, and
exposed capabilities.

## Organization

The hub’s structure is mostly read-only. It is not possible to create,
rename, delete, or otherwise modify directories and files which form the
structure of the hub itself. However, the
[outgoing](/docs/development/abi/system.md) directories of
component instances may include mutable directories, files, and services which
clients can access through the hub.

The component instance tree is reflected in the hierarchy of the hub. A given
[realm's](realms.md) hub can be accessed by successively traversing instances
down the `children/` subdirectory. Moreover, the root of a hub directory is
always scoped to a particular realm. Having opened a directory representing a
realm, a client can obtain information about the realm itself, its child realms,
and its component instances, but it cannot obtain any information about the
realm's parent. This structure makes it easy to constrain the parts of the hub
particular clients can access.

## Schema

The hub is organized as follows:

### [Execution Independent State](#execution-independent-state)

The following are a set of execution independent files and subdirectories. These
files and directories are visible and traversable even when a component is only
created but not started.

+ **url**: The component’s URL in text format.

+ **children/**: A directory of child component instances, indexed by
child moniker.

+ **children/\<name\>/...** : A child instance (looks like the root).

### [Execution Context](#execution-context)

The following subdirectories represent the state associated with the execution
context of a component instance. The execution subdirectory will only be visible
and traversable when a component instance is running on the system.

+ **exec/** : This directory contains information about instance's execution context,
only exists while the instance is running.

+ **exec/resolved_url**: The component's resolved URL in text format.

+ **exec/in/**: The instance's incoming namespace, as supplied by the
component manager. This contains a listing of services and directories
accessible to the given component instance. A component can connect directly to
these services from the Hub by opening the provided path.

+ **exec/expose/** : The instance's exposed services as listed in its
manifest file. A component can connect directly to these services from the Hub
by opening the provided path.

+ **exec/out/** : The instance's outgoing namespace, served by the
instance itself. A component can connect directly to these services from the Hub by opening the
provided path.

+ **exec/runtime/**: Information about the instance's runtime environment
supplied by its runner (e.g. ELF runner, Dart runner), organized by topic.

+ **exec/runtime/elf/**: Information about the instance's main process
and job (if it has one) if it was started by the elf runner.

+ **exec/runtime/elf/process-id**: The instance's process id in text
format.

+ **exec/runtime/elf/job-id**: The instance's job id in text format.


+ **exec/runtime/elf/args/**: A directory of command-line arguments.
These arguments are presented as a series of files from `0` onward.

## [Capability Routing](#capability-routing)

You can make the **/hub** directory available in any component's incoming namespace
with the appriorate declaration in the component's manifest.

### Basic Example

In this example above, the component, `hub_client` has requested the hub in its
namespace from the `framework`. The `framework` provides `hub_client` with a hub
directory rooted at `hub_client`. In other words, `hub_client` cannot inspect
information about component instances above it in the component hierarchy.

```
// In hub_client.cml.
{
    "program": {
        "binary": "bin/hub_client",
    },
    "use": [
        {
            "directory": "/hub", "from": "framework"
        }
    ],
}
```

### Offering Parent Hub

In this example, the parent component instance passes its view of the Hub to
`hub_client` which then maps it as `/parent_hub` in its namespace. `hub_client`
can inspect information about its parent and siblings through `/parent_hub`.

In the parent component manifest:

```
{
    // Route the root Hub to hub_client.
    "offer": [
        {
          "directory": "/hub",
          "from": "framework",
          "to": [
            {
              "dest": "#hub_client",
            },
          ],
        },
    ],
    "children": [
        {
            "name": "hub_client",
            "url": "fuchsia-pkg://fuchsia.com/hub_test#meta/hub_client.cm",
            "startup": "eager",
        },
    ],
```

In `hub_client.cml`:

```
{
    "program": {
        "binary": "bin/hub_client",
    },
    "use": [
        {
          "directory": "/hub",
          "from": "realm",
          "as": "/parent_hub"
        }
    ]
}
```

### Exposing a sibling Hub

In this example, `hub_client_sibling` exposes its view of the Hub to its
containing realm. The realm, in turn, offers that view of the Hub as
`\sibling_hub` to `hub_client`. `hub_client` maps that view of the Hub to its
incoming namespace.

In `hub_client_sibling.cml`:

```
{
    "program": {
        "binary": "bin/hub_client_sibling",
    },
    "expose": [
        {
            "directory": "/hub",
            "from": "framework",
        },
    ],
}
```

In the parent component manifest file:

```
{
    // Route hub_client_sibling's view of the Hub to hub_client.
    "offer": [
        {
            "directory": "/hub",
            "from": "#hub_client_sibling",
            "to": [
              {
                "dest": "#hub_client",
                "as": "/sibling_hub",
              }
            ]
        }
    ],
    "children": [
        {
            "name": "hub_client_sibling",
            "url": "fuchsia-pkg://fuchsia.com/hub_test#meta/hub_client_sibling.cm",
            "startup": "eager",
        },
        {
            "name": "hub_client",
            "url": "fuchsia-pkg://fuchsia.com/hub_test#meta/hub_client.cm",
            "startup": "eager",
        },
    ],
}
```

In hub_client.cml:

```
{
    "program": {
        "binary": "bin/hub_client",
    },
    "use": [
        {
            "directory": "/sibling_hub", "from": "realm",
        }
    ]
}
```
