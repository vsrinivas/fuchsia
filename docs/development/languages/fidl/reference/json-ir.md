
# FIDL JSON internal representation

For all backends (except C), the FIDL compiler operates in two phases.
A first phase parses the FIDL file(s) and produces a JSON-based Intermediate
Representation (**IR**).
A second phase takes the IR as input, and produces the appropriate language-specific output.

This section documents the JSON IR.

If you are interested in the JSON IR, you can generate it by running
the FIDL compiler with the `json` output directive:

```
fidlc --json outputfile.json --files inputfile.fidl
```

## A simple example

To get started, we can see how a simple example looks.
We'll use the `echo.fidl` ["Hello World Echo Protocol"](../tutorial/README.md)
example from the tutorial:

```fidl
library fidl.examples.echo;

[Discoverable]
protocol Echo {
    EchoString(string? value) -> (string? response);
};
```

The tutorial goes through this line-by-line, but the summary is that we create a
discoverable protocol called `Echo` with a method called `EchoString`.
The `EchoString` method takes an optional string called `value` and returns
an optional string called `response`.

Regardless of the FIDL input, the FIDL compiler generates a JSON data set with
the following overall shape:

```json
{
  "version": "0.0.1",
  "name": "libraryname",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "declaration_order": [],
  "declarations": {}
}
```

The JSON members (name-value pairs) are as follows:

Name                    | Meaning
------------------------|-----------------------------------------------------------------------
version                 | A string indicating the version of the JSON IR schema
name                    | A string indicating the given `library` name
library_dependencies    | A list of dependencies on other libraries
const_declarations      | A list of consts
enum_declarations       | A list of enums
interface_declarations  | A list of protocols provided
struct_declarations     | A list of structs
table_declarations      | A list of tables
union_declarations      | A list of unions
declaration_order       | A list of the object declarations, in order of declaration
declarations            | A list of declarations and their types

Not all members have content.

So, for our simple example, here's what the JSON IR looks like (line
numbers have been added for reference; they are not part of the generated code):

```json
[01]    {
[02]      "version": "0.0.1",
[03]      "name": "fidl.examples.echo;",
[04]      "library_dependencies": [],
[05]      "const_declarations": [],
[06]      "enum_declarations": [],
[07]      "interface_declarations": [

(content discussed below)

[53]      ],
[54]      "struct_declarations": [],
[55]      "table_declarations": [],
[56]      "union_declarations": [],
[57]      "declaration_order": [
[58]        "fidl.examples.echo/Echo"
[59]      ],
[60]      "declarations": {
[61]        "fidl.examples.echo/Echo": "interface"
[62]      }
[63]    }
```

Lines `[01]` and `[63]` wrap the entire JSON object.

Line `[02]` is the version number of the JSON IR schema.

Line `[03]` is the name of the library, and is copied from the FIDL `library` directive.

