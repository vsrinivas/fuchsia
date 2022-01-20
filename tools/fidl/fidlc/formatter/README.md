# fidl-format

The FIDL formatter strives to be highly opinionated, specifically aiming to make
the set of valid formattings for any given semantic input as small as possible.
The model here is `gofmt`, which is well known for being non-configurable,
allowing it to enforce a canonical style.

It is worth noting that a great source of complexity in formatters [in other
languages](#prior-art) has been the addition of line wrapping: formatters that
omit it, such as the aforementioned `gofmt`, are relatively simple to implement,
whereas `rustfmt` is much more complex, due to the need to produce a "correct"
line wrapping in all cases. The FIDL formatter aims for a middle ground: the
line wrapping algorithm is simple, and therefore doesn't truncate lines below
the desired column size in all cases, but does so for the most common ones,
keeping things readable without adding too much complexity.

## Design

Conceptually, the formatting algorithm operates on the raw AST, "pretty
printing" its contents per a fixed set of rules. This algorithm can be thought
of as having three phases:

1. Convert the raw AST into a comment-less text string, with each declaration
   divided into lines and indented according to its relevant [statement
   rules](#statement-rules).
1. Wrap overlong lines per the [text wrapping](#text-wrapping) algorithm.
1. Re-insert [comment blocks](#comment-formatting) at the appropriate points,
   with only their indentation adjusted as necessary.

_Note: For examples in this document certain relevant lines whose size or
subspans need to be highlighted may be surrounded by specially styled comments
that enumerate the capacity or subspanning of the line in question, such as
`|---10---|` for a line with 10 characters remaining. Such highlights come in
three forms: 1. highlights using `-` (ex: `|---10---|`) indicate that the marked
line does not overflow the remaining capacity and does not need wrapping, 2.
highlights using `+` (ex: `|+++10+++|`) indicate that the marked line overflows
the remaining capacity, and needs to be wrapped, and 3. highlights using `=`
(ex: `|===10===|`) indicate that the marked line overflows the remaining
capacity, but cannot possibly be wrapped any further._

### Text wrapping

_Note: Examples in this section use a column width of 50 for ease of
demonstration; the column width on the actual formatter is 100, to match that of
C++._

All non-comment FIDL [statement types](#statement-rules) have well-defined line
breaking conventions. For example, a type declaration whose layout has at least
one member may have the main declaration on the first line, followed by one line
for each member of the layout, followed by a line closing the layout definition
and optionally listing constraints:

```fidl
//  |-----------------------50-----------------------|
    type U = union {
        1: foo string;
    }:optional;
```

The simplicity of this model breaks down when we have elements that force a line
in this default layout to exceed the maximum column width:

```fidl
//  |+++++++++++++++++++++++50+++++++++++++++++++++++|
    type ThisUnionHasAnExtremelyLongNameForNoReason = union {
        1: foo string;
    }:optional;
```

In such cases, the offending lines may be wrapped to avoid going over the limit.
All wrapped content for a line split in such a manner are continued on the next
line(s), doubly indented:

```fidl
//  |-----------------------50-----------------------|
    type ThisUnionHasAnExtremelyLongNameForNoReason
            = union {
        1: foo string;
    }:optional;
```

The algorithm for determining where to split overlong lines operates by defining
"subspans" for each type of FIDL statement. Any line that exceeds the maximum
column width is split into its subspans, with the first subspan remaining on the
original line, and each subsequent subspan getting a doubly indented line of its
own. Subspans may recursively contain other subspans, allowing this process to
be repeated until every subspan is placed such that it does not exceed the
maximum column width, or otherwise cannot be split (ie, has no further
subspans).

Consider the following example, with all of the nested subspans shown below the
line in question:

```fidl
//  |+++++++++++++++++++++++50+++++++++++++++++++++++|
    type Foo = vector<ThisTypeNameIsAbsurdlyTooLong>:<16,optional>;
//  |---A--| |--------------------------B-------------------------|
//           |------------------C------------------||-----D-------|
//                                                  |-E-||---F----|
```

The line is too long, so it is split into its top-level subspans:

```fidl
//  |-----------------------50-----------------------|
    type Foo
//          |+++++++++++++++++++42+++++++++++++++++++|
            = vector<ThisTypeNameIsAbsurdlyTooLong>:<16,optional>;
//          |---------------C---------------------||-----D-------|
//                                                 |-E-||---F----|
```

The second subspan is still too long, so it is split into its constituent pieces
as well. Note that additional indentation only occurs once per statement - all
further subspan line wraps are indented to the same depth as the first one:

```fidl
//  |-----------------------50-----------------------|
    type Foo
//          |-------------------42-------------------|
            = vector<ThisTypeNameIsAbsurdlyTooLong>
//          |-------------------42-------------------|
            :<16,optional>;
```

All of the lines are now shorter than the maximum column length. If the name of
the `vector`s inner type had been even longer, we would have been unable to
split it further:

```fidl
//  |-----------------------50-----------------------|
    type Foo
//          |===================42===================|
            = vector<ThisTypeNameIsLiterallyLongerThanTheMaximumColumnWidth>
//          |-------------------42-------------------|
            :<16,optional>;
```

The same rules apply for nested anonymous layouts, or any other lines that start
from an indented position. The only difference for these cases is the maximum
column width for the wrapping operation:

```fidl
//  |-----------------------50-----------------------|
    type S = struct {
//      |---------------------46---------------------|
        anon struct {
//          |+++++++++++++++++++42+++++++++++++++++++|
            ohDearThisFieldNameIsTooLong AndTheTypeNameIsTooLongAsWell:optional;
//          |-------------A------------| |------------B--------------||----C---|
        };
    };
```

Becomes:

```fidl
//  |-----------------------50-----------------------|
    type S = struct {
//      |---------------------46---------------------|
        a struct {
//          |-------------------42-------------------|
            ohDearThisFieldNameIsTooLong
//                  |---------------34---------------|
                    AndTheTypeNameIsTooLongAsWell
//                  |---------------34---------------|
                    :optional;
        };
    };
```

A consequence of these rules is that very deeply nested layouts will often be
"maximally" wrapped, with even relatively small subspans pushing past the
maximum column width boundary:

```fidl
//  |-----------------------50-----------------------|
    type S = struct {
//      |---------------------46---------------------|
        a struct {
//          |-------------------42-------------------|
            b struct {
//              |-----------------38-----------------|
                c struct {
//                  |---------------34---------------|
                    d struct {
//                      |-------------30-------------|
                        e struct {
//                          |-----------26-----------|
                            somePrettyReasonableFieldName
//                                  |-------18-------|
                                    client_end
//                                  |-------18-------|
                                    :<SomeProtocolName,optional>;
                        };
                    };
                };
            };
        };
    };
```

Deeply nested layouts are generally considered difficult to read, so wrapping
them "prettily" is a non-goal at best for the formatter. In fact, "ugly"
wrapping could even be considered a desirable outcome, since it subtly
discourages such deep layout declarations.

Finally, one interesting result of this algorithm is that each entry in a list
must be its own subspan, so each entry gets its own line when its parent subspan
is wrapped. For example:

```fidl
//  |+++++++++++++++++++++++50+++++++++++++++++++++++|
    @my_attr_invocation(arg_a=123,arg_b=456,arg_c="foobar",arg_c="bazquux")
//  |-------A----------||----------------------B--------------------------|
//                      |---C----||---D----||------E------||------F-------|
//  |-----------------------50-----------------------|
    protocol P {};
```

Becomes:

```fidl
//  |-----------------------50-----------------------|
    @my_attr_invocation(
//          |-------------------42-------------------|
            arg_a=123,
//          |-------------------42-------------------|
            arg_b=456,
//          |-------------------42-------------------|
            arg_c="foobar",
//          |-------------------42-------------------|
            arg_c="bazquux")
//  |-----------------------50-----------------------|
    protocol P {};
```

While a `clang`-style
[BinPackArguments](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)-enabled
wrapping which tries to fit as many elements into each line as possible may
produce more compact, "prettier" output, it also generates a great deal more
complexity in the implementation, and is thus intentionally avoided in favor of
a "one-subelement-per-line" algorithm.

### Comment formatting

The comment formatting algorithm is less opinionated than that for [text
wrapping](#text-wrapping), simply because doing proper comment wrapping would
likely require implementing a markdown formatter, which is not desirable at this
time. The rules for comment formatting only deal with ensuring that comment
blocks are properly indented, and no attempt is made to wrap or otherwise
re-format the contents of the comment lines themselves. There are two kinds of
comment blocks worth considering: single line comments that suffix a line
containing some non-whitespace FIDL syntax (aka "inline blocks"), and groups of
one or more contiguous comment lines that contain no non-comment syntax between
them (aka "standalone blocks", a category which includes doc comments). For
example:

```fidl
/// I am a single-line standalone block.
type S = struct { // I am an inline block.
    // I am multi-line
    // standalone block.
};
```

The formatter will not touch inline comment blocks - they are always placed at
the ends of their respective lines, one space after the last token. For inline
blocks that occur between tokens that would otherwise be on the same line per
the statement rules listed below, a line break is placed immediately after the
comment, and the remainder of the line is doubly-indented. When formatting
standalone blocks, the formatter adjusts the indentation of each line to match
the indentation of the first non-comment line immediately preceding _or_
following the block, taking whichever indentation is greater. As an example, the
following unformatted FIDL:

```fidl
// Standalone block
     //#1

/// Standalone
            /// block
      ///#2
type S = struct // Inline (mid-span) block #3
 { a struct { // Inline block #4

        /// Standalone block #5 (when formatted, next line has greater indentation)
  b string; // Inline block #6

  // Standalone
        // block #7 (when formatted, previous line has greater indentation)

};}; // Inline block #8

            // Standalone block
      // #9
```

Becomes:

```fidl
// Standalone block
// #1

/// Standalone
/// block
/// #2
type S = struct // Inline (mid-span) block #3
        {
  a struct { // Inline block #4

    /// Standalone block #5 (when formatted, next line has greater indentation)
    b string; // Inline block #6

    // Standalone
    // block #7 (when formatted, previous line has greater indentation)

  };
}; // Inline block #8

// Standalone block
// #9
```

### Statement rules

_Note: Examples in this section use a column width of 30 for ease of
demonstration; the column width on the actual formatter is 100, to match that of
C++._

The formatting rules for each type of FIDL statement specify the default line
breaks and the wrapping subspan trees for the text wrapping algorithm.

#### Library declaration

`library` statements are always on their own line, and are not subspanned for
the purposes of text wrapping. They always take the following form:

```fidl
//  |=============30=============|
    library some.very.extremely.absurdly.overlong.library.name;
```

#### Using declaration

`using` statements occupy their own line by default, and have a sigle subspan if
the `as` keyword is used. They always take the following form:

```fidl
//  |+++++++++++++30+++++++++++++|
    using some.very.extremely.absurdly.overlong.library.name as foo;
```

```fidl
//  |=============30=============|
    using some.very.extremely.absurdly.overlong.library.name
            as foo;
```

#### Attribute declaration

Attribute list declarations always occupy their own line by default. There are
two top-level subspans: the attribute name (ie, everything between `@` and `(`,
inclusive), and the argument list (everything else, including the closing `)`).
The argument list can be further subspanned, with each argument taking its own
line. The last argument's subspan includes the closing `)` symbol.

```fidl
//  |+++++++++++++30+++++++++++++|
    @my_attr_invocation(arg_a="this is a long string",arg_b=12345)
//  |------------------||----------------------------------------|
//                      |----------------------------||----------|
```

```fidl
//  |=============30=============|
    @my_attr_invocation(
              arg_a="this is a long string",
              arg_b=12345)
```

#### Constant declaration

`const` statements occupy a single line by default. They are subspanned into two
portions: a declaration containing everything prior to but excluding the `=`
sign, and a value portion for everything after it. The first subspan is further
subspanned into its name and type components.

```fidl
//  |+++++++++++++30+++++++++++++|
    const MyOverlongStringName string:<123,optional> = "some val for my string";
//  |----------------------------------------------| |-------------------------|
//  |-------------------------| |------------------|
```

```fidl
//  |=============30=============|
    const MyOverlongStringName
            string:<123,optional>
            = "some val for my string";
```

#### Alias declaration

`alias` statements occupy a single line by default. They are subspanned into two
portions: a name containing everything prior to but excluding the `=` sign, and
a layout portion for everything after it.

```fidl
//  |+++++++++++++30+++++++++++++|
    alias MyOverlongStringType = string:<123,optional>;
//  |------------------------| |----------------------|
```

```fidl
//  |=============30=============|
    alias MyOverlongStringType
            = string:<123,optional>;
```

#### Type declaration

`type` statements should only keep the first line of their constituent [layout
declaration](#layout-declaration) on the declaration's line. Type declarations
have two top-level subspans: the keyword/name and the layout declaration. The
layout declaration may be further subspanned per the rules in the [layout
declarations](#layout-declaration) section below.

```fidl
//  |+++++++++++++30+++++++++++++|
    type MyVeryLongStructName = struct {};
```

```fidl
//  |=============30=============|
    type MyVeryLongStructName
            = struct {};
```

#### Layout declaration

Layout declaring statements take two forms: those that contain no members, such
as `struct{}`, occupy a single line by default. Membered layouts occupy at least
three lines: one to open the declaration, some number of lines for its contained
members and attributes (all of which are laid out according to their own rules,
depending on statement type), and one more to close it. Additionally, ordinaled
layout members will be right aligned, such that all ordinals up to 5 digits in
length end at the same character depth on their respective lines. Neither the
first nor last line may be subspanned, meaning that layout declarations always
take the form:

```fidl
//  |-------------30-------------|
    strict resource union {
        // [LAYOUT_CONTENTS]
    };
```

#### Layout member

Layout member statements should appear on one line by default, unless they
contain an anonymous layout, in which case only the first line of that layout
should be on the member's line. Members have three top-level subspans: ordinal
and name, layout reference, and value. The layout reference may be further
subspanned per the rules in the [layout declarations](#layout-declaration)
section above.

```fidl
     type E = enum {
//      |+++++++++++26+++++++++++|
        MY_LONG_ENUM_ELEMENT_NAME = 1;
//      |-----------------------| |--|
    };
    type S = struct {
//      |+++++++++++26+++++++++++|
        foo vector<vector<string:optional>>:<16,optional>;
//      |-| |-----------------------------||-------------|
        bar string:optional = "some val for my string";
//      |-| |-------------| |-------------------------|
        my_anonymous_layout struct {
//      |-----------------| |------|
            baz string;
//          |-| |-----|
        };
    };
    type T = table {
//      |+++++++++++26+++++++++++|
        1: foo vector<vector<string:optional>>:<16,optional>;
//      |----| |-----------------------------||-------------|
        002: my_anonymous_layout struct {
//      |----------------------| |------|
            baz string;
//          |-| |-----|
        };
    };
```

```fidl
     type E = enum {
//      |===========26===========|
        MY_LONG_ENUM_ELEMENT_NAME
                = 1;
    };
    type S = struct {
//      |===========26===========|
        foo
                vector<vector<string:optional>>
                :<16,optional>;
        bar
                string:optional
                = "some val for my string";
        my_anonymous_layout
                struct {
            baz string;
        };
    };
    type T = table {
//      |===========26===========|
        1: foo
                vector<vector<string:optional>>
                :<16,optional>;
        002: my_anonymous_layout
                struct {
            baz string;
        };
    };
```

#### Protocol declaration

Barring the special case of an empty `protocol` declaration, which always
occupies a single contiguous line, `protocol` declarations must occupy at least
three lines: one to open the declaration, some number of lines for its contained
methods and attributes (all of which are laid out according to their own rules,
depending on statement type), and one more to close it. Neither the first nor
last line may be subspanned, meaning that protocol declarations always take the
form:

```fidl
//  |-------------30-------------|
    protocol MyProtocol {
        // [PROTOCOL_CONTENTS]
    };
```

#### Protocol compose

`compose` statement inside of `protocol` declarations always occupy a single
line:

```fidl
    protocol MyProtocol {
//      |-----------26-----------|
        compose SomeProtocolBeingComposed;
        // [PROTOCOL_CONTENTS]
    };
```

#### Protocol method

`protocol` method declarations occupy one line by default, though any [layout
declarations](#layout-declaration) used for request or response arguments may
cause line splits. Overlong method declaration lines may be split into three
subspans: one containing the parenthesized request, one containing the `->`
followed by the parenthesized response, and the last for the error description.

```fidl
    protocol MyProtocol {
//      |+++++++++++26+++++++++++|
        Foo(FooReq) -> (FooResp) error FooError;
//      |---------| |----------| |-------------|
    };
```

```fidl
    protocol MyProtocol {
//      |-----------26-----------|
        Foo(FooReq)
                -> (FooResp)
                error FooError;
    };
```

#### Service declaration

Barring the special case of an empty `service` declaration, which always
occupies a single contiguous line, `service` declarations must occupy at least
three lines: one to open the declaration, some number of lines for its contained
protocols and attributes (all of which are laid out according to their own
rules, depending on statement type), and one more to close it. Neither the first
nor last line may be subspanned, meaning that service declarations always take
the form:

```fidl
//  |-------------30-------------|
    service MyService {
        // [SERVICE_CONTENTS]
    };
```

## Implementation

The formatter is implemented in three phases:

1. Parse into a raw abstract syntax tree, using the
   [fidlc](/docs/reference/fidl/language/fidlc.md) compiler's parser.
1. Parse into another tree of `SpanSequence`s.
1. Pretty print the `SpanSequence` tree, using an algorithm that visits each
   node, and decides whether or not it can/should be wrapped, based on the
   `SpanSequence` type and the amount of column width remaining on the current
   line being printed.

The `SpanSequenceTreeVisitor` visits every raw AST node, making decisions about
how it should be divided into `SpanSequence`s, whether leading/trailing
whitespace should be preserved for that node or its constituent tokens, and so
on. Once this is achieved, the contents of each `SpanSequence` can be printed
according to small set of rules (described below), resulting in a fully
formatted text output.

### SpanSequence

There are six types of `SpanSequence`s:

#### TokenSpanSequence

Corresponds 1:1 with FIDL tokens. In the line `library foo.bar;`, there are 5
such tokens: `library`, `foo`, `.`, `bar`, and `;`.

#### InlineCommentSpanSequence

Stores the text representing an inline comment, so that it may later be appended
to the end of its parent `SpanSequence`, regardless of any overflow
considerations.

#### AtomicSpanSequence

A line that may never be split for the purposes of line wrapping. The line
`library foo.bar;` is a single `AtomicSpanSequence` with five child
`TokenSpanSequence`s, as line breaks are [disallowed](#library-declarartion) in
`library` declarations.

#### StandaloneSpanSequence

Stores the text representing a standalone comment. This `SpanSequence` is a bit
of a special case: even though it is exclusively found as a child of
`AtomicSpanSequence`, it is required to be placed on its own line, with bounding
newlines preserved. So, even though a `library` declaration is a single
`AtomicSpanSequence`, in the following case, it will still result in multi-line
output.

```fidl
library foo

// My oddly placed comment

.bar;
```

This text an `AtomicSpanSequence` of five `TokenSpanSequence`s with a single
`StandaloneCommentSpanSequence` interspersed. This results in the above
formatting being preserved, in spite of `AtomicSpanSequence`s "no line
splitting" rule.

#### DivisibleSpanSequence

A line that may be split into its constituent children, thus creating a line
break to enable text wrapping. For example, the line `const Foo string = "abc"`
contains `DivisibleSpanSequence` with two children: another
`DivisibleSpanSequence` encapsulating `const Foo string`, and an
`AtomicSpanSequence` encapsulating `= "abc"`.

### MutlilineSpanSequence

A set of lines that MUST be split by default. For example, the following layout
contains three `MultilineSpanSequence`s:

```fidl
resource struct {
   a zx.handle;
};
```

Every `.fidl` file's `SpanSequence` tree is has a `MutlilineSpanSequence`
representing the entire file as its root node.

### Example

Consider the following FIDL:

```fidl
//  |+++++++++++++30+++++++++++++|
    library foo.bar;

    type AnOverlongNamedTable = table { // inline
        1: an_overlong_named_member struct {
            // block
            // text
            anon_struct_field uint64;
        };
    };
```

It may be split into a `SpanSequence` tree shaped like so:

![diagram](docs/formatter_example_diagram.svg)

Because each of the three `DivisibleSpanSequence`s in this tree are in fact
overflowing (that is, if printed onto their current lines, they would go over
the 30 column maximum), they are wrapped with double indentation, resulting in
the following output:

```fidl
//  |=============30=============|
    library foo.bar;

    type AnOverlongNamedTable
            = table {
        1: an_overlong_named_member
                struct {
            // block
            // text
            anon_struct_field
                    uint64;
        };
    };
```

## Prior art

* Go's formatter, `gofmt`, serves as the model of an opinionated formatter,
  though it is unfortunately [not formally
  specified](https://groups.google.com/g/golang-nuts/c/JBvEoyPJlL0). This
  formatter's
  [implementation](https://github.com/golang/go/blob/master/src/go/printer/printer.go)
  is relatively simple, due in large part to the decision to not enforce line
  wrapping.
* [Rustftmt](https://github.com/rust-lang/rustfmt/blob/master/Design.md) is
  another example of an opinionated formatter that serves as inspiration. Note
  the dramatically increased complexity due to the inclusion of a very robust
  line breaking algorithm.
* This design is an implementation of a pretty printer, famously described in
  abstract by [Phillip
  Wadler](https://homepages.inf.ed.ac.uk/wadler/papers/prettier/prettier.pdf).
* [This
  discussion](http://journal.stuffwithstuff.com/2015/09/08/the-hardest-program-ive-ever-written/#1-note)
  of some of the difficulties encountered during the implementation of the Dart
  formatter, which we hope to avoid with this simpler text wrapping design.
* The [Knuth-Plass](http://www.eprg.org/G53DOC/pdfs/knuth-plass-breaking.pdf)
  line breaking algorithm, another complex heuristic for text wrapping that this
  design hopes to avoid.
