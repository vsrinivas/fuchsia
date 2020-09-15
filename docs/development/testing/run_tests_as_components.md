# Run tests as components

This guide provides instructions on how to run test components using
the `fx` tool.

For more information, see [Tests as components][tests-as-components].

## Run tests

Note: If you encounter any bugs or use cases not supported by `fx test`, file a
bug with `fx`.

To test the package, use the `fx test` command with the name of the package:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>TEST_NAME</var></code>
</pre>

If the package you specified is a test component, the command makes your Fuchsia
device load and run said component. However, if the package you specified is a
host test, the command directly invokes that test binary. Note that this can lead
to the execution of multiple components.

### Customize `fx test` invocations

In most cases, you should run the entire subset of test that verify the code
that you are editing. You can run `fx test` with arguments to run specific tests
or test suites, and flags to filter down to just host or device tests. To
customize `fx test`:

<pre class="prettyprint">
<code class="devsite-terminal">fx test [<var>FLAGS</var>] [<var>TEST_NAME</var> [<var>TEST_NAME</var> [...]]]</code>
</pre>

### Multiple ways to specify a test

`fx test` supports multiple ways to reference a specific test.

- Full or partial paths:

    Provide a partial path to match against all test binaries in children
    directories.

    <pre class="prettyprint">
    <code class="devsite-terminal">fx test //host_x64/gen/sdk</code>
    </pre>


    Provide a full path to match against that exact binary.

    <pre class="prettyprint">
    <code class="devsite-terminal">fx test //host_x64/pm_cmd_pm_genkey_test</code>
    </pre>

    Note: `//` stands for the root of a Fuchsia tree checkout.

- Full or partial [Fuchsia Package URLs][fuchsia_package_url]:

    Provide a partial URL to match against all test components whose Package
    URLs start with the supplied value.

    <pre class="prettyprint">
    <code class="devsite-terminal">fx test <var>fuchsia-pkg://fuchsia.com/my_test_pkg</var></code>
    </pre>

    Provide a full URL to match against that exact test component.

    <pre class="prettyprint">
    <code class="devsite-terminal">fx test <var>fuchsia-pkg://fuchsia.com/my_test_pkg#meta/my_test.cmx</var></code>
    </pre>


