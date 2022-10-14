# Custom file header

[Header source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h)


This is the docstring for this file. It should appear at the top of the generated documentation

## Just the basics

This test file contains the basics of the document generator.
## API_FLAG_1 macro {:#API_FLAG_1}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#17)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">API_FLAG_1</span> 1
</pre>

Documentation for the API flag.

## API_FLAG_2 macro {:#API_FLAG_2}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#18)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">#define</span> <span class="lit">API_FLAG_2</span> 2
</pre>


## MySimpleEnum Enum {:#MySimpleEnum}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#38)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">enum</span> <span class="typ">MySimpleEnum</span> {
  kValue1, <span class="com">// = 0</span>
  kValue2, <span class="com">// = 1</span>
};
</pre>

Here is a regular enum with everything implicit.


## A very complex enum. {:#MyFancyEnum}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#47)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">enum class</span> <span class="typ">MyFancyEnum</span> : <span class="typ">char</span> {
  kValue1 = 1,
  kValue2 = 1 + 1, <span class="com">// = 2</span>
};
</pre>

### A very complex enum.

This is a C++ enum class with an explicit type and explicit values. It also has an explicit
title for the docstring.


## SimpleTestStructure struct {:#SimpleTestStructure}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#23)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">struct</span> <span class="typ">SimpleTestStructure</span> {
    <span class="typ">int</span> a;
    <span class="typ">char</span> b;
    <span class="typ">double</span> c;
};
</pre>

This is a structure that defines some values. The values appear inside the structure.

The first value has no docstring, the second one does.

### b

Some end-of-line documentation.

### c

Some documentation for the `b` member of the `SimpleTestStructure`.

## StandaloneUnion union {:#StandaloneUnion}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#32)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">union</span> <span class="typ">StandaloneUnion</span> {
    <span class="typ">int</span> i;
    <span class="typ">double</span> d;
};
</pre>

## UnnamedStructTypedef struct {:#UnnamedStructTypedef}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/basics.h#59)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">struct</span> <span class="typ">UnnamedStructTypedef</span> {
    <span class="typ">int</span> a;
};
</pre>

This tests the C-style thing of defining an unnamed struct and a typedef for it at the same time.

