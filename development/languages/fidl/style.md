# Elements of Fidl Style

The goal of this document is to describe best practices for composing
Fidl files.

There are several aspects of style this document will consider. Naming
and ordinal namespacing are considerably more important than the
others, as they impact code generation and interface evolution
respectively. Other aspects (library and file layout, and comments)
can be changed with no substantial downside.

Much of this document refers to things that should interact with
future tools: fidl-format, fidl-tidy, and fidl-doc.

[TOC]

## Naming
A Fidl file consists of a list of named declarations, of several
distinct types. These names are used in both the names of
declarations, and in references to other types or values. For example,
a struct's members might name other fidl types.

These names are consumed by the various language backends, and used to
generate identifiers in the generated bindings in the target
language. Each language backend will go to some amount of effort to
generate locally style-compliant names. This is necessary for
languages in which capitalization and underscores have semantic
significance. Some bindings go further than this. For example, fidl
enum constants are namedLikeThis, while the corresponding C++
identifier would be kNamedLikeThis.

### In General
- Avoid commonly reserved keywords. Not naming something `goto` is
  polite, even if you expect the interface only to be consumed from
  Dart (where `goto` is not reserved).
- Prefer avoiding underscores, for two reasons.
  - In some languages underscores impact the semantics of identifiers
    (library visibility in Dart) or are extremely idiomatic in some
    positions (member variables in C++; unused variables in Rust).
  - Reserving a character from the valid identifier space leaves the
    backends significant and easy room to avoid keyword collisions or
    otherwise munge identifiers.
- Avoid naming things `Service` and so on, especially when it is
  implied. This is in line with our Fuchsia-wide naming guidance.
- Use whole words in identifiers.
- Don't repeat the name of an enclosing scope in a member of the
  scope. For instance:
  - Yes: `Enum Color { red = 1; };`
  - No: `Enum Color { colorRed = 1; }`
  - Yes: `library IO; interface Node { ... };`
  - No: `library IO; interface IONode { ... };`
  - Rationale: In some languages (especially C) it might be necessary
    due to the lack of namespacing mechanisms in that
    language. However, any name concatenation can be handled in the
    backend for that language, without penalizing languages which
    provide stronger namespacing mechanisms. So fidl style avoids the
    duplication.

## Library names
Libraries are period-separated lists of identifiers. Portions of the
library name other than the last are also referred to as namespaces.

- All Fuchsia code should be in the `fuchsia` top-level namespace. For
  example, `fuchsia.ui`.
- Each component of the name is in `lowerCamelCase`.

### Interfaces and Methods
- Interface declarations, like all top level type declarations, should
  be named in `UpperCamelCase`.
- Methods should be named in `UpperCamelCase`.
- Parameter names should be in `lowerCamelCase`.
- Example:
  ```
  // Uses ordinals block 0x01-0xff.
  interface Logger {
      1: GetLog(LogScope logScope) -> (Log log);
      2: Publish(string data);
      3: -> OnPublishFailure(PublishResult result);
  };
  ```

### Structs, Unions, and their Members
- Struct and union declarations, like all top level type declarations,
  should be named in `UpperCamelCase`.
- Struct and union members should be named in `lowerCamelCase`.
- Examples:
  ```
  struct Point {
      float x;
      float y;
  };
  union LogResult {
      string data;
      string failureReason;
  };
  ```

### Enums, Their Members and Constants
- Enum declarations, like all top level type declarations, should be
  named in `UpperCamelCase`.
- Constants and enum members should be named in `lowerCamelCase`.
- Examples:
  ```
  const int channelPacketMaxSize = 0x10000;
  enum Colors : uint8 {
      fuchsia = 1;
      coquelicot = 2;
      amaranth = 3;
  };
  ```

### Ordinal namespacing
A Fidl interface consists of some number of methods. Each method has a
unique 32 bit identifier, called an ordinal.

Interfaces evolve in two directions. First, an interface can grow new
methods, with new ordinals. Second, a superinterface can be extended
by a subinterface. The subinterface has all of the methods of its
superinterface, plus its own.

