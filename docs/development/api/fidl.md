# FIDL API Readability Rubric

[TOC]

## General Advice

This section contains some general advice about defining protocols
in the [Fuchsia Interface Definition Language](../languages/fidl/README.md).

### Protocols not objects

FIDL is a language for defining interprocess communication protocols.  Although
the syntax resembles a definition of an object-oriented interface, the design
considerations are more akin to network protocols than to object systems.  For
example, to design a high-quality protocol, you need to consider bandwidth,
latency, and flow control.  You should also consider that a protocol is more
than just a logical grouping of operations: a protocol also imposes a FIFO
ordering on requests and breaking a protocol into two smaller protocols means
that requests made on the two different protocols can be reordered with respect
to each other.

### Focus on the types

A good starting point for designing your FIDL protocol is to design the data
structures your protocol will use.  For example, a FIDL protocol about
networking would likely contain data structures for various types of IP
addresses and a FIDL protocol about graphics would likely contain data
structures for various geometric concepts.  You should be able to look at the
type names and have some intuition about the concepts the protocol manipulates
and how those concepts might be structured.

### Language neutrality

There are FIDL back ends for many different languages.  You should avoid
over-specializing your FIDL definitions for any particular target language.
Over time, your FIDL protocol is likely to be used by many different languages,
perhaps even some languages that are not even supported today.  FIDL is the
glue that holds the system together and lets Fuchsia support a wide variety of
languages and runtimes.  If you over-specialize for your favorite language, you
undermine that core value proposition.

## Names

```
The Naming of Cats is a difficult matter,
It isn't just one of your holiday games;
  --- T.S. Eliot
```

Names defined in FIDL are used to generate identifiers in each target language.
Some languages attach semantic or conventional meaning to names of various
forms.  For example, in Go, whether the initial letter in an identifier is
capitalized controls the visibility of the identifier.  For this reason, many of
the language back ends transform the names in your library to make them more
appropriate for their target language.  The naming rules in this section are a
balancing act between readability in the FIDL source, usability in each target
language, and consistency across target languages.

Avoid commonly reserved words, such as `goto`.  The language back ends will
transform reserved words into non-reserved identifiers, but these transforms
reduce usability in those languages.  Avoiding commonly reserved words reduces
the frequency with which these transformations are applied.

While some FIDL keywords are also commonly reserved words in target languages,
(such as `struct` in C and C++), and should thus be avoided, other FIDL
keywords, particularly `request` and `handle`, are generally descriptive and
can be used as appropriate.

Names must not contain leading or trailing underscores.  Leading or trailing
underscores have semantic meaning in some languages (e.g., leading underscores
control visibility in Dart) and conventional meaning in other languages (e.g.,
trailing underscores are conventionally used for member variables in C++).
Additionally, the FIDL compiler uses leading and trailing underscores to munge
identifiers to avoid collisions.

Use the term `size` to name a number of bytes. Use the term `count` to name a
number of some other quantity (e.g., the number of items in a vector of
structs).

### Case definitions

Sometimes there is more than one way to decide on how to delimit words in
identifiers.  Our style is as follows:

 * Start with the original phrase in US English (e.g., "Non-Null HTTP Client")
 * Remove any punctuation. ("Non Null HTTP Client")
 * Make everything lowercase ("non null http client")
 * Do one of the following, depending on what style is appropriate for the given
   identifier:
    * Replace spaces with underscores ('_') for _lower snake case_
      (`non_null_http_client`).
    * Capitalize and replace spaces with underscores for _upper snake case_
      (`NON_NULL_HTTP_CLIENT`).
    * Capitalize the first letter of each word and join all words together for
      _upper camel case_ (`NonNullHttpClient`).

#### Usage

The following table maps the case usage to the element:

Element                    | Casing             | Example
---------------------------|--------------------|-----------------
`bits`                     | _upper camel case_ | `InfoFeatures`
bitfield members           | _upper snake case_ | `WLAN_SNOOP`
`const`                    | _upper snake case_ | `MAX_NAMES`
primitive alias            | _lower snake case_ | `hw_partition`
`protocol`                 | _upper camel case_ | `AudioRenderer`
protocol method parameters | _lower snake case_ | `enable_powersave`
protocol methods           | _upper camel case_ | `GetBatteryStatus`
`struct`                   | _upper camel case_ | `KeyboardEvent`
struct members             | _lower snake case_ | `child_pid`
`table`                    | _upper camel case_ | `ComponentDecl`
table members              | _lower snake case_ | `num_rx`
`union`                    | _upper camel case_ | `BufferFormat`
union members              | _lower snake case_ | `vax_primary`
`xunion`                   | _upper camel case_ | `ZirconHandle`
xunion members             | _lower snake case_ | `pdp8_iot`
`enum`                     | _upper camel case_ | `PixelFormat`
enum members               | _upper snake case_ | `RGB_888`

### Libraries

Library names are period-separated lists of identifiers. Portions of the library
name other than the last are also referred to as namespaces.  Each component of
the name is in lowercase and must match the following regular expression:
`[a-z][a-z0-9]*`.

We use these restrictive rules because different target languages have different
restrictions on how they qualify namespaces, libraries, or packages.  We have
selected a conservative least common denominator in order for FIDL to work well
with our current set of target languages and with potential future target
languages.

Prefer functional names (e.g., `fuchsia.media`) over product or code names
(e.g., `fuchsia.amber` or `fuchsia.mozart`).  Product names are appropriate
when the product has some external existence beyond Fuchsia and when the
protocol is specific to that product.  For example, `fuchsia.cobalt` is a
better name for the Cobalt interface protocol than `fuchsia.metrics` because
other metrics implementations (e.g., Firebase) are unlikely to implement the same
protocol.

FIDL libraries defined in the Platform Source Tree (i.e., defined in
fuchsia.googlesource.com) must be in the `fuchsia` top-level namespace (e.g.,
`fuchsia.ui`) unless (a) the library defines portions of the FIDL language
itself or its conformance test suite, in which case the top-level namespace must
be `fidl`, or (b) the library is used only for internal testing and is not
included in the SDK or in production builds, in which case the top-level
namespace must be `test`.

FIDL libraries defined in the Platform Source Tree for the purpose of exposing
hardware functionality to applications must be in the `fuchsia.hardware`
namespace.  For example, a protocol for exposing an ethernet device might
be named `fuchsia.hardware.ethernet.Device`.  Higher-level functionality built
on top of these FIDL protocols does not belong in the `fuchsia.hardware` namespace.
For example, it is more appropriate for network protocols to be under
`fuchsia.net` than `fuchsia.hardware`.

Avoid library names with more than two dots (e.g., `fuchsia.foo.bar.baz`).
There are some cases when a third dot is appropriate, but those cases are rare.
If you use more than two dots, you should have a specific reason for that
choice.  For the case of the `fuchsia.hardware` namespace described above, this
is relaxed to "three" and "four" dots, instead of "two" and "three", to
accomodate the longer namespace.

