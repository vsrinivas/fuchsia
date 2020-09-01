# Directory capabilities

[Directory capabilities][glossary-directory] allow components to connect to
directories provided by other components.

## Creating directory capabilities

When a component wants to make one of its directories available to other
components, it specifies the path of that directory in its
[outgoing directory][glossary-outgoing] in one of the following ways:

-   [Exposing][expose] the directory to the parent.

    ```
    {
        "expose": [{
            "directory": "/data",
            "from": "self",
            "rights": ["r*"],
        }],
    }
    ```

-   Or [offering][offer] the directory to some of the component's children with
    read-write rights.

    ```
    {
        "offer": [{
            "directory": "/data",
            "from": "self",
            "rights": ["rw*"],
            "to": [{
                { "dest": "#child-a" },
                { "dest": "#child-b" },
            }],
        }],
    }
    ```

## Consuming directory capabilities

When a component wants to make use of a directory from its parent, it does so by
[using][use] the directory. This will make the directory accessible from the
component's [namespace][glossary-namespace].

This example shows a directory named `/data` that is included in the component's
namespace. If the component instance accesses this directory during its
execution, the component framework performs
[capability routing][capability-routing] to find the component that provides it.
Then, the framework connects the directory from the component's namespace to
this provider.

```
{
    "use": [{
        "directory": "/data",
        "rights": ["r*"],
    }],
}
```

See [`//examples/components/routing`][routing-example] for a working example of
routing a directory capability from one component to another.

## Directory capability rights

As directories are [offered][offer] and [exposed][expose] throughout the system
a user may want to restrict what components who have access to this directory
may do. For example, a component could expose a directory as read-write to its
parent realm which could expose that directory it to its children as read-write
but to its parent as read-only.

[Directory rights][directory-rights] allow any directory declaration to specify
a rights field that indicates the set of rights that the directory would like to
[offer][offer], [expose][expose] or [use][use].

### Example

This example shows component `A` requesting access to `/data` from its namespace
with read-write rights:

```
A.cml
{
    "use": [{
        "directory": "/data",
        "rights": ["rw*"],
    }],
}
```

Furthermore, parent component `B` offers the directory `/data` to component A
but with only read-only rights. In this case the routing fails and `/data`
wouldn't be present in A's namespace.

```
B.cml
{
  "offer": [{
      "directory": "/data",
      "from": "self",
      "rights": ["r*"],
      "to": [{
        { "dest": "#A"}
      }],
  }],
}
```

### Inference Rules

Directory rights are required in the following situations:

-   [use][use] - All directories use statements must specify their directory
    rights.
-   [offer][offer] and [expose][expose] from `self`
-   All directories that are provided by components must specify their directory
    rights.

If an expose or offer directory declaration does not specify optional rights, it
will inherit the rights from the source of the expose or offer. Rights specified
in a `use`, `offer`, or `expose` declaration must be a subset of the rights set
on the capability's source.

### Framework directory capabilities

Some directory capabilities are available to all components through the
framework. When a component wants to use one of these directories, it does so by
[using][use] the directory with a source of `framework`.

```
{
    "use": [{
        "directory": "/hub",
        "from": "framework",
        "rights": ["r*"],
    }],
}
```

## Directory paths

The paths used to refer to directories can be renamed when being
[offered][offer], [exposed][expose], or [used][use].

In the following example, there are three components, `A`, `B`, and `C`, with
the following layout:

```
 A  <- offers directory "/data" from "self" to B as "/intermediary"
 |
 B  <- offers directory "/intermediary" from "realm" to B as "/intermediary2"
 |
 C  <- uses directory "/intermediary2" as "/config"
```

Each component in this example changes the path used to reference the directory
when passing it along in this chain. When component `C` accesses the `/config`
directory in its namespace, it will be connected to directory `/data` in
component `A`'s outgoing directory.

```
A.cml:
{
    "offer": [{
        "directory": "/data",
        "from": "self",
        "to": [{
            { "dest": "#B", "as": "/intermediary" },
        }],
        "rights": ["rw*"],
    }],
    "children": [{
        "name": "B",
        "url": "fuchsia-pkg://fuchsia.com/B#meta/B.cm",
    }],
}
```

```
B.cml:
{
    "offer": [{
        "directory": "/intermediary",
        "from": "self",
        "to": [{
            { "dest": "#C", "as": "/intermediary2" },
        }],
        "rights": ["r*"],
    }],
    "children": [{
        "name": "C",
        "url": "fuchsia-pkg://fuchsia.com/C#meta/C.cm",
    }],
}
```

```
C.cml:
{
    "use": [{
        "directory": "/intermediary2",
        "as": "/config",
    }],
}
```

If any of the names didn't match in this chain, any attempts by `C` to list or
open items in this directory would fail.

[capability-routing]: ../component_manifests.md#capability-routing
[directory-rights]: ../component_manifests.md#directory-rights
[expose]: ../component_manifests.md#expose
[glossary-directory]: /docs/glossary.md#directory-capability
[glossary-fidl]: /docs/glossary.md#fidl
[glossary-namespace]: /docs/glossary.md#namespace
[glossary-outgoing]: /docs/glossary.md#outgoing-directory
[offer]: ../component_manifests.md#offer
[routing-example]: /examples/components/routing
[use]: ../component_manifests.md#use