Lines `[04]`, `[05]` and `[06]` are the library dependencies, constant declarations,
and enumeration declarations.
Our simple example doesn't have any, so they just have a zero-sized (empty) array
as their value ("`[]`").
Similarly, there are no structs (line `[54]`), tables (`[55]`) or unions (`[56]`).
The declaration order (`[57]`..`[59]`) isn't that interesting either,
because there's only the one declaration, and, finally, the
declarations member (`[60]`..`[62]`) just indicates the declared object (here,
`fidl.examples.echo/Echo`) and its type (it's an `interface`).

## The protocol

Where things are interesting, though, is starting with line `[07]` &mdash; it's the protocol
declaration for all protocols in the file.

Our simple example has just one protocol, called `Echo`, so there's just one array element:

```json
[07]      "interface_declarations": [
[08]        {
[09]          "name": "fidl.examples.echo/Echo",
[10]          "maybe_attributes": [
[11]            {
[12]              "name": "Discoverable",
[13]              "value": ""
[14]            }
[15]          ],
[16]          "methods": [
[17]            {
[18]              "ordinal": 1108195967,
[19]              "name": "EchoString",
[20]              "has_request": true,
[21]              "maybe_request": [
[22]                {
[23]                  "type": {
[24]                    "kind": "string",
[25]                    "nullable": true
[26]                  },
[27]                  "name": "value",
[28]                  "size": 16,
[29]                  "alignment": 8,
[30]                  "offset": 16
[31]                }
[32]              ],
[33]              "maybe_request_size": 32,
[34]              "maybe_request_alignment": 8,
[35]              "has_response": true,
[36]              "maybe_response": [
[37]                {
[38]                  "type": {
[39]                    "kind": "string",
[40]                    "nullable": true
[41]                  },
[42]                  "name": "response",
[43]                  "size": 16,
[44]                  "alignment": 8,
[45]                  "offset": 16
[46]                }
[47]              ],
[48]              "maybe_response_size": 32,
[49]              "maybe_response_alignment": 8
[50]            }
[51]          ]
[52]        }
[53]      ],
```

Each protocol declaration array element contains:

*   Line `[09]`: the name of the object (`fidl.examples.echo/Echo` &mdash; this
    gets matched up with the `declarations` member contents starting on line
    `[60]`),
*   Lines `[10]`..`[15]`: an optional list of attributes (we had marked it as
    `Discoverable` &mdash; if we did not specify any attributes then we wouldn't
    see lines `[10]` through `[15]`), and
*   Lines `[16]`..`[51]`: an optional array of methods.

The methods array lists the defined methods in declaration order (giving details
about the ordinal number, the name of the method, whether it has a request
component and a response component, and indicates the sizes and alignments of
those componenets).

The JSON output has two `bool`s, `has_request` and `has_response`,
that indicate if the protocol defines a request and a response, respectively.

Since the string parameters within the request and response are both optional,
the parameter description specifies `"nullable": true` (line `[25]` and `[40]`).

### What about the sizes?

The `size` members might be confusing at first; it's important here to note
that the size refers to the size of the *container* and not the *contents*.

Note: Before reading this section, consider referring to the
[on-wire](wire-format/README.md) format.

Lines `[36]` through `[47]`, for example, define the `response` string container.
It's 16 bytes long, and consists of two 64-bit values:

*   a size field, indicating the number of bytes in the string (we don't rely
    on NUL termination), and
*   a data field, which indicates presence or pointer, depending on context.

For the data field, two interpretations are possible.
In the "wire format" version (that is, as the data is encoded for transmission),
the data field has one of two values: zero indicates the string is null,
and `UINTPTR_MAX` indicates that the data is present.
(See the [Wire Format](wire-format/README.md) chapter for details).

However, when this field has been read into memory and is decoded for consumption,
it contains a 0 (if the string is null), otherwise it's a pointer
to where the string content is stored.

The other fields, like `alignment` and `offset`, also relate to the
[on-wire](wire-format/README.md) data marshalling.

## Of structs, tables, and unions

Expanding on the structs, tables, and unions (`struct_declarations`, `table_declarations`,
and `union_declarations`) from above, suppose we have a simple FIDL file like
the following:

```fidl
library foo;

union Union1 {
    int64 x;
    float64 y;
};

table Table1 {
    1: int64 x;
    2: int64 y;
    3: reserved;
};

struct Struct1 {
    int64 x;
    int64 y;
};
```

The JSON that's generated will contain common elements for all three types.
Generally, the form taken is:

```json
"<TYPE>_declarations": [
  {
    <HEADER>
    "members": [
      <MEMBER>
      <MEMBER>...
    ],
    <TRAILER>
  }
]
```

Where:

Element     | Meaning
------------|-------------------------------------------------------------------------------------
`<TYPE>`    | one of `struct`, `table`, or `union`
`<HEADER>`  | contains the name of the structure, table, or union, and optional characteristics
`<MEMBER>`  | contains information about an element member
`<TRAILER>` | contains more information about the structure, table, or union

### The `struct_declarations`

For the `struct_declarations`, the FIDL code above generates the following (we'll come back
and look at the `members` part shortly):

```json
[01] "struct_declarations": [
[02]   {
[03]     "name": "foo/Struct1",
[04]     "anonymous": false,
[05]     "members": [
[06]       <MEMBER>
[07]       <MEMBER>
[08]     ],
[09]     "size": 16,
[10]     "max_out_of_line": 0,
[11]     "alignment": 8,
[12]     "max_handles": 0
[13]   }
[14] ],
```

Specifically, the `<HEADER>` section (lines `[03]` and `[04]`) contains a `"name"` field (`[03]`).
This is a combination of the `library` name and the name of the structure, giving `foo/Struct1`.

Next, the `"anonymous"` field (`[04]`) is used to indicate if the structure is
anonymous or named, just like in C.
Since neither `table` nor `union` can be anonymous, this field is
present only with `struct`.

Saving the member discussion for later, the `<TRAILER>` (lines `[09]` through `[12]`)
has the following fields:

Field             | Meaning
------------------|-------------------------------------------------------------------------------
`size`            | The total number of bytes contained in the structure
`max_out_of_line` | The maximum size of out-of-line data
`alignment`       | Alignment requirements of the object
`max_handles`     | The maximum number of handles

These four fields are common to the `table_declarations` as well as the `union_declarations`.

### The `table_declarations` and `union_declarations`

The `table_declarations` and `union_declarations` have the same `<HEADER>` fields
as the `struct_declarations`, except that they don't have an `anonymous` field,
and the `table_declarations` doesn't have an `offset` field.
A `table_declarations` does, however, have some additional fields in its `<MEMBER>` part,
we'll discuss these below.

### The `<MEMBER>` part

Common to all three of the above (`struct_definition`, `table_definition`, and `union_definition`)
is the `<MEMBER>` part.

It describes each struct, table, or union member.

Let's look at the `<MEMBER>` part for the `struct_declarations` for the first member,
`int64 x`:

```json
[01] "members": [
[02]   {
[03]     "type": {
[04]       "kind": "primitive",
[05]       "subtype": "int64"
[06]     },
[07]     "name": "x",
[08]     "size": 8,
[09]     "max_out_of_line": 0,
[10]     "alignment": 8,
[11]     "offset": 0,
[12]     "max_handles": 0
[13]   },
```

The fields here are:

Field             | Meaning
------------------|-------------------------------------------------------------------------------
`type`            | A description of the type of the member
`name`            | The name of the member
`size`            | The number of bytes occupied by the member
`max_out_of_line` | The maximum size of out-of-line data
`alignment`       | Alignment requirements of the member
`offset`          | Offset of the member from the start of the structure
`max_handles`     | The maximum number of handles

The second member, `int64 y` is identical except:

*   its `"name"` is `"y"` instead of `"x"`,
*   its `offset` is `8` instead of `0`.

#### `max_out_of_line`

The `max_out_of_line` field indicates the maximum number of bytes which may be stored out-of-line.
For instance with strings, the character array itself is stored out-of-line (that is, as data that
follows the structure), with the string's size and presence indicator being stored in-line.

#### `max_handles`

The `max_handles` field indicates the maximum number of handles to associate with the object.
Since in this case it's a simple integer, the value is zero because an integer doesn't carry
any handles.

#### The `<MEMBER>` part of a `table_declarations`

A `table_declarations` has the same `<MEMBER>` fields as the `struct_declarations` described
above, minus the `offset` and `anonymous` fields, plus two additional fields:

Field      | Meaning
-----------|--------------------------------------------------------------------------------------
`ordinal`  | This is the "serial number" of this table member
`reserved` | A flag indicating if this table member is reserved for future use

Note: If the `reserved` flag indicates the table member is reserved for future
use, then there is no definition of the member given; conversely, if the flag
indicates the member is not reserved, then the member definition follows
immediately after.

In the FIDL example above, the `table_declarations` has the following
`<MEMBER>` part for the first member, `x`:

```json
[01] "members": [
[02]   {
[03]     "ordinal": 1,
[04]     "reserved": false,
[05]     "type": {
[06]       "kind": "primitive",
[07]       "subtype": "int64"
[08]     },
[09]     "name": "x",
[10]     "size": 8,
[11]     "max_out_of_line": 0,
[12]     "alignment": 8,
[13]     "max_handles": 0
[14]   },
```

Here, the `ordinal` (`[03]`) has the value `1`, and the `reserved` flag (`[04]`) indicates
that this field is not reserved (this means that the field is present).
This matches the FIDL declaration from above:

```fidl
table Table1 {
    1: int64 x;
    2: int64 y;
    3: reserved;
};
```

The `<MEMBER>` part for the second member, `y`, is almost identical:

```json
[01] "members": [
[02]   ... // member data for "x"
[03]   {
[04]     "ordinal": 2,
[05]     "reserved": false,
[06]     "type": {
[07]       "kind": "primitive",
[08]       "subtype": "int64"
[09]     },
[10]     "name": "y",
[11]     "size": 8,
[12]     "max_out_of_line": 0,
[13]     "alignment": 8,
[14]     "max_handles": 0
[15]   },
```

The difference is that the `ordinal` field (`[04]`) has the value `2` (again, corresponding to what
we specified in the FIDL code above).

Finally, the third `<MEMBER>`, representing the reserved member, is a little different:

```json
[01] "members": [
[02]   ... // member data for "x"
[03]   ... // member data for "y"
[04]   {
[05]     "ordinal": 3,
[06]     "reserved": true
[07]   }
```

As you'd expect, the `ordinal` value (`[05]`) is `3`, and the `reserved` flag (`[06]`) is
set to `true` this time, indicating that this is indeed a reserved member.
The `true` value means that the field is *not* specified (that is, there are no `"type"`,
`"name"`, `"size"`, and so on fields following).

#### The `<MEMBER>` part of a `union_declarations`

The `<MEMBER>` part of a `union_declarations` is identical to that of a `struct_declarations`,
except that:

*   there is no `max_handles` field (unions can't contain handles)
*   the `offset` field may refer to the same offset for multiple members

Recall that a `union` is an "overlay" &mdash; several different data layouts are considered to
be sharing the space.
In the FIDL code we saw above, we said that the data can be a 64-bit integer (called `x`)
or it could be a floating point value (called `y`).

This explains why both `offset` fields have the same value &mdash; `x` and `y` share the
same space.

Here are just the `offset` parts (and some context) for the `union_declarations` from
the FIDL above:

```json
[01] "union_declarations": [
[02]   {
[03]     "name": "foo/Union1",
[04]     "members": [
[05]       {
[06]         "name": "x",
[07]         "offset": 8
[08]       },
[09]       {
[10]         "name": "y",
[11]         "offset": 8
[12]       }
[13]     ],
[14]     "size": 16,
[15]   }
[16] ],
```

The natural questions you may have are, "why do the offsets (`[07]` and `[11]`) start at 8
when there's nothing else in the union?
And why is the size of the union 16 bytes (`[14]`) when there's only 8 bytes of data
present?"

Certainly, in C you'd expect that a union such as:

```c
union Union1 {
  int64_t x;
  double y;  // "float64" in FIDL
};
```

would occupy 8 bytes (the size required to store the largest element),
and that both `x` and `y` would start at offset zero.

In FIDL, unions have the special property that there's an identifier at the beginning that
is used to tell which member the union is holding.
It's as if we had this in the C version:

```c
enum Union1_types { USING_X, USING_Y };

struct Union1 {
  Union1_types which_one;
  union {
    int64_t x;
    double y;
  };
};
```

That is to say, the first field of the data structure indicates which interpretation of
the union should be used: the 64-bit integer `x` version, or the floating point `y`
version.

> In the FIDL implementation, the "tag" or "discriminant" (what we called `which_one`
> in the C version) is a 32-bit unsigned integer.
> Since the alignment of the union is 8 bytes, 4 bytes of padding are added after the
> tag, giving a total size of 16 bytes for the union.

### Aggregates

More complicated aggregations of data are, of course, possible.
You may have a struct (or table or union) that contains other structures (or tables
or unions) within it.

As a trivial example, just to show the concept, let's include the `Union1` union as
member `u` (line `[09]` below) within our previous structure:

```fidl
[01] union Union1 {
[02]     int64 x;
[03]     float64 y;
[04] };
[05]
[06] struct Struct1 {
[07]     int64 x;
[08]     int64 y;
[09]     Union1 u;
[10] };
```

This changes the JSON output for the `Struct1` `struct_declarations` as follows:

```json
[01] "struct_declarations": [
[02]   {
[03]     "name": "foo/Struct1",
[04]     "anonymous": false,
[05]     "members": [
[06]       ... // member data for "x" as before
[07]       ... // member data for "y" as before
[08]       {
[09]         "type": {
[10]           "kind": "identifier",
[11]           "identifier": "foo/Union1",
[12]           "nullable": false
[13]         },
[14]         "name": "u",
[15]         "size": 16,
[16]         "max_out_of_line": 0,
[17]         "alignment": 8,
[18]         "offset": 16,
[19]         "max_handles": 0
[20]       }
[21]     ],
[22]     "size": 32,
[23]     "max_out_of_line": 0,
[24]     "alignment": 8,
[25]     "max_handles": 0
[26]   }
[27] ],
```

The changes are as follows:

*   New `<MEMBER>` entry for the union `u` (`[08]` through `[20]`),
*   The `size` (`[22]`) of the `struct_declarations` has now doubled to `32`

We'll examine these in order.

Whereas in the previous examples we had simple `"type"` fields, e.g.:

```json
"type": {
  "kind": "primitive",
  "subtype": "int64"
},
```

(and, for the union's `float64 y`, we'd have `"subtype": "float64"`),
we now have:

```json
"type": {
  "kind": "identifier",
  "identifier": "foo/Union1",
  "nullable": false
},
```

The `"kind"` is an `identifier` &mdash; this means that it's a symbol of some
sort, one that is defined elsewhere (and indeed, we defined `foo/Union1` in
the FIDL file as a union).

The `"nullable"` flag indicates if the member is optional or mandatory, just like
we discussed above in the section on [Protocols](#The-protocol).
In this case, since `"nullable"` is `false`, the member is mandatory.

Had we specified:

```fidl
union Union1 {
    int64 x;
    float64 y;
};

struct Struct1 {
    int64 x;
    int64 y;
    Union1? u;  // "?" indicates nullable
};
```

Then we'd see the `"nullable"` flag set to `true`, indicating that the union
`u` is optional.

When the `nullable` flag is set (indicating optional), the union value is
stored out-of-line and the in-line indication consists of just a presence and
size indicator (8 bytes).
This causes the `max_out_of_line` value to change from 0 to 16 (because that's
the size of the out-of-line data), and the size of the entire `struct_declarations`
object shrinks from the 32 shown above to 24 bytes &mdash; `16 - 8 = 8` bytes less.

