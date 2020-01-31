# Test Component

## Create a test component

### BUILD.gn

```gn
import("//build/test/test_package.gni")

executable("my_test_bin") {
  testonly = true
  output_name = "my_test"

  sources = [
    "my_test.cc",
  ]
}

test_package("my_test_pkg") {
  deps = [
    ":my_test_bin",
  ]

  tests = [
    {
      name = "my_test"
    },
  ]
}
```

`test_package` will expect that there is a corresponding cmx file in the `meta`
folder. So for above example there should be a `my_test.cmx` file in `meta/`.

### meta/my\_test.cmx

```json
{
    "program": {
        "binary": "test/my_test"
    },
    "sandbox": {
        "services": [...]
    }
}
```

## Running the tests

To run a Fuchsia test out of your build, execute:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>TEST_NAME</var></code>
</pre>

For more information on running Fuchsia tests, see
[Running tests as components][executing-tests].


## Ambient Services

All test components are started in a new hermetic environment. By default, this
environment only contains a few basic services (ambient):

```text
"fuchsia.sys.Environment"
"fuchsia.sys.Launcher"
"fuchsia.process.Launcher"
"fuchsia.process.Resolver"
```

Tests can use these services by mentioning them in their `sandbox > services`.

## Logger Service

Tests and the components launched in a hermetic environment will have access to system's `fuchsia.logger.LogSink` service if it is included in their sandbox. For tests to inject Logger, the tests must use `injected-services` (see below). Then, the injected Logger service takes precedence.

## Run external services

If your test needs to use (i.e. its sandbox includes) any services other than the ambient and logger services above, you must perform either, both or none:

- Inject the services by starting other components that provide those services in the hermetic test environment
- Request non-hermetic system services be included in the test environment, when a service cannot be faked or mocked, see [Other system services](#Other-system-services).

To inject additional services, you can add a `injected-services` clause to the manifest file's facets:

```json
"facets": {
  "fuchsia.test": {
    "injected-services": {
        "service_name1": "component_url1",
        "service_name2": "component_url2"
    }
  }
}
```

`fx test` will start `component_url1` and `component_url2` and the
test will have access to `service_name1` and `service_name2`. Note that this makes the injected services available in the test environment, but the test component still needs to "use" them by including the service in its `sandbox > services`.


### Network access

Currently we cannot run an instance of netstack inside a hermetic environment,
because it conflicts with the real netstack.  If your test needs to talk to
netstack, it may only talk to the real netstack outside the test environment. To
enable this workaround you need to allow some system services:

```json
"facets": {
  "fuchsia.test": {
    "system-services": [
      "fuchsia.device.NameProvider",
      "fuchsia.net.Connectivity",
      "fuchsia.net.stack.Stack",
      "fuchsia.netstack.Netstack",
      "fuchsia.net.NameLookup",
      "fuchsia.posix.socket.Provider",
    ]
  }
}
```

### Other system services

There are some services, such as network, that cannot be faked or mocked. However, you can connect to real system versions of these services by mentioning these services in `system-services`. Services that cannot be faked:

```text
"fuchsia.boot.FactoryItems"
"fuchsia.boot.ReadOnlyLog"
"fuchsia.boot.RootJob"
"fuchsia.boot.RootResource"
"fuchsia.boot.WriteOnlyLog"
"fuchsia.device.NameProvider"
"fuchsia.kernel.Counter"
"fuchsia.scheduler.ProfileProvider"
"fuchsia.sys.test.CacheControl"
"fuchsia.sysmem.Allocator"
"fuchsia.ui.policy.Presenter"
"fuchsia.ui.scenic.Scenic"
"fuchsia.vulkan.loader.Loader"
```

Depending on your use case you can include one or more of the services above.
However, services that are not listed here are not supported.

This option would be replaced once we fix CP-144 (in component manager v2).

[executing-tests]: /docs/development/testing/running_tests_as_components.md