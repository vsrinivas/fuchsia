# Directory capabilities

[Directory capabilities][glossary-directory] allow components to connect to
directories provided by other components.

## Creating directory capabilities

When a component wants to make one of its directories available to other
components, it specifies the path of that directory in its [outgoing
directory][glossary-outgoing] in one of the following ways:

- [Exposing][expose] the directory to the containing realm.

  ```
  {
      "expose": [{
          "directory": "/data",
          "from": "self",
      }],
  }
  ```


- Or [offering][offer] the directory to some of the component's children.

  ```
  {
      "offer": [{
          "directory": "/data",
          "from": "self",
          "to": [{
              { "dest": "#child-a" },
              { "dest": "#child-b" },
          }],
      }],
  }
  ```

## Consuming directory capabilities

When a component wants to make use of a directory from its containing realm, it
does so by [using][use] the directory. This will make the directory accessible
from the component's [namespace][glossary-namespace].

This example shows a directory named `/data` that is included in the component's
namespace. If the component instance accesses this directory during its
execution, the component framework performs [capability
routing][capability-routing] to find the component that provides it. Then, the
framework connects the directory from the component's namespace to this
provider.

```
{
    "use": [{
        "directory": "/data",
    }],
}
```

See [`//examples/components/routing`][routing-example] for a working example of
routing a directory capability from one component to another.

### Framework directory capabilities

Some directory capabilities are available to all components through the
framework. When a component wants to use one of these directories, it does so by
[using][use] the directory with a source of `framework`.

```
{
    "use": [{
        "directory": "/hub",
        "from": "framework",
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
[expose]: ../component_manifests.md#expose
[glossary-directory]: ../../../glossary.md#directory-capability
[glossary-fidl]: ../../../glossary.md#fidl
[glossary-namespace]: ../../../glossary.md#namespace
[glossary-outgoing]: ../../../glossary.md#outgoing-directory
[offer]: ../component_manifests.md#offer
[routing-example]: /examples/components/routing
[use]: ../component_manifests.md#use
