# Input synthesis

The input synthesis library supports input event injection for testing purposes.

Injection is performed by creating and registering a synthetic input device compatible to a given
`InputDeviceRegistry` FIDL protocol. It then uses that device to send `InputReports` to the input
service, mimicking real user input on HID devices (e.g. touchscreens, media buttons, keyboards).

The library allows test code to send high-level commands that get converted into low-level input
messages, without having to write the low-level commands directly. For example, it will convert a
command `Text.Send("hello")` into a sequence of presses and releases of the keys
`h`, `e`, `l`, `l`, and `o` in a rapid succession. Fuchsia platform's input handlers, and
downstream UI clients, cannot distinguish these fake key presses from those typed up on a real
keyboard.

The two supported protocols are the
[`fuchsia.input.injection.InputDeviceRegistry`](/sdk/fidl/fuchsia.input.injection/input_device_registry.fidl)
protocol served by the modern Input Pipeline component and the
[`fuchsia.ui.input.InputDeviceRegistry`](/sdk/fidl/fuchsia.ui.input/input_device_registry.fidl)
protocol served by the legacy Root Presenter component.

## Use the library in tests

### Import the library

To use `input_synthesis` directly in tests written in Rust, first import the library in your test
file and add it as a dependency in your test's `BUILD` file:

```rs
// my-test.rs
use input_synthesis;
```

```
# BUILD
rustc_test("factory_reset_handler_test") {
  sources = [ "my-test.rs" ]
  deps = [
    "//src/lib/ui/input-synthesis",
    # other deps
  ]
}
```

### Implicit `InputDeviceRegistry`

You can then use the device registry to fire any input event supported in the library:

```
input_synthesis::text_command("hello world", /* key_event_duration */ 100).await?;
```

As an example reference, the [sl4f Input command](/src/testing/sl4f/src/input/facade.rs)
uses the library directly with an implicit `InputDeviceRegistry` based on the
[build argument](#build-args).

### Explicit `InputDeviceRegistry`

You can alternatively create a device registry using the preferred `InputDeviceRegistry` protocol
by first connecting to it. The example below connects to the protocol the way an integration test
would using a Realm Builder `RealmInstance`.

```
let injection_registry = realm.root
    .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_input_injection::InputDeviceRegistryMarker>()
    .expect("Failed to connect to InputDeviceRegistry");
let mut device_registry =
    input_synthesis::modern_backend::InputDeviceRegistry::new(injection_registry);
input_synthesis::synthesizer::text(
    "hello world",
    /* key_event_duration */ 100,
    &mut device_registry,
)
.await?;
```

As an example reference, the
[factory-reset-handler](/src/ui/tests/integration_input_tests/factory-reset-handler/src/main.rs)
test uses the library directly with a specific `InputDeviceRegistry`.

## Use the component in tests

The `input_synthesis` component can also be used via the `input_synthesis.test.fidl` API, which
forwards FIDL calls to the Rust library code. This approach is useful for integration tests that
cannot link to Rust directly but can use prebuilt components.

### (Optional) Extend the FIDL API

Because the `input_synthesis.test.fidl` is relatively new, it's possible it does not yet contain
calls to all supported commands in the `input-synthesis` library. If that's the case, one can add
the desired commands to the source file and serve them accordingly in [`main.rs`](./src/main.rs).

### Import the FIDL

```cpp
// my-test.cc
#include <test/inputsynthesis/cpp/fidl.h>
```

```
# BUILD
executable("my-test-bin") {
  testonly = true
  sources = [ "my-test.cc" ]
  deps = [
    "//src/lib/ui/input-synthesis:test.inputsynthesis",
    # other deps
  ]
}
```

### Use the FIDL

The example below connects to the protocol the way an integration test
would using a Realm Builder `RealmRoot`.

```cpp
auto input_synthesis = realm_->Connect<test::inputsynthesis::Text>();
bool done = false;
input_synthesis->Send("Hello world!", [&done]() { done = true; });
RunLoopUntil([&] { return done; });
```

As an example reference, the
[text-input-test](/src/ui/tests/integration_input_tests/text-input/text-input-test.cc) test
makes use of the test FIDL.

Note: Because this approach uses an implicit `InputDeviceRegistry`, it may be relevant to use the
appropriate [build argument](#build-args) for your use case.

## Planned work

The legacy portion of the library is scheduled for removal along with the deprecation of Root
Presenter's input logic.
