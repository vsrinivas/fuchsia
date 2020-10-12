# Driver Binding

In Fuchsia, the driver framework maintains a tree of drivers and devices in the system. In this
tree, a device represents access to some hardware available to the OS. A driver both publishes and
binds to devices. For example, a USB driver might bind to a PCI device (its parent) and publish an
ethernet device (its child). In order to determine which devices a driver can bind to, each driver
has a bind program and each device has a set of properties. The bind program defines a condition
that matches the properties of devices that it wants to bind to. For more details, see the
[Zircon Driver Development Kit](/docs/concepts/drivers/overview.md) documentation.

Bind programs and the conditions they refer to are defined by a domain specific language. The bind
compiler consumes this language and produces bytecode for bind programs. In the future, it will
also produce code artefacts that drivers may refer to when publishing device properties. The
language has two kinds of source files: programs, and libraries. Libraries are used to share
property definitions between drivers and bind programs.

Note: Driver binding is under active development and this document describes the current state.
Not all drivers use this form of bind rules but a migration is under way to convert them all.

One thing to note about this stage of the migration is that there is no support for defining device
property keys in bind libraries (see below). Instead, the keys from the old driver binding system
([ddk/binding.h](/src/lib/ddk/include/ddk/binding.h)) are available to be extended.
These keys are hardcoded into the bind compiler and are available under the `fuchsia` namespace.
For example, the PCI vendor ID key is `fuchsia.BIND_PCI_VID`. Eventually the hardcoded keys will be
removed from this namespace and all bind property keys will be defined in bind libraries.


## The compiler

The compiler takes a list of library sources, and one program source. For example:

```
fx bindc \
  --include src/devices/bind/fuchsia.usb/fuchsia.usb.bind \
  --output tools/bindc/examples/gizmo.h \
  tools/bindc/examples/gizmo.bind
```

Currently, it produces a C header file that may be included by a driver. The header file defines a
macro:

```
ZIRCON_DRIVER(Driver, Ops, VendorName, Version)
```
 - `Driver` is the name of the driver.
 - `Ops` is a `zx_driver_ops`, which are the driver operation hooks
 - `VendorName` is a string representing the name of the driver vendor.
 - `Version` is a string representing the version of the driver.

For more details, see [the driver development documentation]
(/docs/concepts/drivers/driver-development).

## Bind rules {#bind-rules}

A bind program defines the conditions to call a driver's `bind()` hook. Each statement in the bind
program is a condition over the properties of the device that must hold true in order for the
driver to bind. If the bind rules finish executing and all conditions are true, then the device
coordinator will call the driver's `bind()` hook.

There are four kinds of statements:

 - **Condition statements** are equality (or inequality) expressions of the form
   `<key> == <value>` (or `<key> != <value>`).
 - **Accept statements** are lists of permissible values for a given key.
 - **If statements** provide simple branching.
 - **Abort statements** cause the bind rule execution to terminate and the driver will not bind.

### Example