The goal of the guidelines here is to avoid these extension mechanisms
colliding.

- The zero ordinal must not be used. This is enforced by the compiler.
- Ordinals within an interface should be allocated in a contiguous
  block. For example:
  - 0x80000001 - 0x80000007
  - 1, 2, 3
  - 1000 - 1999
- New ordinals in an interface should use the next ordinal in the
  block.  After 1, 2, and 3, use 4.
- Related interfaces should consider using nearby and distinct ordinal
  blocks. For example, interfaces A and B, in the same library, that
  refer to each other might choose to allocate in blocks 0x100-0x1ff
  and 0x200-0x2ff respectively.  Interfaces that expect to be extended
  by subinterfaces should explicitly claim ordinal blocks.

## Library layout
Like any other language, a fidl library is composed of one or more
fidl files, and has all the typical concerns about how to arrange
source.

In fidl, declarations are visible throughout a library (including
across files): all files in a library are compiled at
once. Declarations, including interfaces, very commonly reference each
other. Sometimes this self reference is circular. The advice in this
section is to keep that from being confusing.

- Prefer structuring a fidl library as a DAG of files. In particular,
  prefer to keep mutually referring things close to each other in the
  same file. One such pattern is declaring pure data types or
  constants in leaf files, and interfaces referencing those types
  together in a trunk file.
- Some aspects of fidl source layout will eventually be checked by a
  fidl format tool.
- Fidl files use 4 space indents, never tabs.
- Fidl files end in exactly one newline character.
- Fidl files contain no trailing whitespace.
- Fidl files separate top-level declarations with one newline.


## Interface Best Practices
Returning interfaces in response messages is an anti-pattern. Instead,
prefer sending a request handle in the request message. This can save
a round trip across the channel.

TODO(kulakowski) Is there anything else here?

## Error Reporting
TODO(kulakowski) Describe status values; StatusOr style unions?

## Error Handling
TODO(kulakowski): stijlist@ has some ideas about in-band error handling.

## Comments
C++ style `// comments` are supported throughout a fidl file. The same
style considerations apply to fidl interfaces as any C++ or Dart
interface.

Eventually we will build tooling to publish these comments in a few
ways (to place into the generated code, and to turn into developer
docs). At that point we may introduce a doxygen-style distinction
between // comments and /// doc comments, but we are not there yet.

- Fidl comments go above the thing being described, and are reasonably
  complete sentences with capitalizations and periods.
  - Yes:
    ```
    struct Widget {
        // Widgets must be published with monotonically increasing ids.
        uint64 id;
        // Relative to the center.
        Point location;
    };
    ```
  - No:
    ```
    struct Widget {
        uint64 id;
        // Widgets must be published with monotonically increasing ids.
        Point location; // relative to center
    };
    ```

- Types or values defined by some external point of truth should be
  commented with references to the external thing. This applies to
  standards docs (eg a wifi specification describing a configuration
  struct) or to code outside fidl (eg a structure that must match the
  ABI of some C header).

## Graveyard
These are ideas considered and rejected.
- Acronyms and initialisms need to be spelled Http rather than HTTP.
  Rationale: Language bindings want to be able to chunk identifiers
  and rework the chunks into something idiomatic. Since fidl
  identifiers do not use underscores, capitalization is the hint to
  chunk. However, backends can disambiguate runs of single character
  chunks (ie, guess that H-T-T-P-Connection is really
  HTTP-Connection). Well-supported fidl languages have style guides
  that disagree about conventions here, and so taking an opinion
  doesn't seem worth it.
  - [Dart][dart naming]
  - [C++][c++ naming]
- Prefixing enum members and constants with `k`.
  - Rationale: The fact that it is a constant seems always lexically
    obvious. The `k` would just be noise.

[dart naming]: https://www.dartlang.org/guides/language/effective-dart/style#do-capitalize-acronyms-and-abbreviations-longer-than-two-letters-like-words
[c++ naming]: https://google.github.io/styleguide/cppguide.html#Function_Names
