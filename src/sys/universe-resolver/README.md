# universe-resolver

`universe-resolver` is a V2 component that implements the Component Resolver FIDL protocol
[`fuchsia.component.resolution.Resolver`] and exposes this protocol as a resolver capability.

The responsibility of `universe-resolver` is to resolve URLs to packages that are made available
by the `pkg-resolver`.

## Building

The `universe-resolver` component should only be included in eng builds.
Include in your local build with `fx set ... --with-base //src/sys/universe-resolver`.

## Running

To launch this component, include it as a child in the component topology using the URL
`fuchsia-pkg://fuchsia.com/universe-resolver#meta/universe-resolver.cm`, and include its
exposed resolver capability in an environment.

```json5
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/sys/universe-resolver/meta/example.cml" region_tag="sample"}
```

## Testing

Unit tests are available in the `universe-resolver-unittests` package.
Make sure they are included in your build: `fx set ... --with-base //src/sys/universe-resolver:tests`.

```
$ fx test universe-resolver-unittests
```

[`fuchsia.component.resolution.Resolver`]: ../../../sdk/fidl/fuchsia.sys2/runtime/component_resolver.fidl
