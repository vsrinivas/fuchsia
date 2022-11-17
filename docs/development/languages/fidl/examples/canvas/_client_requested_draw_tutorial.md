<!-- TODO(fxbug.dev/111273): DOCUMENT[canvas/client_requested_draw] no need for a header, just a brief description -->

Note: The source code for this example is located at
[//examples/fidl/new/canvas/client_requested_draw](/examples/fidl/new/canvas/client_requested_draw).
This directory includes tests exercising the implementation in all supported
languages, which may be run locally by executing the following from
the command line: `fx set core.x64 --with=//examples/fidl/new:tests && fx test
canvas_clientrequesteddraw`.

The FIDL, CML, and realm interface definitions are as follows:

<div>
  <devsite-selector>
    <!-- FIDL -->
    <section>
      <h3 id="canvas-client_requested_draw-fidl">FIDL</h3>
      <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/fidl/canvas.test.fidl" highlight="diff_1" %}</pre>
    </section>
    <!-- CML -->
    <section style="padding: 0px;">
      <h3>CML</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="canvas-client_requested_draw-cml-client">Client</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/meta/client.cml" %}</pre>
        </section>
        <section>
          <h3 id="canvas-client_requested_draw-server">Server</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/meta/server.cml" %}</pre>
        </section>
        <section>
          <h3 id="canvas-client_requested_draw-realm">Realm</h3>
          <pre class="prettyprint">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/realm/meta/realm.cml" %}</pre>
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
          <h3 id="canvas-client_requested_draw-rust-client">Client</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/rust/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="canvas-client_requested_draw-rust-server">Server</h3>
          <pre class="prettyprint lang-rust">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/rust/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Natural) -->
    <section style="padding: 0px;">
      <h3>C++ (Natural)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="canvas-client_requested_draw-cpp_natural-client">Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/cpp_natural/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="canvas-client_requested_draw-cpp_natural-server">Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/cpp_natural/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- C++ (Wire) -->
    <section style="padding: 0px;">
      <h3>C++ (Wire)</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="canvas-client_requested_draw-cpp_wire-client">Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/cpp_wire/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="canvas-client_requested_draw-cpp_wire-server">Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/cpp_wire/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
    <!-- HLCPP -->
    <section style="padding: 0px;">
      <h3 id="canvas-client_requested_draw-hlcpp">HLCPP</h3>
      <devsite-selector style="margin: 0px; padding: 0px;">
        <section>
          <h3 id="canvas-client_requested_draw-hlcpp-client">Client</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/hlcpp/TODO.md" region_tag="todo" %}</pre>
        </section>
        <section>
          <h3 id="canvas-client_requested_draw-hlcpp-server">Server</h3>
          <pre class="prettyprint lang-cc">{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/new/canvas/client_requested_draw/hlcpp/TODO.md" region_tag="todo" %}</pre>
        </section>
      </devsite-selector>
    </section>
  </devsite-selector>
</div>