Prefer to introduce dependencies from libraries with more specific names to
libraries with less specific names rather than the reverse.  For example,
`fuchsia.foo.bar` might depend on `fuchsia.foo`, but `fuchsia.foo` should not
depend on `fuchsia.foo.bar`.  This pattern is better for extensibility because
over time we can add more libraries with more specific names but there are only
a finite number of libraries with less specific names.  Having libraries with
less specific names know about libraries with more specific names privileges the
current status quo relative to the future.

Library names must not contain the following components: `common`, `service`,
`util`, `base`, `f<letter>l`, `zx<word>`.  Avoid these (and other) meaningless
names.  If `fuchsia.foo.bar` and `fuchsia.foo.baz` share a number of concepts
that you wish to factor out into a separate library, consider defining those
concepts in `fuchsia.foo` rather than in `fuchsia.foo.common`.

### Top-level

Avoid repeating the names from the library name.  For example, in the
`fuchsia.process` library, a protocol that launches process should be named
`Launcher` rather than `ProcessLauncher` because the name `process` already
appears in the library name.  In all target languages, top-level names are
scoped by the library name in some fashion.

### Primitive aliases

Primitive aliases must not repeat names from the enclosing library.  In all
target languages, primitive aliases are replaced by the underlying primitive
type and therefore do not cause name collisions.

```fidl
using vaddr = uint64;
```

### Constants

Constant names must not repeat names from the enclosing library.  In all target
languages, constant names are scoped by their enclosing library.

Constants that describe minimum and maximum bounds should use the prefix `MIN_`
and `MAX_`, respectively.

```fidl
const uint64 MAX_NAMES = 32;
```

### Protocols

Protocols are specified with the `protocol` keyword.

Protocols must be noun phrases.
Typically, protocols are named using nouns that suggest an action.  For
example, `AudioRenderer` is a noun that suggests that the protocol is related
to rendering audio.  Similarly, `Launcher` is a noun that suggests that the
protocol is related to launching something.  Protocols can also be passive
nouns, particularly if they relate to some state held by the implementation.
For example, `Directory` is a noun that suggests that the protocol is used for
interacting with a directory held by the implementation.

A protocol may be named using object-oriented design patterns.  For example,
`fuchsia.fonts.Provider` uses the "provider" suffix, which indicates that the
protocol provides fonts (rather than represents a font itself).  Similarly,
`fuchsia.tracing.Controller` uses the "controller" suffix, which indicates that
the protocol controls the tracing system (rather than represents a trace
itself).

The name `Manager` may be used as a name of last resort for a protocol with
broad scope.  For example, `fuchsia.power.Manager`.  However, be warned that
"manager" protocols tend to attract a large amount of loosely related
functionality that might be better factored into multiple protocols.

Protocols must not include the name "service."  All protocols define services.
The term is meaningless.  For example, `fuchsia.net.oldhttp.HttpService`
violates this rubric in two ways.  First, the "http" prefix is redundant with
the library name.  Second, the "service" suffix is banned.
Notice that the successor library simply omits this altogether by being
explicit in naming the service it offers `fuchsia.net.http.Loader`.

#### Methods

Methods must must be verb phrases.
For example, `GetBatteryStatus` and `CreateSession` are verb phrases that
indicate what action the method performs.

Methods on "listener" or "observer" protocols that are called when an event
occurs should be prefixed with `On` and describe the event that occurred in the
past tense.  For example, the `ViewContainerListener` protocol has a method
named `OnChildAttached`.

#### Events

Similarly, events (i.e., unsolicited messages from the server to the client)
should be prefixed with `On` and describe the event that occurred in the past
tense.
For example, the `AudioCapturer` protocol has an event named
`OnPacketCaptured`.

### Structs, unions, xunions, and tables

Structs, unions, xunions, and tables must be noun phrases.
For example, `Point` is a struct that defines a location in space and
`KeyboardEvent` is a struct that defines a keyboard-related event.

### Struct, union, xunion, and table members

Prefer struct, union, xunion, and table member names with a single word when
practical (single-word names render more consistently across target languages).
However, do not be afraid to use multiple words if a single word would be
ambiguous or confusing.

Member names must not repeat names from the enclosing type (or library).  For
example, the `KeyboardEvent` member that contains the time the event was
delivered should be named `time` rather than `event_time` because the name
`event` already appears in the name of the enclosing type.  In all target
languages, member names are scoped by their enclosing type.

### Enums

Enums must be noun phrases.
For example, `PixelFormat` is an enum that defines how colors are encoded
into bits in an image.

### Enum members

Enum member names must not repeat names from the enclosing type (or library).
For example, members of `PixelFormat` enum should be named `ARGB` rather than
`PIXEL_FORMAT_ARGB` because the name `PIXEL_FORMAT` already appears in the name
of the enclosing type.  In all target languages, enum member names are scoped by
their enclosing type.

### Bitfields

Bitfields must be noun phrases.
For example, `InfoFeatures` is a bitfield that indicates which features
are present on an Ethernet interface.

### Bitfield members

Bitfield members must not repeat names from the enclosing type (or library).
For example, members of `InfoFeatures` bitfield should be named `WLAN`
rather than `INFO_FEATURES_WLAN` because the name `INFO_FEATURES` already
appears in the name of the enclosing type.
In all target languages, bitfield member names are scoped by their
enclosing type.

## Organization

### Syntax

 * Use 4 space indents.
 * Never use tabs.
 * Avoid trailing whitespace.
 * Separate declarations for `bits`, `enum`, `protocol`, `struct`, `table`,
   `table`, `union`, and `xunion` constructs from other declarations with
   one blank line (two consecutive newline characters).
 * End files with exactly one newline character.

### Comments

Comments use `///` (three forward slashes). Comments in a library will also
appear in the generated code to ease development when coding against the
library. We say that comments "flow-through" to the target language.

Place comments above the thing being described.  Use reasonably complete
sentences with proper capitalization and periods. Limit comment widths to 80
characters.

For instance:
```fidl
/// A widget displaying violins on the screen.
struct Widget {
    /// A monotonically increasing id, uniquely identifying the widget.
    uint64 id;
    /// Location of the top left corner of the widget.
    Point location;
    ...
};
```

Types or values defined by some external source of truth should be commented
with references to the external thing.  For example, reference the WiFi
specification that describes a configuration structure.  Similarly, if a
structure must match an ABI defined in a C header, reference the C header.

For more information about what your comments should contain, consult the [API
Documentation Rubric](documentation.md).

#### Non flow-through comments

If your comments are meant for library authors, use the simpler comments `//`
(two forward slashes) which do not flow-through to the target language.

When deciding what should be a regular `///` comment versus a non flow-through
comment, keep in mind the following.

Regular comments:

 * descriptions of parameters, arguments, function
 * usage notes

Non flow-through comments:

 * internal "todo" comments
 * copyright notices
 * implementation details

Both style of comments can be combined:
```fidl
/// A widget displaying violins on the screen.
// TODO -- widgets should use UUIDs instead of sequential ids
struct Widget {
    /// A monotonically increasing id, uniquely identifying the widget.
    uint64 id;
    /// Location of the top left corner of the widget.
    ...
};
```

### Files

