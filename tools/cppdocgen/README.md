# C++ document generator

This is an experimental tool to generate markdown documentation from C++. It
consumes .yaml output from clang-doc and formats markdown for serving on
fuchsia.dev.

It is not yet hooked up to the build by default. It is invoked using the
`cpp_docgen()` GN template in `cpp_docgen.gni`. A full example is in the
`e2e_test` directory.

## Comment formatting

This tool (via clang-doc) reads the comments above functions, classes, structs,
defines, enums, etc. and uses these as the documentation for those entities.
These comments are called "docstrings."

The docstrings can be formatted with either `//` or `///`. They must immediately
precede the definition with no blank lines to be counted as documentation for
that definition.

The contents of the comments is treated as markdown and, to a first
approximation, simply copied to the output. The markdown will be converted to
HTML by Google devsite, so all devsite markdown features are available,
including inline HTML.

```
// This is the docstring for the function.
//
// More documentation. Some **bold** and *italic* markdown. You can also have
// things like [normal markdown links](https://www.google.com).
void MyFunction();
```

The following sections describe the limited processing that the docgen tool
does.

### Headings

Markdown headings (lines that start with `#`) are adjusted to appear at the
correct level of hierarchy within the document.

```
// Automatically synchronizes the cardinal grammeter
//
// # Errors
//
// This section is formatted by devsite as a definition list.
//
// ZX_ERROR_NO_RESOURCES
// : This error indicates the turbo encabulator was full.
//
// ZX_ERR_CANCELED
// : One of the six hydrocoptic marzel vanes requested a cancelation.
zx_status_t SynchronizeCardinalGrammeter();
```

In this example, the `# Errors` line will be converted to `### Errors` to fall
correctly under the automatically generated level 2 heading for the function
name. So most headings in docstrings should be level 1 markdown headings.

The exception is if the first line of a docstring starts with a level 1 heading.
This heading will be promoted to be the title for the entity rather in place of
the generated title (see "Setting the title" below).

### Links

Normal markdown links are supported and interpreted by devsite:

  * `[Google](https://www.google.com/)`
  * `[FIDL wire format](/docs/reference/fidl/language/wire-format)`

The docgen tool can automatically create links to named entities *in the same
library* by enclosing the name in square brackets and providing no link
destination (the part in parenthesis) after the square brackets:

  * `See [SynchronizeCardinalGrammeter()] to synchronize.`

The doc generateor will look for a symbol (which could be a function, class,
define, etc.) called `SynchronizeCardinalGrammeter` in one of the headers being
documented. If found, it will be linked to the correct destination and formatted
as code.

  * Anything after an opening parenthesis will be ignored when doing name
    lookup so you can include them for function calls and even include
    parameters if you feel it helps readability.
  * The contents of the `[...]` can not span lines.
  * If the named item can not be found, the text will be passed to the output
    unchanged.

## Organizing the documentation

### Index overview

You can supply content that goes at the top of the generated "index.md" file. It
is recommended this just be the README.md file at the toplevel of your library.
This should give an introduction to the library and it will be followed by the
generated header file and function indices. The title of this file will also
define the title of the generated index.md file.

To specify the overview file, use the `overview` variable in the cppdocgen GN
template. This file path is relative to the BUILD.gn directory.

```
cpp_docgen("my_docs") {
  headers = [ ... ]
  overview = "README.md"
  ...
}
```

### Per-header documentation

You can supply markdown text to be included at the top of the reference for a
header file. To count as the documentation for the header (rather than a
copyright block or the documentation for something else), the comment must
satisfy all of these requirements:

  * It must be deliniated with "///" comments at the beginning of the line.
  * It must be followed with a blank line.
  * It must appear before any non-preprocessor lines (only "#" and "//" lines
    are allowed before it).

It is recommended that this block go immediately before or after the include guard.

### Setting the title

A title will be generated for each reference page based on the header and
library name. This may not always be appropriate.

If the header file comment starts with a markdown heading 1 ("#"), that line
will be used as the title of the page instead. For example:

```
// Copyright 2022 blah blah

#ifndef MY_LIBRARY_MY_HEADER_H_
#define MY_LIBRARY_MY_HEADER_H_

/// # Deprecated functions in libdoom
///
/// The header `<lib/libdoom/deprecated.h>` contains all of the functions
/// that are currently deprecated but allowed to be used.
```

For user-friendliness:

  * Include the path and name of the header file as the user will type it in
    their code (likely relative to the library include directory, not the
    fuchsia repository root). If this is not included in the title, put it near
    the top of the header documentation text.

  * Include the name of the library.

### Grouping functions and defines into one heading

Sometimes you will want some functions or defines to appear under the same
heading. This can make similar functions much easier to follow.

Two functions are implicitly grouped when they have the same name and no blank
or comment lines separating their declarations. Any comment above the first
function becomes the comment for all of them.

```
// Returns an iterator to the beginning of the container.
iterator begin();
const_iterator begin() const;
```

These same rules apply to constructors (which always have matching names).
The difference is that constructors will always go under the same "construcors"
heading, but there will be different sections within that if there are
constructor variants with separate comments.

```
class MyClass {
  // These two will ge grouped together and this will be the docstring for them.
  MyClass();
  MyClass(int a);
  // This one will go in its own section with its own documentation.
  MyClass(std::string a);
```

Functions (even with non-matching names) and #defines can also be grouped
explicitly. To do this, list the items with no blank or comment lines separating
them, provide a comment above the first item, and start that comment with a
markdown heading 1 ("# ..."). The heading will become the title for all items in
the group:

```
/// # ZX_RIGHT_... defines
///
/// These constants define the rights for objects.
#define ZX_RIGHT_NONE 0
#define ZX_RIGHT_DISINTEGRATE 1
#define ZX_RIGHT_STAPLE 2

/// # begin()/cbegin()
///
/// These functions return a (possibly const) iterator to the beginning of the
/// container.
iterator begin();
const_iterator cbegin();
```

It is good practice to include as much of the name as practical in the title.
This is what the user will be scanning for and it should match with the other
titles which use the raw function/define names. It's best to end the title with
"macros" or "defines" to match the generated titles of other sections.

In cases where there is a group of related defines that need individual
documentation, the recommended formatting is to create a group (so they will all
be under one heading with one section showing all the declarations in-order) and
then include per-flag documentation in headings under that. The cppdocgen tool
will indent the headings automatically to be within the group.

```
// Encabulator flag macros
//
// These macros define bits that can be used in the `flags` parameter of the
// [enable_encabulator()] function.
//
// # ENCABULATOR_ENABLE_TURBO
//
// Enables the turbo mode of the encabulator. Etc...
//
//
// # ENCABULATOR_ENABLE_MARZELVANES
//
// Documentation for this flag...
#define ENCABULATOR_ENABLE_TURBO 1
#define ENCABULATOR_ENABLE_MARZELVANES 2
```
