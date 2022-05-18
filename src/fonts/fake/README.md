# Fake Font Provider

Have you ever found yourself needing a component that responds to
`fuchsia.fonts.Provider` FIDL requests, but doesn't provide any actual font
files? Then `fake_fonts` is for you!

`fake_fonts.cm` can serve as a drop-in replacement for `fonts.cm` in tests and
on products that need to satisfy a dependency on `fuchsia.fonts.Provider`, but
don't actually render text or do not care what rendered text looks like.

## Usage

### In tests

1.  Add `"//src/fonts/fake:fake-fonts-cm"` to the `deps` parameter of your
    `fuchsia_test_package()`.

2.  Set up the capability routing.

    a. Add a child component:

    ```yaml
    {
        name: "fake_fonts",
        url: "fuchsia-pkg://fuchsia.com/<your-test-package>#meta/fake_fonts.cm",
    }
    ```

    b. Add an `offer` entry for `fake_fonts`' dependencies:

    ```yaml
    {
        protocol: [ "fuchsia.logger.LogSink" ],
        from: "parent",
        to: [ "#fake_fonts" ],
    }
    ```

    c. Offer protocol `"fuchsia.fonts.Provider"` from `#fake_fonts` to your
    test.

For an example, see [`./meta/fake_fonts_test.cml`](meta/fake_fonts_test.cml).

### In a product configuration

1.  Remove `"//src/fonts"` and any font collections (e.g.
    `"//src/fonts:open-fonts-collection"`) from the product definition. If these
    were added in an inherited product configuration, the removal can be done
    using `legacy_base_package_labels -= [...]` in the child product
    configuration.

2.  Add `"//src/fonts/fake:pkg"` to the product's packages (again, usually
    `legacy_base_package_labels`).

3.  Add `"//src/fonts/fake:core-shard"` to `core_realm_shards`.

## Testing

To verify that the `fake_fonts` component correctly responds to all font queries
with empty or error responses:

```
fx set core.x64 --with //src/fonts/fake:tests
# ...
fx test //src/fonts/fake
```
