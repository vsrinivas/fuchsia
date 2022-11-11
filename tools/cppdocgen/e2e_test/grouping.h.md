# \<e2e_test/grouping.h\> in e2e

[Header source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h)

## GROUPED defines {:#GROUPED_ONE}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#31)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">GROUPED_ONE</span> 1
<span class="kwd">#define</span> <span class="lit">GROUPED_TWO</span> 2
</pre>


These are the explicitly grouped defines.

## UNGROUPED_ONE macro {:#UNGROUPED_ONE}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#13)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">UNGROUPED_ONE</span> 1
</pre>

Not grouped defines.

## UNGROUPED_TWO macro {:#UNGROUPED_TWO}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#14)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">UNGROUPED_TWO</span> 2
</pre>


## MyClass class {:#MyClass}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#34)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">class</span> <span class="typ">MyClass</span> { <span class="com">...</span> };
</pre>

### Constructor{:#MyClass::MyClass}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#37)

<pre class="devsite-disable-click-to-copy">
MyClass::<b>MyClass</b>();
MyClass::<b>MyClass</b>(<span class="typ">int</span> a);
</pre>

These two constructors are grouped and this is the comment for them.


[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#40)

<pre class="devsite-disable-click-to-copy">
MyClass::<b>MyClass</b>(<span class="typ">int</span> a,
                 <span class="typ">int</span> b);
</pre>

This constructor will go in a separate section due to this separate documentation.


### Iterator functions {:#MyClass::begin}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#48)

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> MyClass::<b>begin</b>();
<span class="typ">int</span> MyClass::<b>end</b>();
</pre>


These functions are explicitly grouped.


### MyClass::size() {:#MyClass::size}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#42)

<pre class="devsite-disable-click-to-copy">
<span class="typ">int &amp;</span> MyClass::<b>size</b>();
<span class="typ">const int &amp;</span> MyClass::<b>size</b>();
</pre>


## Explicitly grouped functions. {:#GroupedExplicitlyOne}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#24)

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> <b>GroupedExplicitlyOne</b>(<span class="typ">int</span> this_is_a_vary_long_name_that_forces_the_next_line_break,
                          <span class="typ">const char *</span> the_line_breaks_before_here);
<span class="typ">void</span> <b>GroupedExplicitlyTwo</b>(<span class="typ">double</span> a);
</pre>


These functions have no naming similarities but since there is no blank line nor comment between
them, and there is a single comment beginning with a heading, they go into their own section.


## GroupedImplicitly(…) {:#GroupedImplicitly}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#17)

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> <b>GroupedImplicitly</b>(<span class="typ">int</span> a);
<span class="typ">void</span> <b>GroupedImplicitly</b>(<span class="typ">double</span> a);
</pre>

These two are grouped because the name is the same, even though there is no explicit heading.


## UngroupedOne() {:#UngroupedOne}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#9)

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> <b>UngroupedOne</b>();
</pre>

These two are not grouped because there's no heading and the names are not the same.


## UngroupedTwo() {:#UngroupedTwo}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/grouping.h#10)

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> <b>UngroupedTwo</b>();
</pre>


