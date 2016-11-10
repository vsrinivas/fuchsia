# FIDL Compiler Interfaces

These files represent the interface between the front-end of the FIDL compiler
(which parses `.fidl` files) and the back-ends which generate source code in a
variety of languages. The interface is expressed in FIDL which introduces a
somewhat circular dependency.

There's a script `//lib/fidl/compiler/interfaces/update.sh` that can be run to
update the appropriate files.
