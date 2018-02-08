# FIDL 2.0: Language Specification

Status: DRAFT

Author: jeffbrown@google.com

This document is a specification of the Fuchsia Interface Definition Language
(FIDL) v2.0 syntax.

See [FIDL 2.0: Overview](index.md) for more information about FIDL's overall
purpose, goals, and requirements, as well as links to related documents.

**WORK IN PROGRESS**

[TOC]

## Syntax

The Fuchsia Interface Definition Language provides a syntax for declaring named
constants, enums, structs, unions, and interfaces. These declarations are
collected into libraries for distribution.

FIDL declarations are stored in plain text UTF-8 files. Each file consists of a
sequence of semicolon delimited declarations. The order of declarations within a
FIDL file or among FIDL files within a library is irrelevant. FIDL does not
require (or support) forward declarations of any kind.

Topics for discussion:

*   Previously we defined FIDL files as standalone units loosely organized into
    modules which caused problems for languages such as Dart and Rust which
    prefer to work with more granular units, hence the idea of formalizing the
    concept of FIDL libraries. Is this the right approach?

### Tokens

#### Comments

FIDL supports C-style single-line and multiline comments. They may contain UTF-8
content (which is of course ignored).

```
    // this is a single-line comment
    /* this is a multi-
     * line comment */
```

#### Document Comments

TODO: we should think about how we will generate online documentation from FIDL
files. Perhaps the compiler can emit document contents together with the
declarations in a machine-readable FIDL IR format that could be consumed by
other tools.

#### Reserved Words

The following keywords are reserved in FIDL.

```
as, bool, const, enum, event, float32, float64, handle, int8, int16,
int32, int64, interface, library, request, string, struct, uint8,
uint16, uint32, uint64, union, using, vector
```

To use these words as identifiers, they must be escaped by prepending an "@".
For example "interface" is a reserved word but "@interface" is an identifier
whose name is "interface".

#### Identifiers

FIDL identifiers must match the regex "@?[a-zA-Z_][0-9a-zA-Z]*". The "@" prefix,
if present, serves to distinguish identifiers from reserved words in the FIDL
language. The "@" prefix itself is ignored for the purposes of naming the
identifier. This allows reserved words in the FIDL language to nevertheless be
used as identifiers (when escaped with "@").

Identifiers are case-sensitive.

```
    // a library named "foo"
    library foo;

    // a struct named "Foo"
    struct Foo { };

    // a struct named "struct"
    struct @struct { };
```

#### Qualified Identifiers

FIDL always looks for unqualified symbols within the scope of the current
library. To reference symbols in other libraries, they must be qualified by
prefixing the identifier with the library name or alias.

Names may also require qualification when they refer to symbols which have been
declared within the scope of a **struct**, **union**, **enum**, or **interface**.

**objects.fidl:**

```
    library objects;
    using textures as tex;

    interface Frob {
      // "Thing" refers to "Thing" in the "objects" library
      // "tex.Color" refers to "Color" in the "textures" library
      Paint(Thing thing, tex.Color color);
    };

    struct Thing {
      string name;
    };
```

**textures.fidl:**

```
    library textures;

    struct Color {
      uint32 rgba;
    };
```

Topics for discussion:

*   Should we provide shortcuts for referencing types in other libraries? Should
    we support importing symbols into the library's own scope?

#### Literals

FIDL supports the following literal types using C-like syntax: bools, signed
integers, unsigned integers, floats, strings.

```
    const bool kBool = true;
    const int32 kInt = -333;
    const uint32 kUInt = 42;
    const uint64 kDiamond = 0x183c7effff7e3c18;
    const string kString = "a string";
    const float32 kFloat = 1.0;
```

Topics for discussion:

*   Provide more formal definition.
*   Describe special constants like float.NAN and uint32.MAX.

#### Declaration Separator

FIDL uses the semi-colon **';'** to separate adjacent declarations within the
file, much like C. Although it is somewhat of a nuisance, it makes the grammar
more regular (constant, member, and type declarations look the same) which
simplifies parsing.

NB: I am sure there are other viewpoints on this… Ideally the grammar should be
LL(1) but whoever ultimately writes the new compiler can weigh in on what's
simple to parse or not.

### Libraries

Libraries are named containers of FIDL declarations.

