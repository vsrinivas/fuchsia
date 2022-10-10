This baseline case showcases an end-to-end implementation of a write-only
key-value store implemented using FIDL. This allows each client to have its own
bespoke key-value store on the server, which it can connect to using the `Store`
protocol, and write to using the `WriteItem` method.

Note: The source code for this example is located at
[//examples/fidl/new/key_value_store/baseline](/examples/fidl/new/key_value_store/baseline).
This directory includes tests exercising the implementation in all supported
languages, which may be run locally by executing the following from
the command line `fx set core.x64 --with=//examples/fidl/new:tests && fx test
keyvaluestore_baseline`.

First, we need to define our interface definitions and test harness. The FIDL,
CML, and realm interface definitions set up a scaffold that arbitrary
implementations can use:

<div>
  <devsite-selector>
    <!-- FIDL -->
    <section>
      <h3>FIDL</h3>
      <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/fidl/key_value_store.test.fidl" %}</pre>
    </section>
    <!-- CML -->
    <section style="padding: 0px;">
      <h3>CML</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/meta/client.cml" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/meta/server.cml" %}</pre>
        </section>
        <section>
          <h3>Realm</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/realm/meta/realm.cml" %}</pre>
        </section>
      </devsite-selector>
    </section>
  </devsite-selector>
</div>

Client and server implementations can then be written in any supported language:

<div>
  <devsite-selector>
    <!-- Rust -->
    <section style="padding: 0px;">
      <h3>Rust</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/rust/client/src/main.rs" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/rust/server/src/main.rs" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Natural) -->
    <section style="padding: 0px;">
      <h3>C++ (Natural)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/cpp_natural/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/cpp_natural/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Wire) -->
    <section style="padding: 0px;">
      <h3>C++ (Wire)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/cpp_wire/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/cpp_wire/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- HLCPP -->
    <section style="padding: 0px;">
      <h3>HLCPP</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/hlcpp/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/baseline/hlcpp/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
  </devsite-selector>
</div>
