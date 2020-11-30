# base-resolver

`base-resolver` is a V2 component that implements the Component Resolver FIDL protocol
[`fuchsia.sys2.ComponentResolver`] and exposes this protocol as a resolver capability.

The responsibility of `base-resolver` is to resolve URLs to packages that are located
in pkgfs and are part of the "base" set of packages.

## Building

The `base-resolver` component should be part of the core product configuration and be
buildable by any `fx set` invocation.

## Running

This component is not packaged in the traditional way. Instead, its binary and manifest are
included in the Zircon Boot Image (ZBI) and are accessible via bootfs.

To launch this component, include it as a child in the component topology using the URL
`fuchsia-boot:///#meta/base-resolver.cm`, and include its exposed resolver capability
in an environment.

```json5
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/sys/base-resolver/tests/meta/integration-test.cml" region_tag="environment"}
```

## Testing

Unit tests for base-resolver are available in the `base-resolver-unittests`
package.

```
$ fx test base-resolver-unittests
```

Integration tests for base-resolver are available in the `base-resolver-tests`
package.

```
$ fx test base-resolver-tests
```

[`fuchsia.sys2.ComponentResolver`]: ../../../sdk/fidl/fuchsia.sys2/runtime/component_resolver.fidl
