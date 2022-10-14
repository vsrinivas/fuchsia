# Namespace testing

[Header source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h)


This tests that the basic stuff works inside a namespace. The main thing is that the output be
properly qualified.
## EnumInsideNamespace Enum {:#myns::EnumInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#19)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> myns {

<span class="kwd">enum</span> <span class="typ">EnumInsideNamespace</span> {
  kValue1, <span class="com">// = 0</span>
  kValue2, <span class="com">// = 1</span>
};

}  <span class="com">// namespace myns</span>
</pre>


## myns::ClassInsideNamespace class {:#myns::ClassInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#29)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> myns {

<span class="kwd">class</span> <span class="typ">ClassInsideNamespace</span> { <span class="com">...</span> };

}  <span class="com">// namespace myns</span>
</pre>

### Constructor{:#myns::ClassInsideNamespace::ClassInsideNamespace}

<pre class="devsite-disable-click-to-copy">
ClassInsideNamespace::<b>ClassInsideNamespace</b>();
</pre>


### ClassInsideNamespace::SomeFunction() {:#myns::ClassInsideNamespace::SomeFunction}

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> ClassInsideNamespace::<b>SomeFunction</b>();
</pre>


## myns::StructInsideNamespace struct {:#myns::StructInsideNamespace}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/namespace.h#15)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">namespace</span> myns {

<span class="kwd">struct</span> <span class="typ">StructInsideNamespace</span> {
    <span class="typ">int</span> a;
};

}  <span class="com">// namespace myns</span>
</pre>

## myns::FunctionInsideNamespace() {:#myns::FunctionInsideNamespace}

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> myns::<b>FunctionInsideNamespace</b>();
</pre>


