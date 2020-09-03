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
options to override it.

For instance, to allow a test to produce **ERROR** logs:

  * {Using fuchsia\_test\_package}

  ```gn
  fuchsia_component("my-package") {
    testonly = true
    manifest = "meta/my-test.cmx"
    deps = [ ":my_test" ]
  }

  fuchsia_test_package("my-package") {
    test_specs = {
        log_settings = {
          max_severity = "ERROR"
        }
    }
    test_components = [ ":my-test" ]
  }
  ```

  * {Using test\_package}

  ```gn
  test_package("my-package") {
    deps = [
      ":my_test",
    ]

    meta = []
      {
        path = rebase_path("meta/my-test.cmx")
        dest = "my-test.cmx"
      },
    ]

    tests = [
      {
        log_settings = {
          max_severity = "ERROR"
        }
        name = "my_test"
        environments = basic_envs
      },
    ]
  }
  ```

To make the test fail on any message more severe than **INFO** set `max_severity`
to **"INFO"**.

Valid values for `max_severity`: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`.

If your test was already configured using [legacy methods][legacy-restrict-logs]
you will need to remove your test from the config file (eg.
max_severity_fuchsia.json) and run `fx update` or `fx ota`.

If the test is not removed from the legacy list, the configuration in legacy
list would be preferred and you will see a warning when running the test.

## Running test cases in parallel

  [FTF][ftf] makes it easy to run test cases in parallel by standardizing the
  option across various test runtimes. [Test runners][test-runner] decide
  the default value for how many tests can run in parallel but developers can
  override it using `BUILD.gn`.

  * {Using fuchsia\_test\_package}

  ```gn
  fuchsia_component("my-package") {
    testonly = true
    manifest = "meta/my-test.cml"
    deps = [ ":my_test" ]
  }

  fuchsia_test_package("my-package") {
    test_specs = {
        parallel = 1
    }
    test_components = [ ":my-test" ]
  }
  ```

  * {Using test\_package}

  ```gn
  test_package("my-package") {
    deps = [
      ":my_test",
    ]

    meta = []
      {
        path = rebase_path("meta/my-test.cml")
        dest = "my-test.cm"
      },
    ]

    tests = [
      {
        parallel = 1
        name = "my_test"
        environments = basic_envs
      },
    ]
  }
  ```

NOTE: This feature only works with FTF tests (v2 component tests).

### Running the test

When running the test on development device, prefer `fx test` to run the test.
The tool will automatically pick the configuration and pass it to
run-test-component. If for some reason you need to use `run-test-component`,
you need to pass the flag yourself.

```sh
fx shell run-test-component --max-log-severity=ERROR <test_url>
```

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

### Other system services

There are some services that cannot be faked or mocked. You can connect to real
system versions of these services by mentioning these services in
`system-services`. Services that cannot be faked are listed
[here](/garnet/bin/run_test_component/test_metadata.cc).

Test can only list allowlisted system services under `"system-services"` as
demonstrated above.

[executing-tests]: /docs/development/testing/running_tests_as_components.md
[run-test-component]: /docs/development/testing/running_tests_as_components.md#running_tests_legacy
[syslogs]: /docs/development/logs/concepts.md
[test-packages]: /docs/development/components/build.md#test-packages
[legacy-restrict-logs]: https://fuchsia.googlesource.com/fuchsia/+/1529a885fa0b9ea4867aa8b71786a291158082b7/docs/concepts/testing/test_component.md#restricting-log-severity
[ftf]: fuchsia_testing_framework.md
[test-runner]: fuchsia_testing_framework.md#test-runner