A library is comprised of one or more files.  The files are stored in a
directory hierarchy with the following conventions:

```fidl
fidl/<library>/[<dir>/]*<file>.fidl
```

The `<library>` directory is named using the dot-separated name of the FIDL
library.  The `<dir>` subdirectories are optional and typically not used for
libraries with less than a dozen files.  This directory structure matches how
FIDL files are included in the Fuchsia SDK.

The division of a library into files has no technical impact on consumers of the
library.  Declarations, including protocols, can reference each other and
themselves throughout the library, regardless of the file in which they appear.
Divide libraries into files to maximize readability.

 * Prefer a DAG dependency diagram for files in a library.

 * Prefer keeping mutually referring definitions textually close to each other,
   ideally in the same file.

 * For complex libraries, prefer defining pure data types or constants in leaf
   files and defining protocols that reference those types together in a trunk
   file.

### Ordinals

Protocols contain a number of methods.  Each method is automatically assigned a
unique 32 bit identifier, called an ordinal.  Servers use the ordinal value
to determine which protocol method should be dispatched.

The compiler determines the ordinal value by hashing the library, protocol, and
method name.  In rare cases, ordinals in the same protocol may collide.  If
this happens, you can use the `Selector` attribute to change the name of the
method the compiler uses for hashing.  The following example will use the method
name "C" instead of the method name "B" for calculating the hash:

```fidl
protocol A {
    [ Selector = "C" ]
    B(string s, bool b);
};
```

Selectors can also be used to maintain backwards compatibility with the wire
format in cases where developers wish to change the name of a method.

### Library structure

Carefully consider how you divide your type and protocol definitions into
libraries.  How you decompose these definitions into libraries has a large
effect on the consumers of these definitions because a FIDL library is the unit
of dependency and distribution for your protocols.

The FIDL compiler requires that the dependency graph between libraries is a DAG,
which means you cannot create a circular dependency across library boundaries.
However, you can create (some) circular dependencies within a library.

To decide whether to decompose a library into smaller libraries, consider the
following questions:

 * Do the customers for the library break down into separate roles that would
   want to use a subset of the functionality or declarations in the library?  If
   so, consider breaking the library into separate libraries that target each
   role.

 * Does the library correspond to an industry concept that has a generally
   understood structure?  If so, consider structuring your library to match the
   industry-standard structure.  For example, Bluetooth is organized into
   `fuchsia.bluetooth.le` and `fuchsia.bluetooth.gatt` to match how these
   concepts are generally understood in the industry.  Similarly,
   `fuchsia.net.http` corresponds to the industry-standard HTTP network
   protocol.

 * Do many other libraries depend upon the library?  If so, check whether those
   incoming dependencies really need to depend on the whole library or whether
   there is a "core" set of definitions that could be factored out of the
   library to receive the bulk of the incoming dependencies.

Ideally, we would produce a FIDL library structure for Fuchsia as a whole that
is a global optimum.  However, Conway's law states that "organizations which
design systems \[...\] are constrained to produce designs which are copies of
the communication structures of these organizations."  We should spend a
moderate amount of time fighting Conway's law.

## Types

As mentioned under "general advice," you should pay particular attention to the
types you used in your protocol definition.

### Be consistent

Use consistent types for the same concept.  For example, use a uint32 or a int32
for a particular concept consistently throughout your library.  If you create a
struct for a concept, be consistent about using that struct to represent the
concept.

Ideally, types would be used consistently across library boundaries as well.
Check related libraries for similar concepts and be consistent with those
libraries.  If there are many concepts shared between libraries, consider
factoring the type definitions for those concepts into a common library.  For
example, `fuchsia.mem` and `fuchsia.math` contain many commonly used types for
representing memory and mathematical concepts, respectively.

### Prefer semantic types

Create structs to name commonly used concepts, even if those concepts could be
represented using primitives.  For example, an IPv4 address is an important
concept in the networking library and should be named using a struct even
through the data can be represented using a primitive:

```fidl
struct Ipv4Address {
    array<uint8>:4 octets;
};
```

In performance-critical target languages, structs are represented in line, which
reduces the cost of using structs to name important concepts.

### Consider using fuchsia.mem.Buffer

A Virtual Memory Object (VMO) is a kernel object that represents a contiguous
region of virtual memory.  VMOs track memory on a per-page basis, which means a
VMO by itself does not track its size at byte-granularity.  When sending memory
in a FIDL message, you will often need to send both a VMO and a size.  Rather
than sending these primitives separately, consider using `fuchsia.mem.Buffer`,
which combines these primitives and names this common concept.

### Specify bounds for vector and string

Most `vector` and `string` declarations should specify a length bound.  Whenever
you omit a length bound, consider whether the receiver of the message would
really want to process arbitrarily long sequences or whether extremely long
sequences represent abuse.

Bear in mind that declarations that lack an upper bound are implicitly bounded
by the maximum message length when sent over a `zx::channel`.  If there really
are use cases for arbitrarily long sequences, simply omitting a bound might not
address those use cases because clients that attempt to provide extremely long
sequences might hit the maximum message length.

To address use cases with arbitrarily large sequences, consider breaking the
sequence up into multiple messages using one of the pagination patterns
discussed below or consider moving the data out of the message itself, for
example into a `fuchsia.mem.Buffer`.

### String encoding, string contents, and length bounds

