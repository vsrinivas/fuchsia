# Driver Binding

In Fuchsia, the driver framework maintains a tree of drivers and devices in the system. In this
tree, a device represents access to some hardware available to the OS. A driver both publishes and
binds to devices. For example, a USB driver might bind to a PCI device (its parent) and publish an
ethernet device (its child). In order to determine which devices a driver can bind to, each driver
has a bind program and each device has a set of properties. The bind program defines a condition
that matches the properties of devices that it wants to bind to. For more details, see [the DDK
documentation](/docs/concepts/drivers/overview).

Bind programs and the conditions they refer to are defined by a domain specific language. The bind
compiler consumes this language and produces bytecode for bind programs. In the future, it will
also produce code artefacts that drivers may refer to when publishing device propertieis. The
language has two kinds of source files: programs, and libraries. Libraries are used to share
property definitions between drivers and bind programs.

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
 * `Driver` is the name of the driver.
 * `Ops` is a `zx_driver_ops`, which are the driver operation hooks
 * `VendorName` is a string representing the name of the driver vendor.
 * `Version` is a string representing the version of the driver.

For more details, see [the driver development documentation]
(/docs/concepts/drivers/driver-development).

## Bind Programs

A bind program defines the conditions to call a driver's `bind()` hook.

TODO(fxbug.dev/35932): Flesh out this documentation.

### Grammar

```
program = using-list , ( statement )+ ;

using-list = ( using , ";" )* ;

statement = condition , ";" | accept | if-statement ;

condition = compound-identifier , condition-op , value ;

condition-op = "==" | "!=" ;

accept = "accept" , compound-identifier , "{" ( value , "," )+ "}" ;

if-statement = "if" , condition , "{" , program , "}" ,
                ( "else if" , "{" , program , "}" )* ,
                "else" , "{" , program , "}" ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

value = compound-identifier | STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```

An identifier matches the regex `[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?` and must not match any
keyword. The list of keywords is:

```
if
else
accept
using
```

A string literal matches the regex `”[^”]*”`, and a numeric literal matches the regex `[0-9]+` or
`0x[0-9A-F]+.`

## Bind Libraries

A bind library defines a set of properties that drivers may assign to their children. Also,
bind programs may refer to bind libraries.

TODO(fxbug.dev/35932): Flesh out this documentation.
TODO(fxbug.dev/36103): Implement and document comments.

```
library = library-header , using-list , declaration-list ;

library-header = "library" , compound-identifier , ";" ;

using-list = ( using , ";" )* ;

using = "using" , compound-identifier , ( "as" , IDENTIFIER ) ;

compound-identifier = IDENTIFIER ( "." , IDENTIFIER )* ;

declaration-list = ( declaration , ";" )* ;

declaration = primitive-declaration | enum-declaration ;

primitive-declaration = ( "extend" ) , type , IDENTIFIER ,
                        ( "{" primitive-value-list "}" ) ;

type = "uint" | "string" | "bool";

primitive-value-list = ( IDENTIFIER , "=" , literal , "," )* ;

enum-declaration = ( "extend" ) , "enum" , IDENTIFIER ,
                   ( "{" , enum-value-list , "}" ) ;

enum-value-list = ( IDENTIFIER , "," )* ;

literal = STRING-LITERAL | NUMERIC-LITERAL | "true" | "false" ;
```

An identifier matches the regex `[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?` and must not match any
keyword. The list of keywords is:

```
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