Each library has a name consisting of a dot-delimited identifier. Library names
appear in [Qualified Identifiers](#qualified-identifiers).

Libraries may declare that they use other libraries with a "using" declaration.
This allows the library to refer to symbols defined in other libraries upon which
they depend. Symbols which are imported this way may be accessed either by
qualifying them with the library name as in _"mozart.geometry.Rect"_ or by
qualifying them with the library alias as in _"geo.Rect"_.

```
    library mozart.composition;
    using mozart.geometry as geo;
    using mozart.buffers;

    interface Compositor { … };
```

In the source tree, each library consists of a directory with some number of
***.fidl** files. The name of the directory is irrelevant to the FIDL compiler
but by convention it should resemble the library name itself. A directory should
not contain FIDL files for more than one library.

The scope of "library" and "using" declarations is limited to a single file.
Each individual file within a FIDL library must restate the "library"
declaration together with any "using" declarations needed by that file.

When the FIDL compiler encounters a library name such as "mozart.geometry",
searches the library path to find the directory which contains that library's
FIDL files, then loads the files. The compiler emits an error ifs any referenced
library cannot be found.

The build system generally takes care of providing the FIDL compiler with a
suitable library path argument derived from the build dependencies for the
target. eg. _"--library mozart.geometry=apps/mozart/services/geometry --library
mozart.buffers=apps/mozart/services/buffers"_

The library's name may be used by certain language bindings to provide scoping
for symbols emitted by the code generator.

For example, the C++ bindings generator places declarations for the FIDL library
"mozart.composition" within the C++ namespace "mozart::composition". Similarly,
for languages such as Dart and Rust which have their own module system, each
FIDL library is compiled as a module for that language.

Topics for discussion:

*   Are there any attributes we would like to define at the library level? eg.
    C++ namespace or other code generator hints
*   Should the individual FIDL files which make up a library be collated (merged
    into one combined file archive) or is the one-to-one mapping of library to
    directory good enough?
*   Do FIDL libraries need any auxiliary metadata represented separately from the
    FIDL files themselves?

### Types and Type Declarations

#### Primitives

*   Simple value types.
*   Not nullable.

The following primitive types are supported:

    Boolean         **<code>bool</code></strong>


    Signed integer          **<code>int8 int16 int32 int64</code></strong>


    Unsigned integer        **<code>uint8 uint16 uint32 uint64</code></strong>


    IEEE 754 Floating-point **<code>float32 float64</code></strong>

Numbers are suffixed with their size in bits, **<code>bool</code></strong> is 1
byte.

##### Use

```
    // A record which contains fields of a few primitive types.
    struct Sprite {
    float32 x;
    float32 y;
    uint32 index;
    uint32 color;
    bool visible;
    };
```

#### Enums

*   Proper enumerated types; bit fields are not valid enums.
*   Discrete subset of named values chosen from an underlying integer primitive
    type.
*   Not nullable.

##### Declaration

The ordinal index is **required** for each enum element. The underlying type of
an enum must be one of: **int8, uint8, int16, uint16, int32, uint32, int64,
uint64**. If omitted, the underlying type is assumed to be **uint32**.

Enums may be scoped within: **library, struct, union, interface**.

```
    // An enum declared at library scope.
    enum Beverage : uint8 {
        WATER = 0,
        COFFEE = 1,
        TEA = 2,
        WHISKEY = 3,
    };

    // An enum declared at library scope.
    // Underlying type is assumed to be uint32.
    enum Vessel {
        CUP = 0,
        BOWL = 1,
        TUREEN = 2,
        JUG = 3,
    };

    // An enum declared within an interface scope.
    interface VendingMachine {
        enum PaymentMethod {
            CASH = 0,
            CREDIT = 1,
            HONOR_SYSTEM = 2,
        };
    };
```

##### Use

Enum types are denoted by their identifier, which may be qualified if needed.

```
    // A record which contains two enum fields.
    struct Order {
    Beverage beverage;
    Vessel vessel;
    };
```

#### Arrays

*   Fixed-length sequences of homogeneous elements.
*   Elements can be of any type including: primitives, enums, arrays, strings,
    vectors, handles, structs, unions.
*   Not nullable themselves; may contain nullable types.

##### Use

Arrays are denoted **<code>T[n]</code></strong> where <em>T</em> can
be any FIDL type (including an array) and <em>n</em> is a positive
integer constant expression which specified the number of elements in
the array.

```
    // A record which contains some arrays.
    struct Record {
        // array of exactly 16 floating point numbers
    float32[16] matrix;

        // array of exactly 10 arrays of 4 strings each
    string[4][10] form;
    };
```

#### Strings

*   Variable-length sequence of UTF-8 encoded characters representing text.
*   Nullable; null strings and empty strings are distinct.
*   Can specify a maximum size, eg. **<code>string:40</code></strong> for a
    maximum 40 byte string.

##### Use

Strings are denoted as follows:

*   **<code>string</code></strong> : non-nullable string (validation error
    occurs if null is encountered)
*   <strong><code>string?</code></strong> : nullable string
*   <strong><code>string:N, string:N?</code></strong> : string with maximum
    length of <em>N</em> bytes

    ```
    // A record which contains some strings.
    struct Record {
        // title string, maximum of 40 bytes long
        string:40 title;

        // description string, may be null, no upper bound on size
    string? description;
    };

    ```

#### Vectors

*   Variable-length sequence of homogeneous elements.
*   Nullable; null vectors and empty vectors are distinct.
*   Can specify a maximum size, eg. **<code>vector<T>:40</code></strong> for a
    maximum 40 element vector.
*   There is no special case for vectors of bools. Each bool element takes one
    byte as usual.

##### Use

Vectors are denoted as follows:

*   **<code>vector<T></code></strong> : non-nullable vector of element type
    <em>T</em> (validation error occurs if null is encountered)
*   <strong><code>vector<T>?</code></strong> : nullable vector of element type
    <em>T</em>
*   <strong><code>vector<T>:N, vector<T>:N?</code></strong> : vector with
    maximum length of <em>N</em> elements

<em>T</em> can be any FIDL type.

```
    // A record which contains some vectors.
    struct Record {
        // a vector of up to 10 integers
        vector<int32>:10 params;

        // a vector of bytes, no upper bound on size
        vector<uint8> blob;

        // a nullable vector of up to 24 strings
        vector<string>:24? nullable_vector_of_strings;

        // a vector of nullable strings
        vector<string?> vector_of_nullable_strings;

        // a vector of vectors of arrays of floating point numbers
        vector<vector<float32[16]>> complex;
    };
```

#### Handles

*   Transfers a Zircon capability by handle value.
*   Stored as a 32-bit unsigned integer.
*   Nullable by encoding as a zero-valued handle.

##### Use

Handles are denoted:

*   **<code>handle</code></strong> : non-nullable Zircon handle of
    unspecified type
*   <strong><code>handle?</code></strong> : nullable Zircon handle of
    unspecified type
*   <strong><code>handle<H></code></strong> : non-nullable Zircon handle
    of type <em>H</em>
*   <strong><code>handle<H>?</code></strong> : nullable Zircon handle of
    type <em>H</em>

<em>H</em> can be one of[^1]: **<code>channel, event, eventpair, fifo, job,
process, port, resource, socket, thread, vmo</code></strong>

```
    // A record which contains some handles.
    struct Record {
        // a handle of unspecified type
        handle h;

        // an optional channel
        handle<channel>? c;
    };
```

#### Structs

*   Record type consisting of a sequence of typed fields.
*   Declaration is not intended to be modified once deployed; use interface
    extension instead.
*   Reference may be nullable.

##### Declaration

```
    struct Point { float32 x, y; };
    struct Color { float32 r, g, b; };
```

##### Use

Structs are denoted by their declared name (eg. **Circle**) and nullability:

*   **<code>Circle</code></strong> : non-nullable Circle
*   <strong><code>Circle?</code></strong> : nullable Circle

    ```
    struct Circle {
        bool filled;
        Point center;    // Point will be stored in-line
        float32 radius;
        Color? color;    // Color will be stored out-of-line
        bool dashed;
    };

    ```

#### Unions

*   Tagged option type consisting of tag field and variadic contents.
*   Declaration is not intended to be modified once deployed; use interface
    extension instead.
*   Reference may be nullable.

##### Declaration

```
    union Pattern {
        Color color;
        Texture texture;
    };
    struct Color { float32 r, g, b; };
    struct Texture { string name; };
```

##### Use

Union are denoted by their declared name (eg. **Pattern**) and nullability:

*   **<code>Pattern</code></strong> : non-nullable Shape
*   <strong><code>Pattern?</code></strong> : nullable Shape

    ```
    struct Paint {
        Pattern fg;
        Pattern? bg;
    };

    ```

#### Interfaces

*   Describe methods which can be invoked by sending messages over a channel.
*   Methods are identified by their ordinal index. Ordinals must be stated
    explicitly to reduce the chance that developers might break interfaces by
    reordering methods and to help with interface extension and derivation.
    *   Method ordinals are unsigned values in the range **0x00000001** to
        **0x7fffffff**.
    *   The FIDL wire format internally represents ordinals as 32-bit values but
        reserves the range **0x80000000** to **0xffffffff** for protocol control
        messages, so these values cannot be associated with methods.
*   Each method declaration states its arguments and results.
    *   If no results are declared, then the method is one-way: no response will
        be generated by the server.
    *   If results are declared (even if empty), then the method is two-way:
        each invocation of the method generates a response from the server.
*   When a client or server of an interface is about to close its side of the
    channel, it may elect to send an **epitaph** message to its peer to indicate
    the disposition of the connection. If sent, the epitaph must be the last
    message delivered through the channel. An epitaph message includes:
    *   32-bit generic status: one of the **ZX_status_t** constants
    *   32-bit protocol-specific code: meaning is left up to the interface in
        question
    *   a string: a human-readable message explaining the disposition
*   **Interface extension: **New methods can be added to existing interfaces as
    long as they do not collide with existing methods.
*   **Interface derivation: **New interfaces can be derived from any number of
    existing interfaces as long as none of their methods use the same ordinals.
    (This is purely a FIDL language feature, does not affect the wire format.)

##### Declaration

```
    interface Calculator {
        1: Add(int32 a, int32 b) -> (int32 sum);
        2: Divide(int32 dividend, int32 divisor)
        -> (int32 quotient, int32 remainder);
        3: Clear();
    };

    interface RealCalculator : Calculator {
        1001: AddFloats(float32 a, float32 b) -> (float32 sum);
    };

    interface Science {
        2001: Hypothesize();
        2002: Investigate();
        2003: Explode();
        2004: Reproduce();
    };

    interface ScientificCalculator : RealCalculator, Science {
        3001: Sin(float32 x) -> (float32 result);
    };
```

TODO: Bikeshed away…

*   **'->'** is actually just like the new C++ function syntax, so at least it's
    somewhat consistent, but could use a keyword instead
*   would be nice if enums and interfaces specified values/ordinals the same way
*   might need a way to reserve ordinal ranges, or perhaps we could do so
    implicitly by using interface numbering instead: assign a number to each
    interface (default 0), construct actual method ordinal given (iface_number
    << 16 | method_number)
    *   interface Foo@5 { Add@2(int32 a, int32 b) -> (int32 result); };

##### Use

Interfaces are denoted by their name, directionality of the channel, and
optionality:

*   **<code>Interface</code></strong> : non-nullable FIDL interface (client
    endpoint of channel)
*   <strong><code>Interface?</code></strong> : nullable FIDL interface (client
    endpoint of channel)
*   <strong><code>request<Interface></code></strong> : non-nullable FIDL interface
    request (server endpoint of channel)
*   <strong><code>request<Interface>?</code></strong> : nullable FIDL interface request
    (server endpoint of channel)

    ```
    // A record which contains interface-bound channels.
    struct Record {
        // client endpoint of a channel bound to the Calculator interface
        Calculator c;

        // server endpoint of a channel bound to the Science interface
        request<Science> s;

        // optional client endpoint of a channel bound to the
        // RealCalculator interface
        RealCalculator? r;
    };

    ```

### Constant Declarations

Constant declarations introduce a name within their scope. The constant's type
must be either a primitive or an enum.

Constants may be scoped within: **library, struct, union, interface**.

```
    // a constant declared at library scope
    const int32 kFavoriteNumber = 42;

    // a constant declared within an interface scope
    interface VendingMachine {
    const Beverage kFavoriteBeverage = WHISKEY;
    };
```

### Constant Expressions

TODO: decide whether we want to support any kind of arithmetic, especially for
interface ordinals and enum values, maybe defer?

## Grammar

[Grammar is here](grammar.md).

## Examples

See also [FIDL 2.0: I/O Sketch](io-sketch.md).

<!-- Footnotes themselves at the bottom. -->

## Notes

[^1]: New handle types can easily be added to the language without affecting the
    wire format since all handles are transferred the same way.