This example bind program can be found at [//tools/bindc/examples/gizmo.bind](/tools/bindc/examples/gizmo.bind).

```
using fuchsia.usb;

// The device must be a USB device.
fuchsia.BIND_PROTOCOL == fuchsia.usb.BIND_PROTOCOL.DEVICE;

if fuchsia.BIND_USB_VID == fuchsia.usb.BIND_USB_VID.INTEL {
  // If the device's vendor is Intel, the device class must be audio.
  fuchsia.BIND_USB_CLASS == fuchsia.usb.BIND_USB_CLASS.AUDIO;
} else if fuchsia.BIND_USB_VID == fuchsia.usb.BIND_USB_VID.REALTEK {
  // If the device's vendor is Realtek, the device class must be one of the following values:
  accept fuchsia.BIND_USB_CLASS {
    fuchsia.usb.BIND_USB_CLASS.COMM,
    fuchsia.usb.BIND_USB_CLASS.VIDEO,
  }
} else {
  // If the vendor is neither Intel or Realtek, do not bind.
  abort;
}
```

### Language restrictions

There are some restrictions on the language that are imposed to improve readability and ensure that
bind rules are simple representations of the conditions under which a driver should bind.

 - **Empty blocks are not allowed**.
   It's ambiguous whether an empty block should mean that the driver will bind or abort. The
   author should either use an explicit `abort` statement, or refactor the previous `if` statement
   conditions into a condition statement.

 - **If statements must have else blocks and are terminal**.
   This restriction increases readability by making explicit the branches of execution. Since no
   statement may follow an `if` statement, it is easy to trace a path through the bind rules.

### Grammar

```
program = using-list , ( statement )+ ;

using-list = ( using , ";" )* ;

using = "using" , compound-identifier , ( "as" , IDENTIFIER ) ;

statement = condition , ";" | accept | if-statement | abort ;

condition = compound-identifier , condition-op , value ;

condition-op = "==" | "!=" ;

accept = "accept" , compound-identifier , "{" ( value , "," )+ "}" ;

if-statement = "if" , condition , "{" , ( statement )+ , "}" ,
                ( "else if" , "{" , ( statement )+ , "}" )* ,
                "else" , "{" , ( statement )+ , "}" ;

abort = "abort" , ";" ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

value = compound-identifier | STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```

An identifier matches the regex `[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?` and must not match any
keyword. The list of keywords is:

```
abort
accept
as
else
if
using
```

A string literal matches the regex `”[^”]*”`, and a numeric literal matches the regex `[0-9]+` or
`0x[0-9A-F]+`.

The bind compiler will ignore (treat as whitespace) any line prefixed by `//`, and any multiple
lines delimited by `/*` and `*/`.

### Build targets

To declare bind rules within the Fuchsia build system, use the following build target:

```gn
bind_rules("bind") {
  rules = <bind rules filename>
  output = <generated header filename>
  deps = [ <list of bind library targets> ]
}
```

For more details, refer to [//build/bind/bind.gni](/build/bind/bind.gni).

## Testing
The bind compiler supports a data-driven unit test framework for bind rules that allows you to
test your bind rules in isolation from the driver. A test case for a bind program consists of a
device specification and an expected result, i.e. bind or abort. Test cases are passed to the bind
compiler in the form of JSON specification files and the compiler executes each test case by
running the debugger.

The JSON specification must be a list of test case objects, where each object contains:

 - `name` A string for the name of the test case.
 - `expected` The expected result. Must be `“match”` or `“abort”`.
 - `device` A list of string key value pairs describing the properties of a device. This is
   similar to the debugger's [device specifications](#device-specification).

### Example

This is an example test case, the full set of tests is at `//tools/bindc/examples/test.json`. This
case checks that the bind rules match a device with the listed properties, i.e. an Intel USB audio
device.

```
[
  {
    "name": "Intel",
    "expected": "match",
    "device": {
      "fuchsia.BIND_PROTOCOL": "fuchsia.usb.BIND_PROTOCOL.DEVICE",
      "fuchsia.BIND_USB_VID": "fuchsia.usb.BIND_USB_VID.INTEL",
      "fuchsia.BIND_USB_CLASS": "fuchsia.usb.BIND_USB_CLASS.AUDIO"
    }
  }
]
```

### Build

Define a test build target like so

```
bind_test("example_bind_test") {
  rules = <bind rules filename>
  tests = <test specification filename>
  deps = [ <list of bind library targets> ]
}
```

Alternatively, you can simply add a `tests` argument to your existing `bind_rules` to generate a
test target. It’s name will be the original target’s name plus `_test`. For example, the following
would generate `example_bind_test`.

```
bind_rules("example_bind") {
  rules = "gizmo.bind"
  output = “gizmo_bind.h”
  tests = "tests.json"
  deps = [ "//src/devices/bind/fuchsia.usb" ]
}
```

### Run

If you have defined a build target for your test then you can run the tests as usual with fx test.

```
fx test example_bind_test
```

Otherwise you can run the bind tool directly. For example:

```
fx bindc test \
  tools/bindc/examples/gizmo.bind \
  --test-spec tools/bindc/examples/tests.json \
  --include src/devices/bind/fuchsia.usb/fuchsia.usb.bind
```

## Bind libraries {#bind-libraries}

A bind library defines a set of properties that drivers may assign to their children. Also,
bind programs may refer to bind libraries.

### Namespacing

A bind library begins by defining its namespace:

```
library <vendor>.<library>;
```

Every namespace must begin with a vendor and each vendor should ensure that there are no clashes
within their own namespace. However, the language allows for one vendor to extend the library of
another. Google will use `fuchsia` for public libraries.

Any values introduced by a library are namespaced. For example, the following library defines a
new PCI device ID `GIZMO_VER_1`.

```
library gizmotronics.gizmo;

using fuchsia.pci as pci;

extend uint pci.device_id {
  GIZMO_VER_1 = 0x4242,
};
```

To refer to this value the driver author should use the fully qualified name, as follows.

```
using fuchsia.pci as pci;
using gizmotronics.gizmo;

pci.device_id == gizmotronics.gizmo.device_id.GIZMO_VER_1
```

### Keys and values

Device property definitions look similar to variable declarations in other languages.

```
<type> <name>;
Or:
<type> <name> {
  <value>,
  <value>,
  …
};
```

A bind library may also extend properties from other libraries.

```
extend <type> <name> {
  <value>,
  …
};
```

Each key has a type, and all values that correspond to that key must be of that type. The language
supports primitive types: one of `uint`, `string`, or `bool`; and enumerations (`enum`). When
defining keys you should prefer enumerations except when values will be provided by an external
source, such as hardware.

When definining a primitive value use the form `<identifier> = <literal>`, and for enumerations
only an identifier is necessary. It is valid to define multiple primitive values with the same
literal.

### Grammar

```
library = library-header , using-list , declaration-list ;

library-header = "library" , compound-identifier , ";" ;

using-list = ( using , ";" )* ;

using = "using" , compound-identifier , ( "as" , IDENTIFIER ) ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

declaration-list = ( declaration , ";" )* ;

declaration = primitive-declaration | enum-declaration ;

primitive-declaration = ( "extend" ) , type , compound-identifier ,
                        ( "{" primitive-value-list "}" ) ;

type = "uint" | "string" | "bool";

primitive-value-list = ( IDENTIFIER , "=" , literal , "," )* ;

enum-declaration = ( "extend" ) , "enum" , compound-identifier ,
                   ( "{" , enum-value-list , "}" ) ;

enum-value-list = ( IDENTIFIER , "," )* ;

literal = STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```

An identifier matches the regex `[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?` and must not match any
keyword. The list of keywords is:

```
as
bool
enum
extend
library
string
uint
using
```

A string literal matches the regex `”[^”]*”`, and a numeric literal matches the regex `[0-9]+` or
`0x[0-9A-F]+`.

The bind compiler will ignore (treat as whitespace) any line prefixed by `//`, and any multiple
lines delimited by `/*` and `*/`.

### Build targets

To declare a bind library within the Fuchsia build system, use the following build target:

```gn
bind_library(<library name>) {
  source = <bind library filename>
  public_deps = [ <list of bind library targets> ]
}
```

For more details, refer to [//build/bind/bind.gni](/build/bind/bind.gni).

## Debugger

The debugger can be used to run a bind program against a particular device. It
outputs a trace of the bind program's execution, describing why the driver
would or would not bind to the device.

You can run the debugger in the following ways:

 - **[As a host tool.](#running-the-debugger-host)** You provide the bind program
   source file and a file listing the properties of the device. This is useful
   during bind program development for testing the outcome of a program against
   different combinations of device properties.
 - **[On the target device.](#running-the-debugger-target)** You specify the driver
   path and the device path within the system. This is useful for figuring out why
   a driver did or did not bind to a particular device.

Note: The debugger can only be used with bind programs written in the bind language
described in this page.

### Running the debugger as a host tool {#running-the-debugger-host}

You can run the debugger with the `--debug` option in the bind compiler.

```
fx bindc \
  --include src/devices/bind/fuchsia.usb/fuchsia.usb.bind \
  --debug tools/bindc/examples/gizmo.dev \
  tools/bindc/examples/gizmo.bind
```

The bind program source and the library sources are in the formats described in
the [bind rules](#bind-rules) and [bind libraries](#bind-libraries) sections,
respectively. The `--debug` option takes a  file containing a specification of
the device to run the bind program against.

Note: The `--debug` and `--output` options are mutually exclusive, so the C
header file will not be generated when running the compiler in debug mode.

#### Device specification file {#device-specification}

The debugger takes a file specifying the device to run the bind program against.
This specification is simply a list of key-value pairs describing the properties
of the device.

##### Example

This example device specification can be found at
[//tools/bindc/examples/gizmo.dev](/tools/bindc/examples/gizmo.dev).

```
fuchsia.BIND_PROTOCOL = fuchsia.usb.BIND_PROTOCOL.DEVICE
fuchsia.BIND_USB_VID = fuchsia.usb.BIND_USB_VID.REALTEK
fuchsia.BIND_USB_CLASS = fuchsia.usb.BIND_USB_CLASS.VIDEO
fuchsia.BIND_USB_SUBCLASS = fuchsia.usb.BIND_USB_SUBCLASS.VIDEO_CONTROL
```

##### Grammar

```
device-specification = ( property )* ;

property = compound-identifier , "=" , value ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

value = compound-identifier | STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```

An identifier matches the regex `[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?`.

A string literal matches the regex `”[^”]*”`, and a numeric literal matches the
regex `[0-9]+` or
`0x[0-9A-F]+`.

The bind compiler will ignore (treat as whitespace) any line prefixed by `//`,
and any multiple
lines delimited by `/*` and `*/`.

### Running the debugger on the target device {#running-the-debugger-target}

The debugger is run using its package URL. For example:

```
fx run fuchsia-pkg://fuchsia.com/bind_debugger#meta/bind_debugger.cmx \
    /system/driver/bt-hci-intel.so \
    class/bt-transport/000
```

The command takes the path of the driver to debug and the path of the device to
debug it against.

#### Device path

There are two ways to specify the device:

 - Its path within /dev/class, e.g. `class/bt-transport/000`.
 - Its topological path, e.g. `sys/pci/00:14.0/xhci/usb-bus/003/003/ifc-000/bt_transport_usb`.

Both of the paths are relative to /dev/.

The topological path can be determined from the ouptut of `dm dump`. For example,
tracing the path to the node `[bt_transport_usb]` in the output below gives the
topological path `sys/pci/00:14.0/xhci/usb-bus/003/003/ifc-000/bt_transport_usb`.

```
[root]
   <root> pid=3456
      [null] pid=3456 /boot/driver/builtin.so
      [zero] pid=3456 /boot/driver/builtin.so
   [misc]
      <misc> pid=3525
         [demo-fifo] pid=3525 /boot/driver/demo-fifo.so
         [ktrace] pid=3525 /boot/driver/ktrace.so
   [sys]
      <sys> pid=3369 /boot/driver/platform-bus.so
         [pci] pid=3369 /boot/driver/platform-bus-x86.so
            [00:00.0] pid=3369 /boot/driver/bus-pci.so
            [00:14.0] pid=3369 /boot/driver/bus-pci.so
               <00:14.0> pid=4384 /boot/driver/bus-pci.proxy.so
                  [xhci] pid=4384 /boot/driver/xhci.so
                     [xdc] pid=4384 /boot/driver/xhci.so
                     [usb-bus] pid=4384 /boot/driver/usb-bus.so
                        [001] pid=4384 /boot/driver/usb-bus.so
                           [001] pid=4384 /boot/driver/usb-composite.so
                              [ifc-000] pid=4384 /boot/driver/usb-composite.so
                                 [usb-hid] pid=4384 /boot/driver/usb-hid.so
                                    [hid-device-000] pid=4384 /boot/driver/hid.so
                        [002] pid=4384 /boot/driver/usb-bus.so
                           [002] pid=4384 /boot/driver/usb-composite.so
                              [ifc-000] pid=4384 /boot/driver/usb-composite.so
                                 [usb-hid] pid=4384 /boot/driver/usb-hid.so
                                    [hid-device-000] pid=4384 /boot/driver/hid.so
                        [003] pid=4384 /boot/driver/usb-bus.so
                           [003] pid=4384 /boot/driver/usb-composite.so
                              [ifc-000] pid=4384 /boot/driver/usb-composite.so
                                 [bt_transport_usb] pid=4384 /boot/driver/bt-transport-usb.so
                                    [bt_hci_intel] pid=4384 /system/driver/bt-hci-intel.so
                                       [bt_host] pid=4384 /system/driver/bt-host.so
```

### Debugger output

The output of the debugger is a trace of the bind program's execution. The trace
contains information about whether each statement in the bind program succeeded,
and why or why not. For example, if a condition statement failed because the
device did not have the required value, the debugger will output what the actual
value of the device was (or the fact that the device had no value for that
property). The trace also includes information about which branches were taken
in if statements.

#### Example

The output of the debugger when running the host tool command
[above](#running-the-debugger-host) is:

```
Line 4: Condition statement succeeded: fuchsia.BIND_PROTOCOL == fuchsia.usb.BIND_PROTOCOL.DEVICE;
Line 6: If statement condition failed: fuchsia.BIND_USB_VID == fuchsia.usb.BIND_USB_VID.INTEL
    Actual value of `fuchsia.BIND_USB_VID` was `fuchsia.usb.BIND_USB_VID.REALTEK` [0xbda].
Line 9: If statement condition succeeded: fuchsia.BIND_USB_VID == fuchsia.usb.BIND_USB_VID.REALTEK
Line 11: Accept statement succeeded.
    Value of `fuchsia.BIND_USB_CLASS` was `fuchsia.usb.BIND_USB_CLASS.VIDEO` [0xe].
Driver binds to device.
```

If you run the debugger on the Fuchsia target device, you will see similar output
information. However, information such as identifiers and source code snippets may
be missing, since the system only stores the bind program bytecode, not the
source code.

The trace shows the outcome of each statement which was reached while executing
the bind program:

- The device has the USB device protocol, so the first condition statement is
satisfied.
- The device's vendor ID is REALTEK, so the second branch of the if statement is
taken.
- The device has one of the two accepted classes (video), so the accept
statement is satisfied.

The debugger outputs that the driver would successfully bind to a device with
these properties.
