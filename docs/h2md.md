
# h2md - Header To Markdown

h2md is a simple tool for generating markdown api docs from headers.

It avoids any dependencies and has a very simple line-oriented parser.
Whitespace at the start and end of lines is ignored.

Lines starting with `//@` are either a directive to h2md or the start of
a chunk of markdown.

Markdown chunks are continued on every following line starting
with `//`.  They are ended by a blank line, or a line of source code.

A line of source code after a markdown chunk is expected to be a function
or method declaration, which will be terminated (on the same line or
a later line) by a `{` or `;`. It will be presented as a code block.

Lines starting with `//{` begin a code block, and all following lines
will be code until a line starting with `//}` is observed.

To start a new document, use a doc directive, like
`//@doc(docs/my-markdown.md)`

From the start of a doc directive until the next doc directive, any
generated markdown will be sent to the file specified in the directive.

