Inspect Quick Start
=====


[TOC]

This document is a guide on how to get started with [Component Inspection](README.md) depending on your needs:

**I want to learn more about Inspect concepts**

Read [Getting Started with Inspect](gsw-inspect.md) and the [README](README.md).

**I want to know how to use the iquery tool**

Read the [iquery manual](iquery.md). For detailed examples of usage, see [Getting Started with Inspect](gsw-inspect.md).

This document provides a simplified example of iquery below.

**I have an existing or new component, and I want to support inspection.**

Continue reading this document.

# Quick Start

See below for the quick start guide in your language of choice.

## C++

### Setup
> NOTE: If you need to support dynamic values, see [Dynamic Value Support](#dynamic-value-support). If you are unsure, keep reading.

This section assumes you are writing an asynchronous component and that some part of your component (typically `main.cc`) looks like this:

```
async::Loop loop(&kAsyncLoopConfigAttachToThread);
auto component_context = sys::ComponentContext::Create();
// ...
loop.Run();
```

This sets up an async loop, creates a `ComponentContext` wrapping handles provided by the runtime, and then runs that loop following some other initialization work.

**Change your initialization code to look like the following:**
```
async::Loop loop(&kAsyncLoopConfigAttachToThread);
auto component_context = sys::ComponentContext::Create();
auto inspector =
      inspect::ComponentInspector::Initialize(component_context.get());
inspect::Node& root_node = inspector->root_tree()->GetRoot();
// ...
loop.Run();
```

> Note: You will need to `#include <lib/inspect/component.h>`

You are now using Inspect! To add some data and see it in action, try adding the following:
```
// Important: Make sure to hold on to hello_world_property and don't let it go out of scope.
auto hello_world_property = root_node.CreateStringProperty("hello", "world");
```

See [Viewing Inspect Data](#viewing-inspect-data) below to view what you are now exporting.

See [Supported Data Types](#supported-data-types) for a full list of data types you can try.

Want to test your Inspect integration? Include [testing/inspect.h](/garnet/public/lib/inspect/testing/inspect.h)
in your unit test for a full set of matchers. See [this example](/garnet/public/lib/inspect/tests/inspect_vmo_unittest.cc)
of how it is used.

Read on to learn how Inspect is meant to be used in C++.

#### Dynamic Value Support

Certain features, such as LazyProperty, LazyMetric, and ChildrenCallback are deprecated, but a replacement is on the way (CF-761). If you determine that you need one of these data types, you may use the deprecated API by replacing the setup code with the following:
```
// Legacy work required to expose an inspect hierarchy over FIDL.
auto root = component::ObjectDir::Make("root");
fidl::BindingSet<fuchsia::inspect::Inspect> inspect_bindings_;
component_context->outgoing()->GetOrCreateDirectory("objects")->AddEntry(
    fuchsia::inspect::Inspect::Name_,
    std::make_unique<vfs::Service>(
        inspect_bindings_.GetHandler(root.object().get())));
auto root_node = inspect::Node(root);
```

### C++ Library Concepts

Now that you have a `root_node` you may start building your hierarchy. This section describes some important concepts and patterns to help you get started.

* A Node may have any number of key/value pairs called **Values**.
* A numeric Value is called a **Metric** and a non-numeric Value is called a **Property**.
* A Node may have any number of children, which are also Nodes.

The code above gives you access to a single node named "root". `hello_world_property` is a Property that contains a string value (aptly called a **StringProperty**).

* Values and Nodes are created under a parent Node.

Class `Node` has creator methods for every type of supported value. `hello_world_property` was created using `CreateStringProperty`. You could create a child under the root node by calling `root_node.CreateChild("child name")`. Note that names must always be UTF-8 strings.

* Values and Nodes have strict ownership semantics.

`hello_world_property` owns the Property. When it is destroyed (goes out of scope) the underlying Property is deleted and no longer present in your component's Inspect output. This is true for Metrics and child Nodes as well.

* Inspection is best-effort.

Due to space limitations, the Inspect library may be unable to satisfy a `Create` request. This error is not surfaced to your code: you will receive a Node/Metric/Property object for which the methods are no-ops.

* Pattern: Pass in child Nodes to child objects.

It is useful to add an `inspect::Node` argument to the constructors for your own classes. The parent object, which should own its own `inspect::Node`, may then pass in the result of `CreateChild(...)` to its children when they are constructed:

```
class Child {
  public:
    Child(inspect::Node my_node) : my_node_(std::move(my_node)) {
      // Create metrics and properties on my_node_.
    }

  private:
    inspect::Node my_node_;
    inspect::StringProperty some_property_;
    // ... more properties and metrics
};

class Parent {
  public:
    // ...

    void AddChild() {
      // Note: inspect::UniqueName returns a globally unique name with the specified prefix.
      children_.emplace_back(my_node_.CreateChild(inspect::UniqueName("child-")));
    }

  private:
    std::vector<Child> children_;
    inspect::Node my_node_;
};
```

## Rust

> Rust support for inspect is currently in development.
>
> TODO(crjohns,miguelfrde)

## Dart

This example obtains and adds several data types and nested children to the
root Inspect node.

BUILD.gn:
```
flutter_app("inspect_mod") {
[...]
  deps = [
    [...]
    "//topaz/public/dart/fuchsia_inspect",
    [...]
  ]
[...]

```
root_intent_handler.dart:
```dart {highlight="lines:6"}
import 'package:fuchsia_inspect/inspect.dart' as inspect;
[...]
class RootIntentHandler extends IntentHandler {
  @override
  void handleIntent(Intent intent) {
    var inspectNode = inspect.Inspect().root;
    runApp(InspectExampleApp(inspectNode));
  }
}
```
inspect_example_app.dart:
```dart {highlight="lines:4,7-10,16"}
import 'package:fuchsia_inspect/inspect.dart' as inspect;

class InspectExampleApp extends StatelessWidget {
  final inspect.Node _inspectNode;

  InspectExampleApp(this._inspectNode) {
    _inspectNode.stringProperty('greeting').setValue('Hello World');
    _inspectNode.doubleProperty('double down')..setValue(1.23)..add(2);
    _inspectNode.intProperty('interesting')..setValue(123)..subtract(5);
    _inspectNode.byteDataProperty('bytes').setValue(ByteData(4)..setUint32(0, 0x01020304));
  }
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: _InspectHomePage(
          inspectNode: _inspectNode.child('home-page')),
      [...]
  }
```
You can call `delete()` on a Node or Property when you're done with it.
 Deleting a node deletes everything under it.

`delete()` can also be triggered by a Future completing or broadcast Stream
closing:
```dart
var answerFuture = _answerFinder.getTheAnswer();
var wait = _inspectNode.stringProperty('waiting')..setValue('for a hint');
answerFuture.whenComplete(wait.delete);

stream.listen((_) {}, onDone: node.delete);

// FIDL proxies contain a future that completes when the connection closes:
final _proxy = my_fidl_import.MyServiceProxy();
_proxy.ctrl.whenClosed.whenComplete(node.delete);

```

# Viewing Inspect Data

You can use the [`iquery`](iquery.md) tool to view the Inspect data you exported from your component by looking through the Hub.

This section assumes you have SSH access to your running Fuchsia system and that you started running your component. We will use the name `my_component.cmx` as a placeholder for the name of your component.

## Find your Inspect endpoint

Try the following:
```
# This prints all Inspect endpoints on the system.
$ iquery --find /hub

# This filters the above list to only print your component.
$ iquery --find /hub | grep my_component.cmx
```

> Under the listed directories you will see some paths including "system\_objects." This Inspect data is placed there by the Component Runtime itself.

Your component's endpoint will be listed as `<path>/my_component.cmx/<id>/out/objects/root.inspect`.

> Note: If you followed [Dynamic Value Support](#dynamic-value-support) above, "root.inspect" will be missing.

## Read your Inspect data

Navigate to the `out/objects` directory that was printed above, and run:
```
$ iquery --recursive root.inspect

# OR, if you used Dynamic Values:
$ iquery --recursive .
```

This will print out the following if you followed the suggested steps above:
```
root:
  hello = world
```

# Supported Data Types

Type | Description | Notes
-----|-------------|-------
  IntMetric | A metric containing a signed 64-bit integer. | All Languages
  UIntMetric | A metric containing an unsigned 64-bit integer. | Not supported in Dart
  DoubleMetric | A metric containing a double floating-point number. | All Languages
  {Int,Double,UInt}Array | An array of metric types, includes typed wrappers for various histograms. | Same language support as base metric type
  StringProperty | A property with a UTF-8 string value. | All Languages
  ByteVectorProperty | A property with an arbitrary byte value. | All Languages
  Node | A node under which metrics, properties, and more nodes may be nested. | All Languages
  LazyMetric | A metric which dynamically sets its value as the result of a callback. | DEPRECATED: C++ Only
  LazyProperty | A property which dynamically sets its value as the result of a callback. | DEPRECATED: C++ Only
  ChildrenCallback | A callback that dynamically injects children into a Node on demand. | DEPRECATED: C++ Only
  Link | Instantiates a complete tree of Nodes dynamically. | IN PROGRESS(CF-761): This will replace Lazy metrics, properties, and children

