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

## SimpleTestStructure Struct {:#SimpleTestStructure}

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

