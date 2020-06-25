# Test Component

## Create a test component

### BUILD.gn

```gn
import("//src/sys/build/components.gni")

executable("my_test") {
  sources = [ "my_test.cc" ]
  testonly = true
  deps = [
    "//src/lib/fxl/test:gtest_main",
    "//third_party/googletest:gtest",
  ]
}

fuchsia_component("my-test-component") {
  testonly = true
  manifest = "meta/my_test.cmx"
  deps = [ ":my_test" ]
}

fuchsia_test_package("my-test-package") {
  test_components = [ ":my-test-component" ]
}

group("tests") {
  deps = [ ":my-integration-test" ]
  testonly = true
}
```

`test_package` will expect that there is a corresponding cmx file in the `meta`
folder. So for above example there should be a `my_test.cmx` file in `meta/`.

See also: [test packages][test-packages]

### meta/my\_test.cmx

```json
{
    "program": {
        "binary": "bin/my_test"
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

## Isolated Storage

- By default, the test component is launched in a new hermetic environment.
- The generated environment name is of form test\_env\_XXXXX, where XXXXX is a
  randomly generated number.
- Each test component receives a new isolated storage directory.
- The directory is deleted after the text exits, regardless of the test's
  outcome.

### Keep storage for debugging

If you need to keep test storage for the debugging after the test ends, use
[run-test-component][run-test-component] in the Fuchsia shell and pass
`--realm-label` flag.

The `--realm-label` flag defines the label for environment that your test runs
in. When the test ends, the storage won't be deleted automatically - it'll be
accessible at a path under /data. Assuming you:

- gave your test component (in package `mypackage` with component manifest
  `myurl.cmx`) access to the "isolated-persistent-storage" feature
- passed --realm-label=foo to run-test-component
- wrote to the file `/data/bar` from the test binary
- can connect to the device via `fx shell`

You should see the written file under the path
`/data/r/sys/r/<REALM>/fuchsia.com:<PACKAGE>:0#meta:<CMX>/<FILE>`, e.g.
`/data/r/sys/r/foo/fuchsia.com:mypackage:0#meta:myurl.cmx/bar`

Assuming you can connect to the device via ssh, you can get the data off the
device with the in-tree utility `fx scp`.

When you're done exploring the contents of the directory, you may want to
delete it to free up space or prevent it from interfering with the results of
future tests.

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

## Restricting log severity

Tests may be configured to fail when the component's test environment produces
high severity [logs][syslogs]. This is useful for when such logs, for instance
when such logs are unexpected, as they indicate an error.

A test might expect to log at ERROR severity. For example, the test might be
covering a failure condition & recovery steps. Other tests might expect not to
log anything more severe than INFO. The common case and default behavior is for
errors above WARN level to be considered failures, but there are configuration
files for overrides here:

- **fuchsia**: [//garnet/bin/run_test_component/max_severity_fuchsia.json][max-severity-fuchsia]
- **petals**: //tests/config/max_severity_\<petal\>.json

For example, *experiences*: [//tests/config/max_severity_experiences.json][max-severity-experiences]

For instance, to allow a test to produce **ERROR** logs, add the following:

```json
{
   "tests": [
      {
         "url": "fuchsia-pkg://fuchsia.com/my-package#meta/my-test.cmx",
         "max_severity": "ERROR"
      },
      ...
   ]
}
```

To cause the same test to fail on any log message more severe than **INFO**:

```json
{
   "tests": [
      {
         "url": "fuchsia-pkg://fuchsia.com/my-package#meta/my-test.cmx",
         "max_severity": "INFO"
      },
      ...
   ]
}
```

Valid values for `max_severity`: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`.

Changes to configuration take effect *only after an update*, for instance with `fx
update` or `fx ota`, or by rebuilding and restarting `fx qemu`.

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
"fuchsia.time.Utc"
"fuchsia.ui.policy.Presenter"
"fuchsia.ui.scenic.Scenic"
"fuchsia.vulkan.loader.Loader"
```

Depending on your use case you can include one or more of the services above.
However, services that are not listed here are not supported.

This option would be replaced once we fix CP-144 (in component manager v2).

[executing-tests]: /docs/development/testing/running_tests_as_components.md
[run-test-component]: /docs/development/testing/running_tests_as_components.md#running_tests_legacy
[max-severity-fuchsia]: /garnet/bin/run_test_component/max_severity_fuchsia.json
[max-severity-experiences]: https://fuchsia.googlesource.com/experiences/+/refs/heads/master/tests/config/max_severity_experiences.json
[syslogs]: /docs/development/logs/concepts.md
[test-packages]: /docs/development/components/build.md#test-packages
