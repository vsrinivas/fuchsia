# `cm_types` library

This library contains common Component Manager types used in Component Manifests
(`.cml` files and binary `.cm` files). These types come with `serde` serialization
and deserialization implementations that perform the required validation.

`.cml` files go through a series of transformations before they are consumed by
Component Manager at runtime.

- JSON5 `.cml` files are transformed by `cmc` into `.cm` files, a JSON format not meant to be edited
  by humans.
- JSON `.cm` files are transformed by Component Resolvers into `fuchsia.sys2.ComponentDecl` FIDL, and
  sent over FIDL to Component Manager.
- Component Manager transforms `fuchsia.sys2.ComponentDecl` FIDL into a local representation (in Rust)
  that does some validation and unwraps required "optional" values in FIDL tables.

Each of these stages perform some level of validation, which includes validating things like entity names,
paths, URLs, etc. All of these types require the same validation and should be represented by the types
in this library.

When adding a basic, common type to the CML syntax, consider whether that type should
be added here, so that every stage of the transformation pipeline can benefit.
