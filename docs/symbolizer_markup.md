# Symbolizer markup format #

This document defines a text format for log messages that can be
processed by a _symbolizing filter_.  The basic idea is that logging
code emits text that contains raw address values and so forth, without
the logging code doing any real work to convert those values to
human-readable form.  Instead, logging text uses the markup format
defined here to identify pieces of information that should be converted
to human-readable form after the fact.  As with other markup formats,
the expectation is that most of the text will be displayed as is, while
the markup elements will be replaced with expanded text, or converted
into active UI elements, that present more details in symbolic form.

This means there is no need for symbol tables, DWARF debugging sections,
or similar information to be directly accessible at runtime.  There is
also no need at runtime for any logic intended to compute human-readable
presentation of information, such as C++ symbol demangling.  Instead,
logging must include markup elements that give the contextual
information necessary to make sense of the raw data, such as memory
layout details.

This format identifies markup elements with a syntax that is both simple
and distinctive.  It's simple enough to be matched and parsed with
straightforward code.  It's distinctive enough that character sequences
that look like the start or end of a markup element should rarely if
ever appear incidentally in logging text.  It's specifically intended
not to require sanitizing plain text, such as the HTML/XML requirement
to replace `<` with `&lt;` and the like.

## Scope and assumptions ##

This specification defines a format standard for Magenta and Fuchsia.
But there is nothing specific to Magenta or Fuchsia about the markup
format.  A symbolizing filter implementation will be independent both of
the _target_ operating system and machine architecture where the logs
are generated and of the _host_ operating system and machine
architecture where the filter runs.

This format assumes that the symbolizing filter processes intact whole
lines.  If long lines might be split during some stage of a logging
pipeline, they must be reassembled to restore the original line breaks
before feeding lines into the symbolizing filter.  Most markup elements
must appear entirely on a single line (often with other text before
and/or after the markup element).  There are some markup elements that
are specified to span lines, with line breaks in the middle of the
element.  Even in those cases, the filter is not expected to handle line
breaks in arbitrary places inside a markup element, but only inside
certain fields.

This format assumes that the symbolizing filter processes a coherent
stream of log lines from a single process address space context.  If a
logging stream interleaves log lines from more than one process, these
must be collated into separate per-process log streams and each stream
processed by a separate instance of the symbolizing filter.  Because the
kernel and user processes use disjoint address regions in most operating
systems (including Magenta), a single user process address space plus
the kernel address space can be treated as a single address space for
symbolization purposes if desired.

## Dependence on Build IDs ##

The symbolizer markup scheme relies on contextual information about
runtime memory address layout to make it possible to convert markup
elements into useful symbolic form.  This relies on having an
unmistakable identification of which binary was loaded at each address.

An ELF Build ID is the payload of an ELF note with name `"GNU"` and type
`NT_GNU_BUILD_ID`, a unique byte sequence that identifies a particular
binary (executable, shared library, loadable module, or driver module).
The linker generates this automatically based on a hash that includes
the complete symbol table and debugging information, even if this is
later stripped from the binary.

This specification uses the ELF Build ID as the sole means of
identifying binaries.  Each binary relevant to the log must have been
linked with a unique Build ID.  The symbolizing filter must have some
means of mapping a Build ID back to the original ELF binary (either the
whole unstripped binary, or a stripped binary paired with a separate
debug file).

## Colorization ##

The markup format supports a restricted subset of ANSI X3.64 SGR (Select
Graphic Rendition) control sequences.  These are unlike other markup
elements:
 * They specify presentation details (**bold** or colors) rather than
   semantic information.  The assocation of semantic meaning with color
   (e.g. red for errors) is chosen by the code doing the logging, rather
   than by the UI presentation of the symbolizing filter.  This is a
   concession to existing code (e.g. LLVM sanitizer runtimes) that use
   specific colors and would require substantial changes to generate
   semantic markup instead.
 * A single control sequence changes "the state", rather than being an
   hierarchical structure that surrounds affected text.

The filter processes ANSI SGR control sequences only within a single
line.  If a control sequence to enter a **bold** or color state is
encountered, it's expected that the control sequence to reset to default
state will be encountered before the end of that line.  If a "dangling"
state is left at the end of a line, the filter may reset to default
state for the next line.

