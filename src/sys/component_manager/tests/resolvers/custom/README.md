# Component Resolvers test

Tests that Component Resolvers, implemented by components, are routed
correctly.

The custom resolver resolves a URL with a custom scheme that constructs a
ComponentDecl on the fly.

The integration test tries to access a protocol that is served by the component
represented by the custom URL. If the protocol is served correctly, this means
that the Component Resolver was successful.

## Building

To add this component to your build, append
`--with-base src/sys/component_resolver/tests/resolvers`
to the `fx set` invocation.

## Running the test

```
$ fx test component-manager-test-resolver
```
