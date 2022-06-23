# base-resolver

`base-resolver` is a V2 component that implements the Component Resolver FIDL protocol
[`fuchsia.component.resolution.Resolver`] and exposes this protocol as a resolver capability.

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
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/sys/base-resolver/tests/meta/integration-test.cml" region_tag="environment"}
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

[`fuchsia.component.resolution.Resolver`]: ../../../sdk/fidl/fuchsia.sys2/runtime/component_resolver.fidl

# pkg-cache-resolver

`pkg-cache-resolver` implements the Component Resolver FIDL protocol
[`fuchsia.component.resolution.Resolver`] and exposes this protocol as a resolver capability.

The responsibility of `pkg-cache-resolver` is to resolve the URL
"fuchsia-pkg-cache:///#meta/pkg-cache.cm". It does this by:
1. Using fuchsia.boot/Arguments to determine the hash of the system_image package.
2. Reading the system_image package to determine the hash of the pkg-cache package.
3. Using an RX* handle to blobfs and the package-directory library to serve the package directory
   for pkg-cache directly out of blobfs.

The code for `pkg-cache-resolver` is in the same directory as the `base-resolver` because:
1. They are built into the same binary to save space in bootfs (they both build with the Rust VFS
   library which is large).
2. Eventually base-resolver should stop using pkgfs/packages and instead serve packages directly
   from blobfs (and therefore only serve base packages, whereas currently base-resolver can resolve
   any non-base packages exposed by pkgfs/packages). This will be implemented by changing the
   pkg-cache-resolver implementation to resolve any of the base packages instead of only pkg-cache,
   at which point the two resolvers can be merged into a single component and pkg-cache-resolver
   can be deleted. This is tracked at fxbug.dev/101492.

## Building

The `pkg-cache-resolver` component should be part of the core product configuration and be
buildable by any `fx set` invocation.

## Running

This component is not packaged in the traditional way. Instead, its binary and manifest are
included in the Zircon Boot Image (ZBI) and are accessible via bootfs.

To launch this component, include it as a child in the component topology using the URL
`fuchsia-boot:///#meta/pkg-cache-resolver.cm`, and include its exposed resolver capability
in an environment.

## Testing

Unit tests for pkg-cache-resolver are included in the `base-resolver-unittests` package.

```
$ fx test base-resolver-unittests
```

Integration tests for pkg-cache-resolver are available in the `base-resolver-tests` package
(which uses pkg-cache-resolver as a component resolver) and the `base-resolver-integration-tests`
package, which uses RealmBuilder to fake the dependencies.

```
$ fx test base-resolver-tests base-resolver-integration-tests
```