An SGR control sequence is not interpreted inside any other markup element.
However, other markup elements may appear between SGR control sequences and
the color/**bold** state is expected to apply to the symbolic output that
replaces the markup element in the filter's output.

The accepted SGR control sequences all have the form `"\033[%um"`
(expressed here using C string syntax), where `%u` is one of these:

| Code | Effect | Notes |
|:----:|:------:|-------|
| `0`  | Reset to default formatting. | |
| `1`  | Use **bold text**  | Combines with color states, doesn't reset them.|
| `30` | Black foreground   | |
| `31` | Red foreground     | |
| `32` | Green foreground   | |
| `33` | Yellow foreground  | |
| `34` | Blue foreground    | |
| `35` | Magenta foreground | |
| `36` | Cyan foreground    | |
| `37` | White foreground   | |

## Common markup element syntax ##

All the markup elements share a common syntactic structure to facilitate
simple matching and parsing code.  Each element has the form:

```
{{{tag:fields}}}
```

`tag` identifies one of the element types described below, and is always
a short alphabetic string that must be in lower case.  The rest of the
element consists of one or more fields.  Fields are separated by `:` and
cannot contain any `:` or `}` characters.  How many fields must be or
may be present and what they contain is specified for each element type.

No markup elements or ANSI SGR control sequences are interpreted inside the
contents of a field.

In the descriptions of each element type, `printf`-style placeholders
indicate field contents:

* `%s`

  A string of printable characters, not including `:` or `}`.

* `%p`

  An address value represented by `0x` followed by an even number of
  hexadecimal digits (using either lower-case or upper-case for
  `A`..`F`).  If the digits are all `0` then the `0x` prefix may be
  omitted.  No more than 16 hexadecimal digits are expected to appear in
  a single value (64 bits).

* `%u`

  A nonnegative decimal integer.

* `%x`

  A sequence of an even number of hexadecimal digits (using either
  lower-case or upper-case for `A`..`F`), with no `0x` prefix.
  This represents an arbitrary sequence of bytes, such as an ELF Build ID.

## Presentation elements ##

These are elements that convey a specific program entity to be displayed
in human-readable symbolic form.

* `{{{symbol:%s}}}`

  Here `%s` is the linkage name for a symbol or type.  It may require
  demangling according to language ABI rules.  Even for unmangled names,
  it's recommended that this markup element be used to identify a symbol
  name so that it can be presented distinctively.

  Examples:
  ```
  {{{symbol:_ZN7Mangled4NameEv}}}
  {{{symbol:foobar}}}
  ```

* `{{{pc:%p}}}`

  Here `%p` is the memory address of a code location.
  It might be presented as a function name and source location.

  Examples:
  ```
  {{{pc:0x12345678}}}
  {{{pc:0xffffffff9abcdef0}}}
  ```

* `{{{data:%p}}}`

  Here `%p` is the memory address of a data location.
  It might be presented as the name of a global variable at that location.

  Examples:
  ```
  {{{data:0x12345678}}}
  {{{data:0xffffffff9abcdef0}}}
  ```

* `{{{bt:%u:%p}}}`

  This represents one frame in a backtrace.  It usually appears on a
  line by itself (surrounded only by whitespace), in a sequence of such
  lines with ascending frame numbers.  So the human-readable output
  might be formatted assuming that, such that it looks good for a
  sequence of `bt` elements each alone on its line with uniform
  indentation of each line.  But it can appear anywhere, so the filter
  should not remove any non-whitespace text surrounding the element.

  Here `%u` is the frame number, which starts at zero for the location
  of the fault being identified, increments to one for the caller of
  frame zero's call frame, to two for the caller of frame one, etc.
  `%p` is the memory address of a code location.

  In frames after frame zero, this code location identifies a call site.
  Some emitters may subtract one byte or one instruction length from the
  actual return address for the call site, with the intent that the
  address logged can be translated directly to a source location for the
  call site and not for the apparent return site thereafter (which can
  be confusing).  It's recommended that emitters _not_ do this, so that
  each frame's code location is the exact return address given to its
  callee and e.g. could be highlighted in instruction-level disassembly.
  The symbolizing filter can do the adjustment to the address it
  translates into a source location.  Assuming that a call instruction
  is longer than one byte on all supported machines, applying the
  "subtract one byte" adjustment a second time still results in an
  address somewhere in the call instruction, so a little sloppiness here
  does no harm.

  Examples:
  ```
  {{{bt:0:0x12345678}}}
  {{{bt:1:0xffffffff9abcdef0}}}
  ```

* `{{{hexdict:...}}}`

  This element can span multiple lines.  Here `...` is a sequence of
  key-value pairs where a single `:` separates each key from its value,
  and arbitrary whitespace separates the pairs.  The value (right-hand
  side) of each pair either is one or more `0` digits, or is `0x`
  followed by hexadecimal digits.  Each value might be a memory address
  or might be some other integer (including an integer that looks like a
  likely memory address but actually has an unrelated purpose).  When
  the contextual information about the memory layout suggests that a
  given value could be a code location or a global variable data
  address, it might be presented as a source location or variable name
  or with active UI that makes such interpretation optionally visible.

  The intended use is for things like register dumps, where the emitter
  doesn't know which values might have a symbolic interpretation but a
  presentation that makes plausible symbolic interpretations available
  might be very useful to someone reading the log.  At the same time,
  a flat text presentation should usually avoid interfering too much
  with the original contents and formatting of the dump.  For example,
  it might use footnotes with source locations for values that appear
  to be code locations.  An active UI presentation might show the dump
  text as is, but highlight values with symbolic information available
  and pop up a presentation of symbolic details when a value is selected.

  Example:
  ```
  {{{hexdict:
    CS:                   0 RIP:     0x6ee17076fb80 EFL:            0x10246 CR2:                  0
    RAX:      0xc53d0acbcf0 RBX:     0x1e659ea7e0d0 RCX:                  0 RDX:     0x6ee1708300cc
    RSI:                  0 RDI:     0x6ee170830040 RBP:     0x3b13734898e0 RSP:     0x3b13734898d8
     R8:     0x3b1373489860  R9:         0x2776ff4f R10:     0x2749d3e9a940 R11:              0x246
    R12:     0x1e659ea7e0f0 R13: 0xd7231230fd6ff2e7 R14:     0x1e659ea7e108 R15:      0xc53d0acbcf0
  }}}
  ```

* `{{{dumpfile:%s:%s}}}`

  Here the first `%s` is an identifier for a type of dump and the
  second `%s` is an identifier for a particular dump that's just been
  published.  The types of dumps, the exact meaning of "published",
  and the nature of the identifier are outside the scope of the markup
  format per se.  In general it might correspond to writing a file by
  that name or something similar.

  This is technically a presentation element, but it may also serve to
  trigger additional post-processing work beyond symbolizing the markup.
  It indicates that a dump file of some sort has been published.  Some
  logic attached to the symbolizing filter may understand certain types
  of dump file and trigger additional post-processing of the dump file
  upon encountering this element (e.g. generating visualizations,
  symbolization).  The expectation is that the information collected
  from contextual elements (described below) in the logging stream may
  be necessary to decode the content of the dump.  So if the symbolizing
  filter triggers other processing, it may need to feed some distilled
  form of the contextual information to those processes.

  On Magenta and Fuchsia in particular, "publish" means to call the
  `__sanitizer_publish_data` function from `<magenta/sanitizer.h>`
  with the "type" identifier as the "sink name" string.  The "dump
  identifier" is the name attached to the Magenta VMO whose handle
  was passed in the call to `__sanitizer_publish_data`.
  **TODO(mcgrathr): Link to docs about `__sanitizer_publish_data` and
  getting data dumps off the device.**

  An example of a type identifier is `sancov`, for dumps from LLVM
  [SanitizerCoverage](https://clang.llvm.org/docs/SanitizerCoverage.html).

  Example:
  ```
  {{{dumpfile:sancov:sancov.8675}}}
  ```

## Contextual elements ##

These are elements that supply information necessary to convert
presentation elements to symbolic form.  Unlike presentation elements,
they are not directly related to the surrounding text.  Contextual
elements should appear alone on lines with no other non-whitespace
text, so that the symbolizing filter might elide the whole line from
its output without hiding any other log text.

The contextual elements themselves do not necessarily need to be
presented in human-readable output.  However, the information they
impart may be essential to understanding the logging text even after
symbolization.  So it's recommended that this information be preserved
in some form when the original raw log with markup may no longer be
readily accessible for whatever reason.

Contextual elements should appear in the logging stream before they are
needed.  That is, if some piece of context may affect how the
symbolizing filter would interpret or present a later presentation
element, the necessary contextual elements should have appeared
somewhere earlier in the logging stream.  It should always be possible
for the symbolizing filter to be implemented as a single pass over the
raw logging stream, accumulating context and massaging text as it goes.

* `{{{module:%x:%s:...}}}`

  Here `%x` encodes an ELF Build ID (or equivalent unique identifier).
  The `%s` is a human-readable identifier for the module, such as an ELF
  `DT_SONAME` string or a file name; but it might be empty.  It's only
  for casual information.  The Build ID string is the sole way to
  identify the binary from which this module was loaded.  A "module" is
  a single linked binary, such as a loaded ELF file.  Usually each
  module occupies a contiguous range of memory (always does on Magenta).

  The `...` is a sequence of fields (separated by `:`) that each
  describe a range of memory, called a _segment_.  The field for each
  segment has the form `%p,%p,%s` which can be read as: address, size,
  flags.  The first `%p` is the starting address of the segment and
  the second `%p` is its size in bytes.  The starting address will
  usually have been rounded down to the active page size, and the size
  rounded up.  The `%s` is one or more of the letters 'r', 'w', and
  'x' (in that order and in either upper or lower case) to indicate
  this segment of memory is readable, writable, and/or executable.
  The symbolizing filter can use this information to guess whether an
  address is a likely code address or a likely data address in the
  given module.

  There can be any number of segments (within reason), but there must
  be at least one.  They must be in ascending order of address and
  must not overlap.  For an ELF module, the segments should correspond
  exactly to the `PT_LOAD` segments in the ELF file's program headers
  (except for address and size rounding).

  Example:
  ```
  {{{module:83238ab56ba10497:libc.so:0x7acba69d5000,0x5a000,r:0x7acba6a2f000,0x7e000,rx:0x7acba6aad000,0x8000,rw}}}
  ```
