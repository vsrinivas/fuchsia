# session_manager

Reviewed on: 2021-10-01

`session_manager` is the component that runs and manages sessions. For more
information on what sessions are and what roles they play in products built on
Fuchsia, see [Session Framework](/docs/concepts/session/introduction.md)

## Building

Add the `session_manager` component to builds by including `--with-base
//src/session` in the `fx set` invocation followed by rebuilding and re-paving
the device.

Product configurations built on Session Framework (such as `fx set
workstation.x64`) include `//src/session` by default.

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

`session_manager` will launch a session on boot if the build contains
a `session_config` configuration.

To boot into session, create a configuration file that specifies which session
to launch on boot:

```
{ "session_url": "fuchsia-pkg://fuchsia.com/your_package#meta/your_session.cm" }
```

Add to your `BUILD.gn` file:

```
import("//src/session/build/session_config.gni")

session_config("your_session_config") {
    config = "path/to/config.json"
}
```

Then, ensure that the target `:your_session_config` is included in the base
image (for example, using `--with-base`, or as a direct dependency of a product
build group).

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
