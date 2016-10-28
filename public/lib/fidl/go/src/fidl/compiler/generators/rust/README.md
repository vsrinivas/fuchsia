# Rust Code Generator Usage Guide

This guide exists to help you use the Rust code generator for the Mojom IDL.

## Output directory structure

In order to provide as close of a match between the Rust import/module system
and Mojom's import/module system, generated code is organized into a heirarchy
of directories based on Mojom module. Inside of a Mojom module would exists each
individual file (which in Rust is its own module). By this design, technically
its okay to have namespace collisions, but we hope that this is accounted for in
the parser.

The directory structure is best shown through an example. This example will be a
running example throughout the rest of the README.

Suppose you have the following Mojom files which live in the same directory:

```mojom
// test.mojom

module my_module.your_module;
```

```mojom
// test2.mojom

module my_module;

import("test.mojom");
```

The resulting directory structure would look like this:

```
$ tree .
.
|-- my_module
|   |-- your_module
|   |   |-- mod.rs
|   |   `-- test.rs
|   |-- mod.rs
|   `-- test2.rs
`-- mod.rs
```

The ``mod.rs`` files contain all the module definitions and module exporting
necessary to allow imports to work.

## Including generated code in your crate

In order to include this generated code into your crate, the first step is to
modify your ``build.rs`` file (or other Cargo build script) to generate your code
into ``OUT_DIR`` using the generator binary.

The second step is simply using the ``include!`` macro to include the generated
code into your Rust code at build time. Wherever you want to reference your
generated code from, just write:

```rust
pub mod generated {
    include!(concat!(env!("OUT_DIR"), "/mod.rs"));
}
```

``concat!`` additional directories relative to ``OUT_DIR`` as necessary.

More detailed help may be found 
[http://doc.crates.io/build-script.html#case-study-code-generation](on the Cargo
website).

## Naming scheme

First, let's assume your included generated code can be found at Rust source
path ``$gen``.

Given this, you may access the generated code via ``$gen::generated``. For
example, code from ``test.mojom`` may be found at:

```rust
use $gen::generated::my_module::your_module::test;
```

If inside of ``test.mojom`` you have a struct called ``MyStruct``, you may
access it via:

```rust
use $gen::generated::my_module::your_module::test::MyStruct;
```

However, if you want to access an enum ``MyEnum`` which was defined inside of
MyStruct, the enum can be accessed via:

```rust
use $gen::generated::my_module::your_module::test::MyStructMyEnum;
```

If a constant ``MyConst`` instead was defined, it may be found via:

```rust
use $gen::generated::my_module::your_module::test::MYSTRUCTMYCONST;
```

Finally, suppose you define a struct ``type``. That's a reserved Rust keyword!
The way those are handled is by appending an underscore to the name. Thus, you
may find the struct via:

```rust
use $gen::generated::my_module::your_module::test::type_;
```

Note that the naming conventions used for Mojoms do not align with Rust naming
conventions in several respects. We default to the Mojom naming conventions to
reduce ambiguity. Although the Rust compiler may complain about the naming
scheme, we explicitly put ``#[allow(bad_style)]`` at the top of every generated
file to suppress these warnings. In general, we recommend that when generated
names are imported into your actual Rust code, use ``as`` for import aliasing to
make your code look as clean as possible.

