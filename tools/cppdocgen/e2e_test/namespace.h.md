# Namespace testing

[Header source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h)


This tests that the basic stuff works inside a namespace. The main thing is that the output be
properly qualified and that links to other items are correct.
## EnumInsideNamespace Enum {:#myns::EnumInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#21)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> myns {

<span class="kwd">enum</span> <span class="typ">EnumInsideNamespace</span> {
  kValue1, <span class="com">// = 0</span>
  kValue2, <span class="com">// = 1</span>
};

}  <span class="com">// namespace myns</span>
</pre>

Here is a link to <code><a href="namespace.h.md#myns::StructInsideNamespace">myns::StructInsideNamespace</a></code>.


## myns::ClassInsideNamespace class {:#myns::ClassInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#34)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> myns {

<span class="kwd">class</span> <span class="typ">ClassInsideNamespace</span> { <span class="com">...</span> };

}  <span class="com">// namespace myns</span>
</pre>

Here is a link to <code><a href="namespace.h.md#myns::FunctionInsideNamespace">myns::FunctionInsideNamespace</a></code>.

### Constructor{:#myns::ClassInsideNamespace::ClassInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#36)

<pre class="devsite-disable-click-to-copy">
ClassInsideNamespace::<b>ClassInsideNamespace</b>();
</pre>


### ClassInsideNamespace::SomeFunction() {:#myns::ClassInsideNamespace::SomeFunction}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#38)

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> ClassInsideNamespace::<b>SomeFunction</b>();
</pre>


## myns::StructInsideNamespace struct {:#myns::StructInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#16)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> myns {

<span class="kwd">struct</span> <span class="typ">StructInsideNamespace</span> {
    <span class="typ">int</span> a;
};

}  <span class="com">// namespace myns</span>
</pre>

Here is a link to <code><a href="namespace.h.md#myns::EnumInsideNamespace">myns::EnumInsideNamespace</a></code>.

## myns::FunctionInsideNamespace() {:#myns::FunctionInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#31)

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> myns::<b>FunctionInsideNamespace</b>();
</pre>

Here is a link to <code><a href="namespace.h.md#myns::ClassInsideNamespace">myns::ClassInsideNamespace</a></code>. In particular, see
[myns::ClassInsideNamespace::SomeFunction].


