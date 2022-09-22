The originally write-only key-value store is now extended with the
ability to read items back out of the store.

Note: The source code for this example is located at
[//examples/fidl/new/key-value-store/add_read_item](/examples/fidl/new/key-value-store/add_read_item).
This directory includes tests exercising implementation in all supported
languages, which may be run by exercised locally by running the following from
the command line `fx set core.x64 --with=//examples/fidl/new:tests && fx test
keyvaluestore_addreaditem`.

The changes applied to the FIDL and CML definitions are as follows:

<div>
  <devsite-selector>
    <!-- FIDL -->
    <section>
      <h3>FIDL</h3>
      <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/fidl/key_value_store.test.fidl" highlight="6,7,8,9,10,11,12,13,14,15,16,17,18,27,28,29,30,31,32,41,42,43,44" %}</pre>
    </section>
    <!-- CML -->
    <section style="padding: 0px;">
      <h3>CML</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/meta/client.cml" highlight="26,27,28,29,30,31,32,33" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/meta/server.cml" %}</pre>
        </section>
        <section>
          <h3>Realm</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/realm/meta/realm.cml" %}</pre>
        </section>
      </devsite-selector>
    </section>
  </devsite-selector>
</div>

Client and server implementations for all languages change as well:

<div>
  <devsite-selector>
    <!-- Rust -->
    <section style="padding: 0px;">
      <h3>Rust</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/rust/client/main.rs" highlight="45,46,47,48,49,50,51,52,53,54,55,56,57" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/rust/server/main.rs" highlight="76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Natural) -->
    <section style="padding: 0px;">
      <h3>C++ (Natural)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/cpp-natural/TODO.md" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/cpp-natural/TODO.md" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Wire) -->
    <section style="padding: 0px;">
      <h3>C++ (Wire)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/cpp-wire/TODO.md" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/cpp-wire/TODO.md" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- Dart -->
    <section style="padding: 0px;">
      <h3>Dart</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-dart">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/dart/TODO.md" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-dart">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/dart/TODO.md" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- HLCPP -->
    <section style="padding: 0px;">
      <h3>HLCPP</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3>Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/hlcpp/TODO.md" %}</pre>
        </section>
        <section>
          <h3>Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/key-value-store/add_read_item/hlcpp/TODO.md" %}</pre>
        </section>
      </devsite-selector>
    </section>
  </devsite-selector>
</div>
