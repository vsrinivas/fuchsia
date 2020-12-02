# pkgfs-reflector

`pkgfs-reflector` is a helper library that lets tests like the `pkg-cache`
integration tests to create a fake pkgfs and inject it into a CFv2 compoennt
tree for use by a real `pkg-cache`.

## Usage

In order to use `pkgfs-reflector` in a test, we need to include the reflector
into our test package.

```
...

fuchsia_test_package("test-package") {
  ...
  deps = [
    ...
    "//src/sys/pkg/testing/pkgfs-reflector:pkgfs-reflector",
  ]
}
...
```

Then create a test realm component, that forwards `/pkgfs` from
`pkgfs-reflector` to the component under test:

```
{
    children: [
        {
            name: "pkgfs_reflector",
            url: "fuchsia-pkg://fuchsia.com/pkgfs-reflector#meta/pkgfs-reflector.cm",
        },
        {
            name: "some_test_component",
            url: "fuchsia-pkg://...",
        },
    ],
    offer: [
        {
            directory: "pkgfs",
            from: "#pkgfs_reflector",
            to: [ "#some_test_component" ],
        },
        ...
    ],
    expose: [
        {
            protocol: "test.pkg.reflector.Reflector",
            from: "#pkgfs_reflector",
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

Then in the test driver, register pkgfs before we communicate with the component
under test:

```rust
let (pkgfs_client_end, pkgfs_server_end) =
    fidl::endpoints::create_endpoints::<DirectoryMarker>().expect("creating pkgfs channel");

// set up the fake pkgfs
...

let app = ScopedInstance::new("collection".to_string(), TEST_CASE_REALM.to_string())
    .await?;

let reflector = app
    .connect_to_protocol_at_exposed_dir::<ReflectorMarker>()?;

reflector.reflect(pkgfs_client_end).await?;

// Connect to the component under test, which can use the fake pkgfs.
let component_under_test =
    app.connect_to_protocol_at_exposed_dir::<ComponentUnderTestMarker>()?;

...
```
