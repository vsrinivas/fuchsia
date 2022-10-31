# session_manager

Reviewed on: 2021-10-01

`session_manager` is the component that runs and manages [session components](glossary.session-component).

## Building

Add the `session_manager` component to builds by including `--with-base
//src/session/bin/session_manager` in the `fx set` invocation followed by
rebuilding and re-paving the device.

Product configurations built on Session Framework (such as `fx set
workstation_eng.x64`) include `//src/session/bin/session_manager` by default.

## Running

`session_manager` is launched in one of two ways: manually or automatically on
system boot.

In general, running manually is useful during development, while running on boot
is desirable for a production configuration.

### Running manually

Use the [`ffx session`]((/docs/development/tools/ffx/getting-started.md) tool
to launch a specific session:

```
ffx session launch fuchsia-pkg://fuchsia.com/your_session#meta/your_session.cm
```

### Launching a session on boot

`session_manager` attempts to launch a session on boot based on the contents of
its `session_url` configuration parameter.

To boot into a session, include `session_manager` and the session component in
the base package set and assign the URL of the session component to the product
configuration:

```
fx set core.qemu-x64 \
  --with-base //src/session/bin/session_manager \
  --with-base //path/to/your_session \
  --args=product_config.session_url="fuchsia-pkg://fuchsia.com/your_package#meta/your_session.cm"
```

This can also be configured directly in a product's definition `*.gni` files
or via `fx args`.

Re-build, re-pave, and restart your device and it will boot into
`session_manager` and launch your session.

## Testing

Unit tests for `session_manager` are included in the `session_manager_tests`
package, and integration tests are included in the
`session_manager_integration_tests` package.

Both can be included in your build by adding `--with //src/session:tests` to
your `fx set`.

```
fx test session_manager_tests
fx test session_manager_integration_tests
```

## Source layout

The entry point for `session_manager` is in `src/main.rs` with implementation
details in other files within `src/`.

Unit tests are co-located with the code, while integration tests are in
[`//src/session/tests`](/src/session/tests).

[glossary.session-component]: /docs/glossary.md#session-component