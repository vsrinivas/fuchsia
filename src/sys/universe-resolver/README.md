# universe-resolver

`universe-resolver` is a V2 component that implements the Component Resolver FIDL protocol
[`fuchsia.sys2.ComponentResolver`] and exposes this protocol as a resolver capability.

The responsibility of `universe-resolver` is to resolve URLs to packages that are located
in pkgfs or other external sources belonging to the `fuchsia.com` repo.

## Building

The `universe-resolver` component should be part of the core product configuration and be
buildable by any `fx set` invocation.

## Running

To launch this component, include it as a child in the component topology using the URL
`fuchsia-pkg://fuchsia.com/universe-resolver#meta/universe-resolver.cm`, and include its
exposed resolver capability in an environment.

```json5
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/sys/universe-resolver/meta/example.cml" region_tag="sample"}
```

## Testing

Unit tests are available in the `universe-resolver-unittests` package.

```
$ fx test universe-resolver-unittests
```

[`fuchsia.sys2.ComponentResolver`]: ../../../sdk/fidl/fuchsia.sys2/runtime/component_resolver.fidl
