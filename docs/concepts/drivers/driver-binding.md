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
([driver/binding.h](/zircon/system/public/zircon/driver/binding.h)) are available to be extended.
These keys are hardcoded into the bind compiler and are available under the `deprecated` namespace.
For example, the PCI vendor ID key is `deprecated.BIND_PCI_VID`. Eventually this namespace will be
removed and all bind property keys will be defined in bind libraries.

## The compiler

The compiler takes a list of library sources, and one program source. For example:

```
bindc --include pci.lib,usb.lib --output gizmo.h gizmo.bind
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

## Bind rules

A bind program defines the conditions to call a driver's `bind()` hook. Each statement in the bind
program is a condition over the properties of the device that must hold true in order for the
driver to bind. If the bind rules finish executing and all conditions are true, then the device
coordinator will call the driver's `bind()` hook.

There are four kinds of statements:

 - **Condition statements** are equality (or inequality) expressions of the form
   `<key> == <value>` (or `<key> != <value>`).
 - **Accept statements** are lists of permissable values for a given key.
 - **If statements** provide simple branching.
 - **Abort statements** cause the bind rule execution to terminate and the driver will not bind.

### Example

```
using deprecated.usb;

// The device must be a USB device.
deprecated.BIND_PROTOCOL == deprecated.usb.BIND_PROTOCOL.DEVICE;

if (deprecated.BIND_USB_VID == deprecated.usb.INTEL) {
  // If the device's vendor is Intel, the device ID must be one of the following values:
  accept deprecated.BIND_USB_DID {
    1337,
    0xcafe,
  };
} else if (deprecated.BIND_USB_VID == deprecated.usb.REALTEK) {
  // If the device's vendor is Realtek, the device class must be audio.
  deprecated.BIND_USB_CLASS = deprecated.usb.BIND_USB_CLASS.AUDIO;
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
`0x[0-9A-F]+.`

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

## Bind libraries

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
`0x[0-9A-F]+.`

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
