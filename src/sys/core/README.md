# How to vary a core realm

Note: for a more in-depth explanation, see [RFC-0089][rfc].

The core realm is the highest-level CFv2 component realm holding other packaged
components. The contents of this realm may vary across products, as certain
parts of our platform belongs in certain configurations and not in others.

The exact contents of the core realm are controlled by a GN argument that is set
in the product definitions (located in //products). This argument contains a
list of GN targets. These targets are walked at GN metadata time to find CML
shard files to be [merged with][cml-includes] a common base to create the core
realm.

## The core's base

The common portions of the core realm which are shared across all products are
included in `//src/sys/core/meta/core.cml`.

Any components or capability routes included in this file will be part of all
Fuchsia products. Core shard operations are only additive. Therefore if a new
product is added which wishes to exclude a portion of this file, the
to-be-excluded contents should be removed from this file and added to a new core
shard.

## The core shards

The optional portions of the core realm are included in separate CML files, and
are pointed to by the `core_shard` template defined in
`//src/sys/core/build/core_shard.gni`.

Any product wishing to merge a shard into its core realm should include the
shard's GN target in the `core_realm_shards` argument in the product's `.gni`
file.

## The core name

Each product which uses the `core_realm_shards` argument should set a unique
value for the `core_realm_package_name`. This value is used for the name of the
generated core realm's package, so that the core realm packages between
different products can be disambiguated.

By convention, this value should be set to `core-$PRODUCT_NAME`. So for example
the workstation product's core package name is `core-workstation`.

[rfc]: //docs/contribute/governance/rfcs/0089_core_realm_variations.md
[cml-includes]: //docs/development/components/build.md#component-manifest-includes
