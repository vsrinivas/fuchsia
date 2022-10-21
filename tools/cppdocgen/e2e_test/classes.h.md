# \<e2e_test/classes.h\> in e2e

[Header source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/classes.h)

## BaseClass1 class {:#BaseClass1}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/classes.h#52)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">class</span> <span class="typ">BaseClass1</span> { <span class="com">...</span> };
</pre>

### BaseClass1::BaseClass1Function() {:#BaseClass1::BaseClass1Function}

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> BaseClass1::<b>BaseClass1Function</b>();
</pre>

Complicated documentation for BaseClass1Function.


## BaseClass2 class {:#BaseClass2}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/classes.h#58)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">class</span> <span class="typ">BaseClass2</span> { <span class="com">...</span> };
</pre>

### BaseClass2::BaseClass2Function() {:#BaseClass2::BaseClass2Function}

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> BaseClass2::<b>BaseClass2Function</b>();
</pre>

Insightful documentation for BaseClass2Function.


## DerivedClass class {:#DerivedClass}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/classes.h#64)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">class</span> <span class="typ">DerivedClass</span> : <span class="kwd">public</span> <span class="typ"><a href="classes.h.md#BaseClass1">GlobalNamespace::BaseClass1</a></span>,
                     <span class="kwd">private</span> <span class="typ"><a href="classes.h.md#BaseClass2">GlobalNamespace::BaseClass2</a></span> { <span class="com">...</span> };
</pre>

### Inherited from [BaseClass1](classes.h.md#BaseClass1)

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> <a href="classes.h.md#BaseClass1::BaseClass1Function"><b>BaseClass1Function</b></a>();
</pre>

### DerivedClass::BaseClass2Function() {:#DerivedClass::BaseClass2Function}

<pre class="devsite-disable-click-to-copy">
<span class="typ">void</span> DerivedClass::<b>BaseClass2Function</b>();
</pre>

An override with documentation. Note that the BaseClass1Function() is not overridden.


## NoDeclarationClass class {:#NoDeclarationClass}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/classes.h#77)

This class should not have a generated declaration becaose of the  annotation.

### NoDeclarationClass::SomeFunction() {:#NoDeclarationClass::SomeFunction}

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> NoDeclarationClass::<b>SomeFunction</b>();
</pre>


## SimpleTestClass class {:#SimpleTestClass}

[Declaration source code](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/tools/cppdocgen/e2e_test/classes.h#8)

<pre class="devsite-disable-click-to-copy">
<span class="kwd">class</span> <span class="typ">SimpleTestClass</span> {
  <span class="kwd">public</span>:
    <span class="com">// Public data members:</span>
    <span class="typ">int</span> public_value;
    <span class="typ">int</span> public_value2;
};
</pre>

### public_value

Some documentation for the public value.

This violates the style guide but should still work.

### public_value2

End-of-line comment. Scary!

### Constructor{:#SimpleTestClass::SimpleTestClass}

<pre class="devsite-disable-click-to-copy">
SimpleTestClass::<b>SimpleTestClass</b>();
SimpleTestClass::<b>SimpleTestClass</b>(<span class="typ">int</span> a);
SimpleTestClass::<b>SimpleTestClass</b>(<span class="typ">int</span> a = 1,
                                 <span class="typ">int</span> b = 2);
</pre>


### SimpleTestClass::FunctionWithNoGeneratedDeclaration() {:#SimpleTestClass::FunctionWithNoGeneratedDeclaration}

This member shouldn't have a declaration because of the  annotation.


### SimpleTestClass::TheFunction() {:#SimpleTestClass::TheFunction}

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> SimpleTestClass::<b>TheFunction</b>();
</pre>

This is a documented pure virtual function.


### SimpleTestClass::value() {:#SimpleTestClass::value}

<pre class="devsite-disable-click-to-copy">
<span class="typ">int</span> SimpleTestClass::<b>value</b>();
</pre>


