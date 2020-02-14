# Semantic for handles

Handles have a numeric value with no real meaning. However, we can associate a lot of information
to a handle. Some information is available using **zx\_object\_get\_info** but, fidlcat is not
currently able to use this system call with a process it monitors (Zircon will implement it in a
not too far future).

Some information is easy to infer. For example, if a process calls **zx\_channel\_create**, both
handles are channels.

However, we also need more information. For handles which are defined at the program startup, we
have some extra information. For example:

- fd:1

- dir:/svc

- directory-request:/

That means that, for a handle, we can have three attributes:

- a type (fd, dir, proc-self, vdso-vmo, ...).

- a file descriptor (a numerical value mostly used by the fd type).

- a path (a string value used, for example, by the dir type).

For handles created during the process life, we can infer this information by analysing the FIDL
method calls.

For example, with these two syscalls:

    echo_client_cpp 2378655:2378657 zx_channel_create(options:uint32: 0)
      -> ZX_OK (out0:handle: 712aee63, out1:handle: 3bdaedc3)

    echo_client_cpp 2378655:2378657 zx_channel_write(
        handle:handle: 537aead7(dir:/svc), options:uint32: 0)
      sent request fuchsia.io/Directory.Open = {
        flags: uint32 = 3
        mode: uint32 = 493
        path: string = "fuchsia.sys.Launcher"
        object: handle = 712aee63
      }
      -> ZX_OK

We know that handle **537aead7** is directory **"/svc"** (we get this information for the program
startup).

Doing an open of **"fuchsia.sys.Launcher"** on this handle will open
**"/svc/fuchsia.sys.Launcher"**.

That means that the handle **712aee63** in a remote process will refer to
**"/svc/fuchsia.sys.Launcher"**.

Because **712aee63** and **3bdaedc3** are linked (they have been created by the same
**zx\_channel\_create**), we can now know that, everytime we use **3bdaedc3**, it refers to
**dir:/svc/fuchsia.sys.Launcher**

Each time fidlcat has to print handle **3bdaedc3**, it can add the infered information:

**3bdaedc3(dir:/svc/fuchsia.sys.Launcher)**

This is way more readable.

## How to infer handle semantic

To be able to infer this kind of information, we need to understand the semantic of FIDL messages
which use handles.

Because this is currently specific to fidlcat, we decided not to put this information in the FIDL
files. Instead, we have a file embeded within fidlcat which describes the semantic for the sdk
messages.

For example, for the Directory Open method, we can define:

    library fuchsia.io {
      Directory::Open {
        request.object = handle / request.path;
      }
    }

This says that, everytime we have an Open method, the semantic for the field **object** (a handle)
which is found within the request, is equal to the semantic of the handle used by the
**zx\_channel\_write syscall** concatenated with the value of the field **path**.

With this simple rule, we are now able to infer the information about any handle used within an
Open.

## Description of the grammar for the semantic files.

File ::= LibraryDefinition\*

LibraryDefinition ::= '__library__' LibraryName '__{__' MethodDefinition\* '__}__'

LibraryName ::= IdentifierWithDots

MethodDefinition ::= ProtocolName '__::__' MethodName '__{__' Assignment\* '__}__'

ProtocolName ::= Identifier

MethodName ::= Identifier

Assignment ::= Expression '__=__' Expression '__;__'

Expression ::= MultiplicativeExpression

MultiplicativeExpression ::= AccessExpression [ '__/__' AccessExpression ]

AccessExpression ::= TerminalExpression ( '__.__' FieldName )\*

TerminalExpression ::= '__request__' | '__handle__'

FieldName ::= Identifier

Identifier: a sequence of letters, numbers and underscores which starts with a letter or an
underscore.

IdentifierWithDots: a sequence of letters, numbers, dots and underscores which starts with a letter
or an underscore.

Whitespaces (spaces, new lines, tabulations) are ignored (but they separate lexical items).

## Semantic of expressions

We have two terminal expressions:

- **handle**: the handle used to call **zx\_channel\_write** (or another system call which read/write FIDL
messages).

- **request**: the FIDL method given to the **zx\_channel\_write**.

The dot operator access a field within an object. It can be used on the terminal expression
**request**. It can be also used on any expression with returns an object.

The slash operator concatenates a handle with a string. The result is a handle with the same type,
with the same file descriptor value and with a path which is the contatanation of the handle path
and the left string. The left of the operator must be a handle. The right of the operator must be
a string.

It optimizes the result to have a canonical path.

For example, with **"dir:/svc/."**, the canonical result is **"dir:/svc"**.

Assignments (with the "**=**" operator) assign the handle semantic on the right side to the handle
on the left side. We don't modify the handle, we just modify its semantic.