- Package name:

    Provide a
    [package name](/docs/concepts/packages/package_url.md#package-name) to
    run all test components in that package:

    <pre class="prettyprint">
    <code class="devsite-terminal">fx test <var>my_test_pkg</var></code>
    </pre>

- Component name:

    Provide a
    [resource-path](/docs/concepts/packages/package_url.md#resource-paths) to
    test a single component in a package:

    <pre class="prettyprint">
    <code class="devsite-terminal">fx test <var>my_test</var></code>
    </pre>

### Running multiple tests

If you want to run multiple sets of Fuchsia tests, configure your Fuchsia build
to include several of the primary testing bundles, build Fuchsia, and then run
all tests in the build:

```bash
fx set core.x64 --with //bundles:tools,//bundles:tests,//garnet/packages/tests:all
fx build
fx test
```

You can also provide multiple targets in a single invocation:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>package_1 package_2 component_1 component_2</var></code>
</pre>



### Converting from run-test or run-host-tests

Note: Please file a bug with `fx` if you find any test invocations that cannot
be converted.

#### run-test

For `run-test`, you should always be able to change `fx run-test` to `fx test`,
for example:

<pre class="prettyprint">
<code class="devsite-terminal">fx run-test <var>TEST_PACKAGE_NAME</var></code>
</pre>

Now becomes:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>TEST_PACKAGE_NAME</var></code>
</pre>


#### run-host-tests

For `run-host-tests`, you should always be able to change `fx run-host-tests` to
`fx test`, for example:

<pre class="prettyprint">
<code class="devsite-terminal">fx run-host-tests <var>PATH_TO_HOST_TEST</var></code>
</pre>

Now becomes:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>PATH_TO_HOST_TEST</var></code>
</pre>

#### run-e2e-tests

Caution: The conversion from `run-e2e-tests` to `fx test` is currently in the beta
stage. The `fx test` command may fail to run some of the existing end-to-end tests
today.

For `run-e2e-tests`, use the `fx test` command with the `--e2e`
option, for example:

<pre class="prettyprint">
<code class="devsite-terminal">fx run-e2e-tests <var>END_TO_END_TEST_NAME</var></code>
</pre>

Now becomes:

<pre class="prettyprint">
<code class="devsite-terminal">fx test --e2e <var>END_TO_END_TEST_NAME</var></code>
</pre>

#### the `-t` flag

Unlike `fx run-test` (which operated on *packages*), `fx test` matches against tests
in many different ways. This means that you can easily target tests either by their
package name or directly by a component's name. One common workflow with `run-test`
was to use the `-t` flag to specify a single component:

<pre class="prettyprint">
<code class="devsite-terminal">fx run-test <var>PACKAGE_NAME</var> -t <var>NESTED_COMPONENT_NAME</var></code>
</pre>

Now, with `fx test`, that simply becomes:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>NESTED_COMPONENT_NAME</var></code>
</pre>

#### Passing arguments to individual tests

Note: `fx test` passes extra arguments to all selected tests. If you are
targeting many test components in a single pass, this option may not be ideal.

Use the `--` flag to provide additional arguments which are ignored by `fx test`
and passed to test components.

For example, to pass a timeout flag to a test that accepts it, execute:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>TEST_NAME</var> -- --timeout=5</code>
</pre>

Internally, this command runs the following:

<pre class="prettyprint">
<code class="devsite-terminal">fx shell run-test-component <var>TEST_NAME</var> --timeout=5</code>
</pre>

If your test is a [Components v2][glossary-components-v2] test, then you can
specify a filter which controls which test cases actually run:

<pre class="prettyprint">
<code class="devsite-terminal">fx test <var>TEST_NAME</var> -- --test-filter FooTest*</code>
</pre>

Note that there must be a space between `--test-filter` and the filter; you cannot
use `=`. The filter is a [glob style pattern][rust-glob-syntax].

#### Implementation

When `fx test` runs your test component, `fx test` calls `run-test-component`
on the target device with the test url to run the test.


## Run tests (Legacy)

Tests can be exercised with the `fx run-test` command by providing the name of
the package containing the tests.

<pre class="prettyprint">
<code class="devsite-terminal">fx run-test <var>TEST_PACKAGE_NAME</var></code>
</pre>

This command will rebuild any modified files, push the named package to the
device, and run it.

Tests can also be run directly from the shell on a Fuchsia device with the
`run-test-component` command, which can take either a fuchsia-pkg URL or a
prefix to search pkgfs for.

If using a fuchsia-pkg URL the test will be automatically updated on the device,
but not rebuilt like if `fx run-test` was used. The test will be neither rebuilt
nor updated if a prefix is provided.

In light of the above facts, the recommended way to run tests from a Fuchsia
shell is:

<pre class="prettyprint">
<code class="devsite-terminal">fx shell run-test-component `locate <var>TEST_PACKAGE_NAME</var>`</code>
</pre>

The `locate` tool will search for and return fuchsia-pkg URLs based on a given
search query. If there are multiple matches for the query the above command will
fail, so `locate` should be invoked directly to discover the URL that should be
provided to `run-test-component`.

`run-test-component` will create transient isolated storage for the test. See
[isolated-storage][isolated-storage] for more info.

<!-- Reference links -->

[tests-as-components]: /docs/concepts/testing/tests_as_components.md
[component_manifest]: /docs/concepts/components/v1/component_manifests.md
[rust_testing]: ../languages/rust/testing.md
[test_package]: /docs/concepts/testing/test_component.md
[isolated-storage]: /docs/concepts/testing/test_component.md#isolated_storage
[fuchsia_package_url]: /docs/concepts/packages/package_url.md
[glossary-components-v2]: /docs/glossary.md#components-v2
[rust-glob-syntax]: https://docs.rs/glob/0.3.0/glob/struct.Pattern.html
