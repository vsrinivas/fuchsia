# Write bind rules for a driver

This guide walks through the steps for writing bind rules for the
[`i2c_temperature`][i2c-temperature-sample-driver] sample driver.

For a driver to bind to a [node][drivers-and-nodes] (which represents a hardware or
virtual device), the driver’s bind rules must match the bind properties of the node. In
this guide, we’ll write bind rules for the `i2c_temperature` sample driver so that they
match the bind properties of the `i2c-child` node.

The `test_i2c_controller` driver creates a child node named `i2c-child` for testing the
`i2c_temperature` sample driver. We can use this `test_i2c_controller` driver to identify
the bind properties of the `i2c-child` node for writing `i2c_temperature`’s bind rules.

Before you begin, writing bind rules requires familiarity with the concepts
in [Driver binding][driver-binding].

The steps are:

1.  [Identify the bind properties](#identify-bind-properties).
2.  [Write the bind rules](#write-bind-rules).
3.  [Add a Bazel build target for the bind rules](#add-a-bazel-build-target).

## 1. Identify the bind properties {:#identify-bind-properties}

You can identify the bind properties of your target node in one of the following ways:

* [Use the ffx driver list-devices command](#use-ffx-driver-list-devices)
* [Look up the bind properties in the driver source code](#look-up-the-driver-source-code)

### Use the ffx driver list-devices command {:#use-ffx-driver-list-devices}

To print the properties of every node in the Fuchsia system, run the following command:

```posix-terminal
ffx driver list-devices -v
```

This command prints the properties of a node in the following format:

```none {:.devsite-disable-click-to-copy}
Name     : i2c-child
Moniker  : root.sys.platform.platform-passthrough.acpi.acpi-FWCF.i2c-child
Driver   : None
3 Properties
[ 1/  3] : Key fuchsia.BIND_PROTOCOL          Value 0x000018
[ 2/  3] : Key fuchsia.BIND_I2C_ADDRESS       Value 0x0000ff
[ 3/  3] : Key "fuchsia.driver.framework.dfv2" Value true
```

The output above shows that the `i2c-child` node has the following bind properties:

*   Property key `fuchsia.BIND_PROTOCOL` with an integer value of `24`
    (`0x000018`)
*   Property key `fuchsia.BIND_I2C_ADDRESS` with an integer value of `0xFF`.

### Look up the bind properties in the driver source code {#look-up-the-driver-source-code}

When adding a child node, drivers can provide bind properties to the node. So if you view the
source code of the driver that creates your target node as a child node, you can identify the
bind properties for which you want to write the bind rules.

For instance, the `test_i2c_controller` driver creates a child node named `i2c-child` which we
want the `i2c_temperature` sample driver to bind to. So we’ll examine the source code of the
`test_i2c_controller` driver to identify which bind properties are passed to this child node.

In [`test_i2c_controller.cc`][test-i2c-controller-cc], the code below adds a child node
named `i2c-child`:

```none {:.devsite-disable-click-to-copy}
// Set the properties of the node that a driver will bind to.
auto properties = fidl::VectorView<fdf::wire::NodeProperty>(arena, 2);
properties[0] = fdf::wire::NodeProperty::Builder(arena)
             .key(fdf::wire::NodePropertyKey::WithStringValue(
                  arena, bind_fuchsia_hardware_i2c::DEVICE))
             .value(fdf::wire::NodePropertyValue::WithEnumValue(
                  arena, bind_fuchsia_hardware_i2c::DEVICE_ZIRCONTRANSPORT))
              .Build();

properties[1] = fdf::wire::NodeProperty::Builder(arena)
      .key(fdf::wire::NodePropertyKey::WithIntValue(0x0A02 /* BIND_I2C_ADDRESS */))
      .value(fdf::wire::NodePropertyValue::WithIntValue(0xff))
       .Build();

auto args = fdf::wire::NodeAddArgs::Builder(arena)
              .name(arena, "i2c-child")
              .offers(fidl::VectorView<fcd::wire::Offer>::FromExternal(&offer, 1))
              .properties(properties)
               .Build();
```

This code shows that the `i2c-child` node is created with the following two bind properties:

* Property key `fuchsia.BIND_PROTOCOL` with an integer value of `24`.
* Property key `fuchsia.BIND_I2C_ADDRESS` with an integer value of `0xFF`.

(For more information on the `NodeAddArgs` struct used to pass the bind properties to
a child node, see the [NodeProperty and NodeAddArgs structs](#nodeproperty-and-nodeaddargs-structs)
section in Appendices.)

## 2. Write the bind rules {:#write-bind-rules}

Once you know the bind properties you want to match, you can use the bind language to write
the bind rules for your driver (for instance, the `i2c_temperature` sample driver in this case).

In the previous section, we’ve identified that the `i2c-child` node has the following bind
properties:

* Property key `fuchsia.BIND_PROTOCOL` with an integer value of `24`.
* Property key `fuchsia.BIND_I2C_ADDRESS` with an integer value of `0xFF`.

The [`i2c_temperature.bind`][i2c-temperature-bind] file shows that the following bind rules are
written for these bind properties:

```none {:.devsite-disable-click-to-copy}
using fuchsia.i2c;

fuchsia.BIND_PROTOCOL == fuchsia.i2c.BIND_PROTOCOL.DEVICE;
fuchsia.BIND_I2C_ADDRESS == 0xFF;
```

Integer-based bind property keys that start with `BIND_` (defined in
[`binding_priv.h`][binding-prev-h] in the Fuchsia source tree) are old property keys currently
hardcoded in the bind compiler. When these keys are used in bind rules, they are prefixed with
`fuchsia.`. Hence, `BIND_PROTOCOL` becomes `fuchsia.BIND_PROTOCOL` and `BIND_I2C_ADDRESS`
becomes `fuchsia.BIND_I2C_ADDRESS`.

The property values of `fuchsia.BIND_PROTOCOL` are defined in
[`protodefs.h`][protodefs-h]. From the file, we can discover that the protocol that maps to
an integer value of `24` is `i2c`:

```none {:.devsite-disable-click-to-copy}
DDK_PROTOCOL_DEF(I2C,                     24,   "i2c", 0)
```

We can then navigate to the [`fuchsia.i2c`][fuchsia-i2c-bind-library] bind library
and find the list of the `BIND_PROTOCOL` property values:

```none {:.devsite-disable-click-to-copy}
extend uint fuchsia.BIND_PROTOCOL {
  DEVICE = 24,
  IMPL = 25,
};
```

This gives us the property value of `fuchsia.i2c.BIND_PROTOCOL.DEVICE` for the
property key of `fuchsia.BIND_PROTOCOL` in the bind rules above.

## 3. Add a Bazel build target for the bind rules {:#add-a-bazel-build-target}

Once you have a file that contains the bind rules for your driver (`i2c_temperature`), you need
to update the `BUILD.bazel` file to add a build target for the bind rules.

To add a Bazel build target for the bind rules, use the following template:

```
fuchsia_driver_bytecode_bind_rules(
  name = <target name>,
  output = "<bind rules driver>.bindbc",
  rules = <bind rules filename>,
  deps = [
    <bind library dependencies>
  ],
)
```

Using the `i2c_temperature.bind` file, the Bazel build target for the `i2c_temperature` sample
driver's bind rules may look like below:

```none {:.devsite-disable-click-to-copy}
fuchsia_driver_bytecode_bind_rules(
  name = "bind_bytecode",
  output = "i2c_temperature.bindbc",
  rules = "i2c_temperature.bind",
  deps = [
    "@fuchsia_sdk//fidl/fuchsia.hardware.i2c:fuchsia.hardware.i2c_bindlib",
  ],
)
```

For each library used in your bind rules, you need to add the library as a dependency to
the build target. The `i2c_temperature` sample driver's bind rules use the
`fuchsia.hardware.i2c` bind library, so it needs to be included as a dependency in `deps`.

To determine which bind libraries are used in the bind rules, you can examine the driver
source code. In the bind properties of the `i2c-child` node, the first property key
`fuchsia.hardware.i2c.Device` is from a generated bind library from the FIDL protocol. This
information can be derived from examining the following driver source code:

```none {:.devsite-disable-click-to-copy}
properties[0] = fdf::wire::NodeProperty::Builder(arena)
            .key(fdf::wire::NodePropertyKey::WithStringValue(
                  arena, bind_fuchsia_hardware_i2c::DEVICE))
            .value(fdf::wire::NodePropertyValue::WithEnumValue(
                  arena, bind_fuchsia_hardware_i2c::DEVICE_ZIRCONTRANSPORT))
            .Build();
```

The prefix `fuchsia.hardware.i2c` implies that this bind property’s key and value are defined
in the following header:

```none {:.devsite-disable-click-to-copy}
#include <bind/fuchsia/hardware/i2c/cpp/bind.h>
```

Also, in the [`BUILD.bazel`][build-bazel] file, notice that the target `test_i2c_controller`
contains the dependency `@fuchsia_sdk//fidl/fuchsia.hardware.i2c:fuchsia.hardware.i2c_bindlib_cc`,
which can be seen in the `cc_binary` build target below:

```none {:.devsite-disable-click-to-copy}
cc_binary(
  name = "test_controller",
  srcs = [
    "constants.h",
    "test_i2c_controller.cc",
    "test_i2c_controller.h",
  ],
  linkshared = True,
  deps = [
    "@fuchsia_sdk//fidl/fuchsia.device.fs:fuchsia.device.fs_llcpp_cc",
    "@fuchsia_sdk//fidl/fuchsia.driver.compat:fuchsia.driver.compat_llcpp_cc",
    "@fuchsia_sdk//fidl/fuchsia.hardware.i2c:fuchsia.hardware.i2c_bindlib_cc",
  ...
```

The prefix `@fuchsia_sdk//fidl/` in the dependency means that the values are pulled from a bind
library that is generated from the `fuchsia.hardware.i2c` FIDL interface. (For more information
on the generated bind library, see the [FIDL tutorial][fidl-tutorial] for drivers.)

## Appendices

### NodeProperty and NodeAddArgs structs {:#nodeproperty-and-nodeaddargs-structs}

Bind properties are represented by the `NodeProperty` struct in the `fuchsia.driver.framework`
FIDL library:

```none {:.devsite-disable-click-to-copy}
/// Definition of a property for a node. A property is commonly used to match a
/// node to a driver for driver binding.
type NodeProperty = table {
    /// Key for the property.
    1: key NodePropertyKey;

    /// Value for the property.
    2: value NodePropertyValue;
};
```

Then the bind properties are passed to a child node using the `NodeAddArgs` struct:

```none {:.devsite-disable-click-to-copy}
/// Arguments for adding a node.
type NodeAddArgs = table {
    /// Name of the node.
    1: name string:MAX_NODE_NAME_LENGTH;

    /// Capabilities to offer to the driver that is bound to this node.
    2: offers vector<fuchsia.component.decl.Offer>:MAX_OFFER_COUNT;

    /// Functions to provide to the driver that is bound to this node.
    3: symbols vector<NodeSymbol>:MAX_SYMBOL_COUNT;

    /// Properties of the node.
    4: properties vector<NodeProperty>:MAX_PROPERTY_COUNT;
};
```

<!-- Reference links -->

[i2c-temperature-sample-driver]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/i2c_temperature/
[drivers-and-nodes]: /docs/concepts/drivers/drivers_and_nodes.md
[driver-binding]: /docs/concepts/drivers/driver_binding.md
[test-i2c-controller-cc]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/i2c_temperature/test_i2c_controller.cc
[binding-prev-h]: /src/lib/ddk/include/lib/ddk/binding_priv.h
[protodefs-h]: /src/lib/ddk/include/lib/ddk/protodefs.h
[fuchsia-i2c-bind-library]: /src/devices/bind/fuchsia.i2c/fuchsia.i2c.bind
[fidl-tutorial]: /docs/development/drivers/tutorials/fidl-tutorial.md
[i2c-temperature-bind]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/i2c_temperature/i2c_temperature.bind
[build-bazel]: https://fuchsia.googlesource.com/sdk-samples/drivers/+/refs/heads/main/src/i2c_temperature/BUILD.bazel
