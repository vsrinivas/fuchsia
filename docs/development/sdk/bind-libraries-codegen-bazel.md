# Bind library code generation tutorial for Bazel

This tutorial is for driver authors developing out-of-tree using the SDK who want to use the
bind library code generation feature that has been explained in detail on this page:

 * [Bind library code generation tutorial](/docs/development/drivers/tutorials/bind-libraries-codegen.md)

This guide assumes familiarity with the concepts from that page.

## What is the same and what is different?

Most of the concepts and samples laid out in the linked tutorial will also apply to users of
the SDK.

The only differences are:

 * No Rust target is generated currently.
 * Instead of `:{target_name}_cpp` the C++ library (`cc_library`) target is `:{target_name}_cc`.
 * The Bazel target for the C++ library `fuchsia_bind_cc_library` will need to be manually added
   if the bind library is not from the SDK.

## An example

This example will show a manually defined bind library with a dependency on an SDK bind library
so that it can show using both types. The bind libraries will be used in a child driver's
bind rules, and the C++ library from the bind libraries will be used in a parent driver's code
when creating a child node for the child driver to bind to.

### BUILD.bazel

#### The bind library

```bazel {:.devsite-disable-click-to-copy}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/bind_library/BUILD.bazel" region_tag="fuchsia_gizmo_library" %}
```

#### The parent driver

```bazel {:.devsite-disable-click-to-copy}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/bind_library/BUILD.bazel" region_tag="parent_driver" highlight="9,10" %}
```

#### The child bind rules

```bazel {:.devsite-disable-click-to-copy}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/bind_library/BUILD.bazel" region_tag="bind_rules" highlight="6,7" %}
```

### parent-driver.cc

```cpp {:.devsite-disable-click-to-copy}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/bind_library/parent-driver.cc" region_tag="bind_imports" highlight="2" %}

{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/bind_library/parent-driver.cc" region_tag="add_properties" adjust_indentation="auto" highlight="2,3,7" %}
```

### child-driver.bind

```none {:.devsite-disable-click-to-copy}
{% includecode gerrit_repo="fuchsia/sdk-samples/drivers" gerrit_path="src/bind_library/child-driver.bind" region_tag="bind_rules" highlight="1,3,6,7,9" %}
```