FIDL `string`s are encoded in [UTF-8](https://en.wikipedia.org/wiki/UTF-8), a
variable-width encoding that uses 1, 2, 3, or 4 bytes per
[Unicode code point](http://unicode.org/glossary/#code_point).

Bindings enforce valid UTF-8 for strings, and strings are therefore not
appropriate for arbitrary binary data. See
[Should I use string or vector?](#should-i-use-string-or-vector).

Because the purpose of length bound declarations is to provide an easily
calculable upper bound on the total byte size of a FIDL message, `string` bounds
specify the maximum _number of bytes_ in the field. To be on the safe side, you
will generally want to budget <code>(4 bytes · <var>code points in
string</var>)</code>. (If you know for certain that the text only uses code
points in the single-byte ASCII range, as in the case of phone numbers or credit
card numbers, 1 byte per code point will be sufficient.)

How many code points are in a string? This question can be complicated to
answer, particularly for user-generated string contents, because there is not
necessarily a one-to-one correspondence between a Unicode code point and what
users might think of as "characters".

For example, the string

```
á
```

is rendered as a single user-perceived "character", but actually consists of two
code points:

```
1. LATIN SMALL LETTER A (U+0061)
2. COMBINING ACUTE ACCENT (U+0301)
```

In Unicode terminology, this kind of user-perceived "character" is known as a
[grapheme cluster](https://unicode.org/reports/tr29/#Grapheme_Cluster_Boundaries).

A single grapheme cluster can consist of arbitrarily many code points. Consider
this longer example:

```
á🇨🇦b👮🏽‍♀️
```

If your system and fonts support it, you should see **four grapheme clusters**
above:

```
1. 'a' with acute accent
2. emoji of Canadian flag
3. 'b'
4. emoji of a female police officer with a medium skin tone
```

These four grapheme clusters are encoded as **ten code points**:

```
 1. LATIN SMALL LETTER A (U+0061)
 2. COMBINING ACUTE ACCENT (U+0301)
 3. REGIONAL INDICATOR SYMBOL LETTER C (U+1F1E8)
 4. REGIONAL INDICATOR SYMBOL LETTER A (U+1F1E6)
 5. LATIN SMALL LETTER B (U+0062)
 6. POLICE OFFICER (U+1F46E)
 7. EMOJI MODIFIER FITZPATRICK TYPE-4 (U+1F3FD)
 8. ZERO WIDTH JOINER (U+200D)
 9. FEMALE SIGN (U+2640)
10. VARIATION SELECTOR-16 (U+FE0F)
```

In UTF-8, this string takes up **28 bytes**.

From this example, it should be clear that if your application's UI displays a
text input box that allows _N_ arbitrary grapheme clusters (what users think of
as "characters"), and you plan to transport those user-entered strings over
FIDL, you will have to budget _some multiple_ of <code>4·<var>N</var></code> in
your FIDL `string` field.

What should that multiple be? It depends on your data. If you're dealing with a
fairly constrained use case (e.g. human names, postal addresses, credit card
numbers), you might be able to assume 1-2 code points per grapheme cluster. If
you're building a chat client where emoji use is rampant, 4-5 code points per
grapheme cluster might be safer. In any case, your input validation UI should
show clear visual feedback so that users aren't surprised if they run out of
room.

### Integer types

Select an integer type appropriate for your use case and be consistent about how
you use them.  If your value is best thought of as a byte of data, use `byte`.
If a negative value has no meaning, use an unsigned type.  As a rule of thumb if
you're unsure, use 32-bit values for small quantities and 64-bit values for
large ones.

### How should I represent errors?

Select the appropriate error type for your use case and be consistent about how
you report errors.

Use the `status` type for errors related to kernel objects or IO.  For example,
`fuchsia.process` uses `status` because the library is largely concerned with
manipulating kernel objects.  As another example, `fuchsia.io` uses `status`
extensively because the library is concerned with IO.

Use a domain-specific enum error type for other domains.  For example, use an
enum when you expect clients to receive the error and then stop rather than
propagate the error to another system.

There are two patterns for methods that can return a result or an error:

 * Prefer using the `error` syntax to clearly document and convey a
   possible erroneous return, and take advantage of tailored target language
   bindings;

 * Use the
   [optional value with error enum](#using-optional-value-with-error-enum)
   for cases when you need maximal performance.

The performance difference between the [error syntax](#using-the-error-syntax)
vs [optional value with error enum](#using-optional-value-with-error-enum) are
small:

  * Slightly bigger payload (8 extra bytes for values, 16 extra bytes for
    errors);
  * Since the value and error will be in an envelope, there is additional work
    to record/verify the number of bytes and number of handles;
  * Both will represent the value out-of-line, and therefore require a pointer
    indirection.

### Using the error syntax

Methods can take an optional `error <type>` specifier to indicate that they
return a value, or error out and produce `<type>`. Here is an example:

```fidl
// Only erroneous status are listed
enum MyErrorCode {
    ERR_MISSING_FOO = 1;  // avoid using 0
    ERR_NO_BAR = 2;
    ...
};

protocol Frobinator {
    1: Frobinate(...) -> (FrobinateResult value) error MyErrorCode;
};
```

When using this pattern, you can either use an `int32`, `uint32`, or an enum
thereof to represent the kind of error returned. In most cases, returning an
enum is the preferred approach. As noted in the [enum](#enum) section, it is best
to avoid using the value `0`.

#### Using optional value with error enum

When maximal performance is required, defining a method with two returns, an
optional value and an error code, is common practice. See for instance:

```fidl
enum MyErrorCode {
    OK = 0;               // The success value should be 0,
    ERR_MISSING_FOO = 1;  // with erroneous status next.
    ERR_NO_BAR = 2;
    ...
};

protocol Frobinator {
    1: Frobinate(...) -> (FrobinateResult? value, MyErrorCode err);
};
```

When using this pattern, returning an enum is the preferred approach. Here,
defining the `0` value as the "success" is best. For further details, refer
to the [enum](#enum) section.

#### Avoid messages and descriptions in errors

In some unusual situations, protocols may include a string description of the
error in addition to a `status` or enum value if the range of possible error
conditions is large and descriptive error messages are likely to be useful to
clients.  However, including a string invites difficulties.  For example,
clients might try to parse the string to understand what happened, which means
the exact format of the string becomes part of the protocol, which is
especially problematic when the strings are localized.  *Security note:*
Similarly, reporting stack traces or exception messages to the client can
unintentionally leak privileged information.

### Should I define a struct to encapsulate method parameters (or responses)?

Whenever you define a method, you need to decide whether  to pass parameters
individually or to encapsulate the parameters in a struct.  Making the best
choice involves balancing several factors.  Consider the questions below to help
guide your decision making:

 * Is there a meaningful encapsulation boundary?  If a group of parameters makes
   sense to pass around as a unit because they have some cohesion beyond this
   method, you might want to encapsulate those parameters in a struct.
   (Hopefully, you have already identified these cohesive groups when you
   started designing your protocol because you followed the "general advice"
   above and focused on the types early on.)

 * Would the struct be useful for anything beyond the method being called?  If
   not, consider passing the parameters separately.

 * Are you repeating the same groups of parameters in many methods?  If so,
   consider grouping those parameters into one or more structures.  You might
   also consider whether the repetition indicates that these parameters are
   cohesive because they represent some important concept in your protocol.

 * Are there a large number of parameters that are optional or otherwise are
   commonly given a default value?  If so, consider using use a struct to reduce
   boilerplate for callers.

 * Are there groups of parameters that are always null or non-null at the same
   time?  If so, consider grouping those parameters into a nullable struct to
   enforce that invariant in the protocol itself.  For example, the
   `FrobinateResult` struct defined above contains values that are always null
   at the same time when `error` is not `MyError.OK`.

### Should I use string or bytes?

In FIDL, `string` data must be valid UTF-8, which means strings can represent
sequences of Unicode code points but cannot represent arbitrary binary data.  In
contrast, `bytes` or `array<uint8>` can represent arbitrary binary data and do
not imply Unicode.

Use `string` for text data:

 * Use `string` to represent package names because package names are required to
   be valid UTF-8 strings (with certain excluded characters).

 * Use `string` to represent file names within packages because file names
   within packages are required to be valid UTF-8 strings (with certain excluded
   characters).

 * Use `string` to represent media codec names because media codec names are
   selected from a fixed vocabulary of valid UTF-8 strings.

 * Use `string` to represent HTTP methods because HTTP methods are comprised of
   a fixed selection of characters that are always valid UTF-8.

Use `bytes` or `array<uint8>` for small non-text data:

 * Use `bytes` for HTTP header fields because HTTP header fields do not
   specify an encoding and therefore cannot necessarily be represented in UTF-8.

 * Use `array<uint8>:6` for MAC addresses because MAC address are binary data.

 * Use `array<uint8>:16` for UUIDs because UUIDs are (almost!) arbitrary binary
   data.

Use shared-memory primitives for blobs:

 * Use `fuchsia.mem.Buffer` for images and (large) protobufs, when it makes
   sense to buffer the data completely.
 * Use `handle<socket>` for audio and video streams because data may arrive over
   time, or when it makes sense to process data before completely written or
   available.

### Should I use vector or array?

A `vector` is a variable-length sequence that is represented out-of-line in the
wire format.  An `array` is a fixed-length sequence that is represented in-line
in the wire format.

Use `vector` for variable-length data:

 * Use `vector` for tags in log messages because log messages can have between
   zero and five tags.

Use `array` for fixed-length data:

 * Use `array` for MAC addresses because a MAC address is always six bytes long.

### Should I use a struct or a table?

Both structs and tables represent an object with multiple named fields. The
difference is that structs have a fixed layout in the wire format, which means
they *cannot* be modified without breaking binary compatibility. By contrast,
tables have a flexible layout in the wire format, which means fields *can* be
added to a table over time without breaking binary compatibility.

Use structs for performance-critical protocol elements or for protocol elements
that are very unlikely to change in the future. For example, use a struct to
represent a MAC address because the structure of a MAC address is very unlikely
to change in the future.

Use tables for protocol elements that are likely to change in the future.  For
example, use a table to represent metadata information about camera devices
because the fields in the metadata are likely to evolve over time.

### How should I represent constants?

There are three ways to represent constants, depending on the flavor of
constant you have:

1. Use `const` for special values, like **PI**, or **MAX_NAME_LEN**.
2. Use `enum` when the values are elements of a set, like the repeat
   mode of a media player: **OFF**, **SINGLE_TRACK**, or **ALL_TRACKS**.
3. Use `bits` for constants forming a group of flags, such as the capabilities
   of an interface: **WLAN**, **SYNTH**, and **LOOPBACK**.

#### const

Use a `const` when there is a value that you wish to use symbolically
rather than typing the value every time.
The classical example is **PI** &mdash; it's often coded as a `const`
because it's convenient to not have to type `3.141592653589` every time
you want to use this value.

Alternatively, you may use a `const` when the value may change, but needs
to otherwise be used consistently throughout.
A maximum number of characters that can be supplied in a given field is
a good example (e.g., **MAX_NAME_LEN**).
By using a `const`, you centralize the definition of that number, and
thus don't end up with different values throughout your code.

Another reason to choose `const` is that you can use it both to constrain
a message, and then later on in code.
For example:

```fidl
const int32 MAX_BATCH_SIZE = 128;

protocol Sender {
    Emit(vector<uint8>:MAX_BATCH_SIZE batch);
};
```

You can then use the constant `MAX_BATCH_SIZE` in your code to assemble
batches.

#### enum

Use an enum if the set of enumerated values is bounded and controlled by the
Fuchsia project.  For example, the Fuchsia project defines the pointer event
input model and therefore controls the values enumerated by `PointerEventPhase`.

In some scenarios, you should use an enum even if the Fuchsia project itself
does not control the set of enumerated values if we can reasonably expect that
people who will want to register new values will submit a patch to the Fuchsia
source tree to register their values.  For example, texture formats need to be
understood by the Fuchsia graphics drivers, which means new texture formats can
be added by developers working on those drivers even if the set of texture
formats is controlled by the graphics hardware vendors.  As a counter example,
do not use an enum to represent HTTP methods because we cannot reasonably expect
people who use novel HTTP methods to submit a patch to the Platform Source Tree.

For _a priori_ unbounded sets, a `string` might be a more appropriate choice if
you foresee wanting to extend the set dynamically.  For example, use a `string`
to represent media codec names because intermediaries might be able to do
something reasonable with a novel media code name.

If the set of enumerated values is controlled by an external entity, use an
integer (of an appropriate size) or a `string`.  For example, use an integer (of
some size) to represent USB HID identifiers because the set of USB HID
identifiers is controlled by an industry consortium.  Similarly, use a `string`
to represent a MIME type because MIME types are controlled (at least in theory)
by an IANA registry.

We recommend that, where possible, developers avoid use of `0` as an enum value.
Because many target languages use `0` as the default value for integers, it can
be difficult for to distinguish whether a `0` value was set intentionally, or
instead was set because it is the default. For instance, the
`fuchsia.module.StoryState` defines three values:  `RUNNING` with value `1`,
`STOPPING` with value `2`, and `STOPPED` with value `3`.

There are two cases where using the value `0` is appropriate:

  * The enum has a natural default, initial, or unknown state;

  * The enum defines an error code used in the
    [optional value with error enum](#using-optional-value-with-error-enum)
    pattern.

#### bits

If your protocol has a bitfield, represent its values using `bits` values
(for details, see [`FTP-025`: "Bit Flags."][ftp-025])

For example:

```fidl
// Bit definitions for Info.features field

bits InfoFeatures : uint32 {
    WLAN = 0x00000001;      // If present, this device represents WLAN hardware
    SYNTH = 0x00000002;     // If present, this device is synthetic (not backed by h/w)
    LOOPBACK = 0x00000004;  // If present, this device receives all messages it sends
};
```

This indicates that the `InfoFeatures` bit field is backed by an unsigned 32-bit
integer, and then goes on to define the three bits that are used.

You can also express the values in binary (as opposed to hex) using the `0b`
notation:

```fidl
bits InfoFeatures : uint32 {
    WLAN =     0b00000001;  // If present, this device represents WLAN hardware
    SYNTH =    0b00000010;  // If present, this device is synthetic (not backed by h/w)
    LOOPBACK = 0b00000100;  // If present, this device receives all messages it sends
};
```

This is the same as the previous example.

## Good Design Patterns

This section describes several good design patterns that recur in many FIDL
protocols.

### Protocol request pipelining

One of the best and most widely used design patterns is _protocol request
pipelining_.  Rather than returning a channel that supports a protocol, the
client sends the channel and requests the server to bind an implementation of
the protocol to that channel:

```fidl
GOOD:
protocol Foo {
    GetBar(string name, request<Bar> bar);
};

BAD:
protocol Foo {
    GetBar(string name) -> (Bar bar);
};
```

This pattern is useful because the client does not need to wait for a round-trip
before starting to use the `Bar` protocol.  Instead, the client can queue
messages for `Bar` immediately.  Those messages will be buffered by the kernel
and processed eventually once an implementation of `Bar` binds to the protocol
request.  By contrast, if the server returns an instance of the `Bar` protocol,
the client needs to wait for the whole round-trip before queuing messages for
`Bar`.

If the request is likely to fail, consider extending this pattern with a reply
that describes whether the operation succeeded:

```fidl
protocol CodecProvider {
    TryToCreateCodec(CodecParams params, request<Codec> codec) -> (bool succeed);
};
```

To handle the failure case, the client waits for the reply and takes some other
action if the request failed.  Another approach is for the protocol to have an
event that the server sends at the start of the protocol:

```fidl
protocol Codec2 {
    -> OnReady();
};

protocol CodecProvider2 {
    TryToCreateCodec(CodecParams params, request<Codec2> codec);
};
```

To handle the failure case, the client waits for the `OnReady` event and takes
some other action if the `Codec2` channel is closed before the event arrives.

However, if the request is likely to succeed, having either kind of success
signal can be harmful because the signal allows the client to distinguish
between different failure modes that often should be handled in the same way.
For example, the client should treat a service that fails immediately after
establishing a connection in the same way as a service that cannot be reached in
the first place.  In both situations, the service is unavailable and the client
should either generate an error or find another way to accomplishing its task.

### Flow Control

FIDL messages are buffered by the kernel.  If one endpoint produces more
messages than the other endpoint consumes, the messages will accumulate in the
kernel, taking up memory and making it more difficult for the system to recover.
Instead, well-designed protocols should throttle the production of messages to
match the rate at which those messages are consumed, a property known as _flow
control_.

The kernel provides some amount of flow control in the form of back pressure on
channels.  However, most protocols should have protocol-level flow control and
use channel back pressure as a backstop to protect the rest of the system when
the protocol fails to work as designed.

Flow control is a broad, complex topic, and there are a number of effective
design patterns.  This section discusses some of the more popular flow control
patterns but is not exhaustive.  Protocols are free to use whatever flow control
mechanisms best suit their use cases, even if that mechanism is not listed
below.

#### Prefer pull to push

Without careful design, protocols in which the server pushes data to the client
often have poor flow control.  One approach to providing better flow control is
to have the client pull one or a range from the server.  Pull models have
built-in flow control the client naturally limits the rate at which the server
produces data and avoids getting overwhelmed by messages pushed from the server.

A simple way to implement a pull-based protocol is to "park a callback" with the
server using the _hanging get pattern_.  In this pattern, the client sends a
`GetFoo` message, but the server does not reply immediately.  Instead, the
server replies when a "foo" is available.  The client consumes the foo and
immediately sends another hanging get.  The client and server each do one unit
of work per data item, which means neither gets ahead of the other.

The hanging get pattern works well when the set of data items being transferred
is bounded in size and the server-side state is simple, but does not work well
in situations where the client and server need to synchronize their work.

#### Throttle push using acknowledgements

One approach to providing flow control in protocols that use the push, is the
_acknowledgment pattern_, in which the caller provides an acknowledgement
response that the caller uses for flow control.  For example, consider this
generic listener protocol:

```fidl
protocol Listener {
    OnBar(...) -> ();
};
```

The listener is expected to send an empty response message immediately upon
receiving the `OnBar` message.  The response does not convey any data to the
caller.  Instead, the response lets the caller observe the rate at which the
callee is consuming messages.  The caller should throttle the rate at which it
produces messages to match the rate at which the callee consumes them.  For
example, the caller might arrange for only one (or a fixed number) of messages
to be in flight (i.e., waiting for acknowledgement).

#### Events

In FIDL, servers can send clients unsolicited messages called _events_.
Protocols that use events need to provide particular attention to flow control
because the event mechanism itself does not provide any flow control.

A good use case for events is when at most one instance of the event will be
sent for the lifetime of the channel.  In this pattern, the protocol does not
need any flow control for the event:

```fidl
protocol DeathWish {
    -> OnFatalError(status error_code);
};
```

Another good use case for events is when the client requests that the server
produce events and when the overall number of events produced by the server is
bounded.  This pattern is a more sophisticated version of the hanging get
pattern in which the server can respond to the "get" request a bounded number of
times (rather than just once):

```fidl
protocol NetworkScanner {
    ScanForNetworks();
    -> OnNetworkDiscovered(string network);
    -> OnScanFinished();
};
```

If there is no a priori bound on the number of events, consider having the
client acknowledge the events by sending a message.  This pattern is a more
awkward version of the acknowledgement pattern in which the roles of client and
server are switched.  As in the acknowledgement pattern, the server should
throttle event production to match the rate at which the client consumes the
events:

```fidl
protocol View {
    -> OnInputEvent(InputEvent event);
    NotifyInputEventHandled();
};
```

One advantage to this pattern over the normal acknowledgement pattern is that
the client can more easily acknowledge multiple events with a single message
because the acknowledgement is disassociated from the event being acknowledged.
This pattern allows for more efficient batch processing by reducing the volume
of acknowledgement messages and works well for in-order processing of multiple
event types:

```fidl
protocol View {
    -> OnInputEvent(InputEvent event, uint64 seq);
    -> OnFocusChangedEvent(FocusChangedEvent event, uint64 seq);
    NotifyEventsHandled(uint64 last_seq);
};
```

### Feed-forward dataflow

Some protocols have _feed-forward dataflow_, which avoids round-trip latency by
having data flow primarily in one direction, typically from client to server.
The protocol only synchronizes the two endpoints when necessary.  Feed-forward
dataflow also increases throughput because fewer total context switches are
required to perform a given task.

The key to feed-forward dataflow is to remove the need for clients to wait for
results from prior method calls before sending subsequent messages.  For
example, protocol request pipelining removes the need for the client to wait
for the server to reply with a protocol before the client can use the
protocol.  Similarly, client-assigned identifiers (see below) removes the need
for the client to wait for the server to assign identifiers for state held by
the server.

Typically, a feed-forward protocol will involve the client submitting a sequence
of one-way method calls without waiting for a response from the server.  After
submitting these messages, the client explicitly synchronizes with the server by
calling a method such as `Commit` or `Flush` that has a reply.  The reply might
be an empty message or might contain information about whether the submitted
sequence succeeded.  In more sophisticated protocols, the one-way messages are
represented as a union of command objects rather than individual method calls,
see the _command union pattern_ below.

Protocols that use feed-forward dataflow work well with optimistic error
handling strategies.  Rather than having the server reply to every method with a
status value, which encourages the client to wait for a round trip between each
message, instead include a status reply only if the method can fail for reasons
that are not under the control of the client.  If the client sends a message
that the client should have known was invalid (e.g., referencing an invalid
client-assigned identifier), signal the error by closing the connection.  If the
client sends a message the client could not have known was invalid, either
provide a response that signals success or failure (which requires the client to
synchronize) or remember the error and ignore subsequent dependent requests
until the client synchronizes and recovers from the error in some way.

Example:

```fidl
protocol Canvas {
    Flush() -> (status code);
    Clear();
    UploadImage(uint32 image_id, Image image);
    PaintImage(uint32 image_id, float x, float y);
    DiscardImage(uint32 image_id);
    PaintSmileyFace(float x, float y);
    PaintMoustache(float x, float y);
};
```

### Client-assigned identifiers

Often a protocol will let a client manipulate multiple pieces of state held by
the server.  When designing an object system, the typical approach to this
problem is to create separate objects for each coherent piece of state held by
the server.  However, when designing a protocol, using separate objects for each
piece of state has several disadvantages:

Creating separate protocol instances for each logical object consumes kernel
resources because each instance requires a separate channel object.
Each instance maintains a separate FIFO queue of messages.  Using
separate instances for each logical object means that messages sent
to different objects can be reordered with respect to each other, leading to
out-of-order interactions between the client and the server.

The _client-assigned identifier pattern_ avoids these problems by having the
client assign `uint32` or `uint64` identifiers to objects retained by the server.
All the messages exchanged between the client and the server are funnelled
through a single protocol instance, which provides a consistent FIFO ordering
for the whole interaction.

Having the client (rather than the server) assign the identifiers allows for
feed-forward dataflow because the client can assign an identifier to an object
and then operate on that object immediately without waiting for the server to
reply with the object's identifier.  In this pattern, the identifiers are valid
only within the scope of the current connection, and typically the zero
identifier is reserved as a sentinel.  *Security note:* Clients should not use
addresses in their address space as their identifiers because these addresses
can leak the layout of their address space.

The client-assigned identifier pattern has some disadvantages.  For example,
clients are more difficult to author because clients need to manage their own
identifiers.  Developers commonly want to create a client library that provides
an object-oriented facades for the service to hide the complexity of managing
identifiers, which itself is an anti-pattern (see _client libraries_ below).

A strong signal that you should create a separate protocol instance to
represent an object rather than using a client-assigned identifier is when you
want to use the kernel's object capability system to protect access to that
object.  For example, if you want a client to be able to interact with an object
but you do not want the client to be able to interact with other objects,
creating a separate protocol instance means you can use the underlying channel
as a capability that controls access to that object.

### Command union

In protocols that use feed-forward dataflow, the client often sends many one-way
messages to the server before sending a two-way synchronization message.  If the
protocol involves a particularly high volume of messages, the overhead for
sending a message can become noticeable.  In those situations, consider using
the _command union pattern_ to batch multiple commands into a single message.

In this pattern, the client sends a `vector` of commands rather than sending an
individual message for each command.  The vector contains a union of all the
possible commands, and the server uses the union tag as the selector for command
dispatch in addition to using the method ordinal number:

```fidl
struct PokeCmd { int32 x; int32 y; };

struct ProdCmd { string:64 message; };

union MyCommand {
    PokeCmd poke;
    ProdCmd prod;
};

protocol HighVolumeSink {
  Enqueue(vector<MyCommand> commands);
  Commit() -> (MyStatus result);
};
```

Typically the client buffers the commands locally in its address space and sends
them to the server in a batch.  The client should flush the batch to the server
before hitting the channel capacity limits in either bytes and handles.

For protocols with even higher message volumes, consider using a ring buffer in
a `zx::vmo` for the data plane and an associated `zx::fifo` for the control
plane.  Such protocols place a higher implementation burden on the client and
the server but are appropriate when you need maximal performance.  For example,
the block device protocol uses this approach to optimize performance.

### Pagination

FIDL messages are typically sent over channels, which have a maximum message
size.  In many cases, the maximum message size is sufficient to transmit
reasonable amounts of data, but there are use cases for transmitting large (or
even unbounded) amounts of data.  One way to transmit a large or unbounded
amount of information is to use a _pagination pattern_.

#### Paginating Writes

A simple approach to paginating writes to the server is to let the client send
data in multiple messages and then have a "finalize" method that causes the
server to process the sent data:

```fidl
protocol Foo {
    AddBars(vector<Bar> bars);
    UseTheBars() -> (...);
};
```

For example, this pattern is used by `fuchsia.process.Launcher` to let the
client send an arbitrary number of environment variables.

A more sophisticated version of this pattern creates a protocol that
represents the transaction, often called a _tear-off protocol:

```fidl
protocol BarTransaction {
    Add(vector<Bar> bars);
    Commit() -> (...);
};

protocol Foo {
    StartBarTransaction(request<BarTransaction> transaction);
};
```

This approach is useful when the client might be performing many operations
concurrently and breaking the writes into separate messages loses atomicity.
Notice that `BarTransaction` does not need an `Abort` method.  The better
approach to aborting the transaction is for the client to close the
`BarTransaction` protocol.

#### Paginating Reads

A simple approach to paginating reads from the server is to let the server send
multiple responses to a single request using events:

```fidl
protocol EventBasedGetter {
    GetBars();
    -> OnBars(vector<Bar> bars);
    -> OnBarsDone();
};
```

Depending on the domain-specific semantics, this pattern might also require a
second event that signals when the server is done sending data.  This approach
works well for simple cases but has a number of scaling problems.  For example,
the protocol lacks flow control and the client has no way to stop the server if
the client no longer needs additional data (short of closing the whole
protocol).

A more robust approach uses a tear-off protocol to create an iterator:

```fidl
protocol BarIterator {
    GetNext() -> (vector<Bar> bars);
};

protocol ChannelBasedGetter {
    GetBars(request<BarIterator> iterator);
};
```

After calling `GetBars`, the client uses protocol request pipelining to queue
the first `GetNext` call immediately.  Thereafter, the client repeatedly calls
`GetNext` to read additional data from the server, bounding the number of
outstanding `GetNext` messages to provide flow control.  Notice that the
iterator need not require a "done" response because the server can reply with an
empty vector and then close the iterator when done.

Another approach to paginating reads is to use a token.  In this approach, the
server stores the iterator state on the client in the form of an opaque token,
and the client returns the token to the server with each partial read:

```fidl
struct Token { array<uint8>:16 opaque; }
protocol TokenBasedGetter {
    // If token is null, fetch the first N entries. If token is not null, return
    // the N items starting at token. Returns as many entries as it can in
    // results and populates next_token if more entries are available.
    GetEntries(Token? token) -> (vector<Entry> entries, Token? next_token);
}
```

This pattern is especially attractive when the server can escrow all of its
pagination state to the client and therefore no longer need to maintain
paginations state at all.  The server should document whether the client can
persist the token and reuse it across instances of the protocol.  *Security
note:* In either case, the server must validate the token supplied by the client
to ensure that the client's access is limited to its own paginated results and
does not include results intended for another client.

### Eventpair correlation

When using client-assigned identifiers, clients identify objects held by the
server using identifiers that are meaningful only in the context of their own
connection to the server.  However, some use cases require correlating objects
across clients.  For example, in `fuchsia.ui.scenic`, clients largely interact
with nodes in the scene graph using client-assigned identifiers.  However,
importing a node from another process requires correlating the reference to that
node across process boundaries.

The _eventpair correlation pattern_ solves this problem using a feed-forward
dataflow by relying on the kernel to provide the necessary security.  First, the
client that wishes to export an object creates a `zx::eventpair` and sends one
of the entangled events to the server along with its client-assigned identifier
of the object.  The client then sends the other entangled event to the other
client, which forwards the event to the server with its own client-assigned
identifier for the now-shared object:

```fidl
protocol Foo {
    ExportThing(uint32 client_assigned_id, ..., handle<eventpair> export_token);
};

protocol Bar {
    ImportThing(uint32 some_other_client_assigned_id, ..., handle<eventpair> import_token);
};
```

To correlate the objects, the server calls `zx_object_get_info` with
`ZX_INFO_HANDLE_BASIC` and matches the `koid` and `related_koid` properties from
the entangled event objects.

### Eventpair cancellation

When using tear-off protocol transactions, the client can cancel long-running operations
by closing the client end of the protocol.  The server should listen for
`ZX_CHANNEL_PEER_CLOSED` and abort the transaction to avoid wasting resources.

There is a similar use case for operations that do not have a dedicated channel.
For example, the `fuchsia.net.http.Loader` protocol has a `Fetch` method that
initiates an HTTP request.  The server replies to the request with the HTTP
response once the HTTP transaction is complete, which might take a significant
amount of time.  The client has no obvious way to cancel the request short of
closing the entire `Loader` protocol, which might cancel many other outstanding
requests.

The _eventpair cancellation pattern_ solves this problem by having the client
include one of the entangled events from a `zx::eventpair` as a parameter to the
method.  The server then listens for `ZX_EVENTPAIR_PEER_CLOSED` and cancels the
operation when that signal is asserted.  Using a `zx::eventpair` is better than
using a `zx::event` or some other signal because the `zx::eventpair` approach
implicitly handles the case where the client crashes or otherwise tears down
because the `ZX_EVENTPAIR_PEER_CLOSED` is generated automatically by the kernel
when the entangled event retained by the client is destroyed.

### Empty protocols

Sometimes an empty protocol can provide value.  For example, a method that
creates an object might also receive a `request<FooController>` parameter.  The
caller provides an implementation of this empty protocol:

```fidl
protocol FooController {};
```

The `FooController` does not contain any methods for controlling the created
object, but the server can use the `ZX_CHANNEL_PEER_CLOSED` signal on the
protocol to trigger destruction of the object.  In the future, the protocol
could potentially be extended with methods for controlling the created object.

## Antipatterns

This section describes several antipatterns: design patterns that often provide
negative value.  Learning to recognize these patterns is the first step towards
avoiding using them in the wrong ways.

### Client libraries

Ideally, clients interface with protocols defined in FIDL using
language-specific client libraries generated by the FIDL compiler.
While this approach lets Fuchsia provide high-quality support for a large
number of target languages, sometimes the protocol is too low-level to program directly.
In such cases, it's appropriate to provide a hand-written client library that
interfaces to the same underlying protocol, but is easier to use correctly.

For example, `fuchsia.io` has a client library, `libfdio.so`, which provides a
POSIX-like frontend to the protocol.  Clients that expect a POSIX-style
`open`/`close`/`read`/`write` interface can link against `libfdio.so` and speak
the `fuchsia.io` protocol with minimal modification.  This client library
provides value because the library adapts between an existing library interface
and the underlying FIDL protocol.

Another kind of client library that provides positive value is a framework.  A
framework is an extensive client library that provides a structure for a large
portion of the application.  Typically, a framework provides a significant
amount of abstraction over a diverse set of protocols.  For example, Flutter is
a framework that can be viewed as an extensive client library for the
`fuchsia.ui` protocols.

FIDL protocols should be fully documented regardless of whether the protocol has
an associated client library.  An independent group of software engineers should
be able to understand and correctly use the protocol directly given its
definition without need to reverse-engineer the client library.  When the
protocol has a client library, aspects of the protocol that are low-level and
subtle enough to motivate you to create a client library should be documented
clearly.

The main difficulty with client libraries is that they need to be maintained for
every target language, which tends to mean client libraries are missing (or
lower quality) for less popular languages.  Client libraries also tend to ossify
the underlying protocols because they cause every client to interact with the
server in exactly the same way.  The servers grow to expect this exact
interaction pattern and fail to work correctly when clients deviate from the
pattern used by the client library.

In order to include the client library in the Fuchsia SDK, we should provide
implementations of the library in at least two languages.

### Service hubs

A _service hub_ is a `Discoverable` protocol that simply lets you discover a
number of other protocols, typically with explicit names:

```fidl
BAD:
[Discoverable]
protocol ServiceHub {
    GetFoo(request<Foo> foo);
    GetBar(request<Bar> bar);
    GetBaz(request<Baz> baz);
    GetQux(request<Qux> qux);
};
```

Particularly if stateless, the `ServiceHub` protocol does not provide much
value over simply making the individual protocol services discoverable directly:

```fidl
[Discoverable]
protocol Foo { ... };

[Discoverable]
protocol Bar { ... };

[Discoverable]
protocol Baz { ... };

[Discoverable]
protocol Qux { ... };
```

Either way, the client can establish a connection to the enumerated services.
In the latter case, the client can discover the same services through the normal
mechanism used throughout the system to discover services.  Using the normal
mechanism lets the core platform apply appropriate policy to discovery.

However, service hubs can be useful in some situations.  For example, if the
protocol were stateful or was obtained through some process more elaborate than
normal service discovery, then the protocol could provide value by transferring
state to the obtained services.  As another example, if the methods for
obtaining the services take additional parameters, then the protocol could
provide value by taking those parameters into account when connecting to the
services.

### Overly object-oriented design

Some libraries create separate protocol instances for every logical object in
the protocol, but this approach has a number of disadvantages:

 * Message ordering between the different protocol instances is undefined.
   Messages sent over a single protocol are processed in FIFO order (in each
   direction), but messages sent over different channels race.  When the
   interaction between the client and the server is spread across many channels,
   there is a larger potential for bugs when messages are unexpectedly
   reordered.

 * Each protocol instance has a cost in terms of kernel resources, waiting
   queues, and scheduling.  Although Fuchsia is designed to scale to large
   numbers of channels, the costs add up over the whole system and creating a
   huge proliferation of objects to model every logical object in the system
   places a large burden on the system.

* Error handling and teardown is much more complicated because the number of
  error and teardown states grows exponentially with the number of protocol
  instances involved in the interaction.  When you use a single protocol
  instance, both the client and the server can cleanly shut down the interaction
  by closing the protocol.  With multiple protocol instances, the interaction
  can get into states where the interaction is partially shutdown or where the
  two parties have inconsistent views of the shutdown state.

 * Coordination across protocol boundaries is more complex than within a single
   protocol because multiple protocols need to allow
   for the possibility that different protocols will be used by different
   clients, who might not completely trust each other.

However, there are use cases for separating functionality into multiple
protocols:

 * Providing separate protocols can be beneficial for security because some
   clients might have access to only one of the protocols and thereby be
   restricted in their interactions with the server.

 * Separate protocols can also more easily be used from separate threads.  For
   example, one protocol might be bound to one thread and another protocol
   might be bound to another thread.

 * Clients and servers pay a (small) cost for each method in a protocol.
   Having one giant protocol that contains every possible method can be less
   efficient than having multiple smaller protocols if only a few of the
   smaller protocols are needed at a time.

 * Sometimes the state held by the server factors cleanly along method
   boundaries.  In those cases, consider factoring the protocol into smaller
   protocols along those same boundaries to provide separate protocols for
   interacting with separate state.

A good way to avoid over object-orientation is to use client-assigned
identifiers to model logical objects in the protocol.  That pattern lets clients
interact with a potentially large set of logical objects through a single
protocol.

<!-- xrefs -->
[ftp-025]: https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/languages/fidl/reference/ftp/ftp-025.md
