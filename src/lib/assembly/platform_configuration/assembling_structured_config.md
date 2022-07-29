# Assembling structured configuration

[Self-link](https://goto.google.com/fuchsia-assembling-structured-config)

[Structured configuration][sc-docs] allows developers to package components with
different values in different contexts. Product assembly allows developers to
define structured configuration values based on high-level platform and product
configuration.

To vary your component's configuration by product or build type you must declare
a schema, identify how values will be derived from a product's configuration,
and generate your component's config.

**Warning: this feature is under active development. If you have any questions
when following this guide, ask in the [Software Assembly chat][sa-chat] or
schedule a slot in [Software Assembly office hours][sa-hours].**

# Define a configuration schema

Your component must have a declared configuration schema in its manifest. For
[example][example-cml]:

```json5
config: {
    enable_foo: { type: "bool" },
},
```

For more information see the [documentation for structured config][sc-docs].

# Identify product assembly configuration

Many configuration options can be represented as the difference between
engineering builds and non-engineering builds. If the configuration of your
component can't be deduced solely from build type, schedule a slot in [product
assembly office hours][sa-hours] and decide on how your component's
configuration will be represented in the [software assembly schema][sa-schema].

# Configure the package

Determine your package's name and your component's manifest resource path.
Define logic to populate configuration for it in the [product assembly
tool][configure-product]:

```rs
patches
    .package("configured_by_assembly")
    .component("meta/to_configure.cm")
    .field("enable_foo", matches!(self.platform.build_type, BuildType::Eng));
```

Note: You cannot mix GN and product assembly when producing configuration values
for a given component.

# Add an image assembly comparison exception

Product assembly tools are replacing the existing GN-based method for
configuring system images, and during that transition there are a number of
consistency checks to ensure that GN and product assembly produce identical
outputs.

You must add your package's name to a [list of exceptions][mismatch-exceptions]
because using product assembly for configuration results in different package
contents.

# Update size limits configuration (if any)

When producing configuration values for a package, product assembly must write
its outputs to a different location in the build directory. There are binary
size checks that rely on paths to package manifests that exist in the build
directory, so it is important that you update the package's output path in the
build configuration rules.

See [this diff][session-manager-diff] for an example of changing
session_manager's size limit configuration to match the new location.

[sc-docs]: https://fuchsia.dev/fuchsia-src/development/components/configuration/structured_config
[sa-chat]: https://goto.google.com/fuchsia-product-assembly-chat
[sa-hours]: https://goto.google.com/fuchsia-product-assembly-office-hours
[example-cml]: /examples/assembly/structured_config/configured_by_assembly/meta/to_configure.cml
[configure-product]: /src/lib/assembly/platform_configuration/src/lib.rs
[sa-schema]: /src/lib/assembly/config_schema/src/product_config.rs
[mismatch-exceptions]: /build/assembly/scripts/compare_image_assembly_config_contents.py
[session-manager-diff]: https://goto.google.com/fuchsia-session-manager-size-limits-path-update
