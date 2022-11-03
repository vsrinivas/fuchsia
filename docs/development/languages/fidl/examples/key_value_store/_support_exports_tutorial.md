<!-- TODO(fxbug.dev/109277): DOCUMENT[key_value_store/support_exports] no need for a header, just a brief description -->

Note: The source code for this example is located at
[//examples/fidl/new/key_value_store/support_exports](/examples/fidl/new/key_value_store/support_exports).
This directory includes tests exercising the implementation in all supported
languages, which may be run locally by executing the following from
the command line: `fx set core.x64 --with=//examples/fidl/new:tests && fx test
keyvaluestore_supportexports`.

First, we need to define our interface definitions and test harness. The FIDL,
CML, and realm interface definitions set up a scaffold that arbitrary
implementations can use:

<div>
  <devsite-selector>
    <!-- FIDL -->
    <section>
      <h3 id="key_value_store-support_exports-fidl">FIDL</h3>
      <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/fidl/key_value_store.test.fidl" highlight="diff_1,diff_2,diff_3" %}</pre>
    </section>
    <!-- CML -->
    <section style="padding: 0px;">
      <h3>CML</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="key_value_store-support_exports-cml-client">Client</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/meta/client.cml" %}</pre>
        </section>
        <section>
          <h3 id="key_value_store-support_exports-server">Server</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/meta/server.cml" %}</pre>
        </section>
        <section>
          <h3 id="key_value_store-support_exports-realm">Realm</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/realm/meta/realm.cml" %}</pre>
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
          <h3 id="key_value_store-support_exports-rust-client">Client</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/rust/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="key_value_store-support_exports-rust-server">Server</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/rust/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Natural) -->
    <section style="padding: 0px;">
      <h3>C++ (Natural)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="key_value_store-support_exports-cpp_natural-client">Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/cpp_natural/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="key_value_store-support_exports-cpp_natural-server">Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/cpp_natural/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Wire) -->
    <section style="padding: 0px;">
      <h3>C++ (Wire)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="key_value_store-support_exports-cpp_wire-client">Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/cpp_wire/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="key_value_store-support_exports-cpp_wire-server">Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/cpp_wire/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- HLCPP -->
    <section style="padding: 0px;">
      <h3 id="key_value_store-support_exports-hlcpp">HLCPP</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="key_value_store-support_exports-hlcpp-client">Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/hlcpp/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="key_value_store-support_exports-hlcpp-server">Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key_value_store/support_exports/hlcpp/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
  </devsite-selector>
</div>
