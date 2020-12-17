# dir-reflector

`dir-reflector` is a helper library that lets tests like the `pkg-cache`
integration tests to inject a directory into a CFv2 component tree. This can
then be used to serve fake services to real components, like `pkg-cache`.

## Usage

In order to use `dir-reflector` in a test, we need to include the reflector
into our test package.

```
...

fuchsia_component("pkgfs-reflector") {
    manifest = "meta/pkgfs-reflector.cml"
    deps = [ "//src/sys/pkg/testing/dir-reflector:bin" ]
}

fuchsia_test_package("test-package") {
  ...
  deps = [
    ...
    ":pkgfs-reflector",
  ]
}
...
```

Next, create a component to expose the reflected directory to other components.
This describes how to expose the reflected directory as `/pkgfs`:

```
{
    include: [ "sdk/lib/diagnostics/syslog/client.shard.cml" ],
    program: {
        binary: "bin/dir_reflector",
    },
    capabilities: [
        { protocol: "test.pkg.reflector.Reflector" },
        {
            directory: "reflected",
            rights: [ "rw*" ],
            path: "/reflected",
        },
    ],
    use: [
        { runner: "elf" },
    ],
    expose: [
        {
            protocol: "test.pkg.reflector.Reflector",
            from: "self",
        },
        {
            directory: "reflected",
            from: "self",
            as: "pkgfs",
        },
    ],
}
```

Finally, create a test realm component, that forwards `/reflected` from
`dir-reflector` to the component under test:

```
{
    children: [
        {
            name: "dir_reflector",
            url: "fuchsia-pkg://fuchsia.com/test-package#meta/dir-reflector.cm",
        },
        {
            name: "some_test_component",
            url: "fuchsia-pkg://...",
        },
    ],
    offer: [
        {
            directory: "reflected",
            from: "#dir_reflector",
            to: [ "#some_test_component" ],
        },
        ...
    ],
    expose: [
        {
            protocol: "test.pkg.reflector.Reflector",
            from: "#dir_reflector",
        },
        ...
    ],
    ...
}
```

Finally, in the test driver manifest, either include the test realm as a static
child (if we only ever need one instance of the component under test, or launch
it in a collection if the test driver needs to launch a number of instances of
the test realm.

Then in the test driver, register directory before we communicate with the
component under test:

```rust
let (dir_client_end, dir_server_end) =
    fidl::endpoints::create_endpoints::<DirectoryMarker>()
    .expect("creating dir channel");

// use the directory_server_end to create the mock directory...
...

// set up the child component
let app = ScopedInstance::new("collection".to_string(), TEST_CASE_REALM.to_string())
    .await?;

// register the directory with the reflector.
let reflector = app
    .connect_to_protocol_at_exposed_dir::<ReflectorMarker>()?;

reflector.reflect(dir_client_end).await?;

// Connect to the component under test, which can use the fake directory.
let component_under_test =
    app.connect_to_protocol_at_exposed_dir::<ComponentUnderTestMarker>()?;

...
```
