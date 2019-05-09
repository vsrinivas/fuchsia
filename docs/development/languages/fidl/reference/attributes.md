# FIDL Attributes

The following FIDL attributes are supported:

* [`[Deprecateed]`](#deprecated)
* [`[Discoverable]`](#discoverable)
* [`[Doc]`](#doc)
* [`[FragileBase]`](#fragilebase)
* [`[Internal]`](#internal)
* [`[Layout]`](#layout)
* [`[MaxBytes]`](#maxbytes)
* [`[MaxHandles]`](#maxhandles)
* [`[Selector]`](#selector)
* [`[Transitional]`](#transitional)
* [`[Transport]`](#transport)

## Scope

An attribute preceeds a FIDL element, for example:

```fidl
[Layout = "Simple"]
protocol MyProtocol {...
```

It's used to either modify the characteristics of the element, or provide
documentation.

> Note that the attribute applies *only* to the *next* element, not all
> subsequent ones.
> Elements after the current one revert to having no attributes.

## Syntax

Attributes may include multiple values, and multiple attributes may be
specified in the same element, for example:

```fidl
[Layout = "Simple", Transport = "SocketControl, OvernetEmbedded"]
```

Illustrates both aspects:
* there are two attributes, `Layout` and `Transport`, and
* the `Transport` attribute has two values, `SocketControl` and
  `OvernetEmbedded`.

## `[Deprecated]` {#deprecated}

**USAGE**: `[Deprecated]`

**MEANING**:
See [FTP-013].

> Note: not yet implemented.

## `[Discoverable]` {#discoverable}

**USAGE**: `[Discoverable]`

**MEANING**:
Causes the service's name to be made available for lookup.
A service with a `[Discoverable]` attribute can be found at run-time.
That is to say, you can "request" this service, and zircon will locate it
and provide access to it.

## `[Doc]` {#doc}

**USAGE**: `[Doc = "`_string_`"]`

**MEANING**:
In FIDL, comments can start with two ("`//`") or three slashes ("`///`"),
or they can be embodied within a `[Doc]` attribute.
The two-slash variant does not propagate the comments to the generated
target, whereas both the three-slash and `[Doc]` variants do.

That is:

```fidl
/// Foo
struct MyFooStruct { ...
```

and

```fidl
[Doc = "Foo"]
struct MyFooStruct { ...
```

have the same effect &mdash; one ("`///`") is syntactic sugar for the other.

Both have the same effect, namely that the text of the comment is
emitted into the generated code, in a manner compatible with the syntax of
the target language.

> Technically, to be identical, the `[Doc]` version would be
> `[Doc = " Foo\n"]`; note the space before the "Foo" and the line-feed "`\n`".

## `[FragileBase]` {#fragilebase}

**USAGE**: `[FragileBase]`

**MEANING**:
Denotes that the interface can be composed, otherwise it cannot.
See also [Protocol Composition][composition].

## `[Internal]` {#internal}

**USAGE**: `[Internal]`

**MEANING**:
This marks internal libraries, such as library `zx`.
It should be used only by Fuchsia developers.

## `[Layout]` {#layout}

**USAGE**: `[Layout = "`_layout_`"]`

**MEANING**:
This attribute currently has one valid value, `Simple`, and is meaningful
only on protocols.

It's used to indicate that all arguments and returns must contain objects
that are of a fixed size.
The arguments and returns themselves, however, can be dynamically sized
strings or vectors of primitives.

To clarify with an example, the following is valid:

```fidl
[Layout = "Simple"]
protocol MyProtocol {
    DynamicCountOfFixedArguments(vector<uint8>:1024 inputs);
};
```

Here, the argument is a dynamically sized `vector` of unsigned 8-bit
integers called `inputs`, with a maximum bound of 1024 elements.

The benefit of `[Layout = "Simple"]` is that the data can be directly
mapped without having to be copied and assembled.

## `[MaxBytes]` {#maxbytes}

**USAGE**: `[MaxBytes = "`_number_`"]`

**MEANING**:
This attribute is used to limit the number of bytes that can be transferred
in a message.
The compiler will issue an error if the number of bytes exceeds this limit.

## `[MaxHandles]` {#maxhandles}

**USAGE**: `[MaxHandles = "`_number_`"]`

**MEANING**:
This attribute is used to limit the number of handles that can be
transferred in a message.
The compiler will issue an error if the number of handles exceeds this limit.

## `[Selector]` {#selector}

**USAGE**: `[Select = "`_selector_`"]`

**MEANING**:
Allows you to change the hashing basis for the method ordinal, see
[FTP-020].

It can be used to rename a method without breaking ABI compatibility.
For example, if we wish to rename the `Investigate` method to `Experiment`
in the `Science` interface, we can write:

```fidl
interface Science {
    [Selector="Investigate"] Experiment();
};
```

It can also be used for `xunion` variants to keep ABI compatibility in the
same way.

## `[Transitional]` {#transitional}

**USAGE**: `[Transitional = "`_description_`"]`

**MEANING**:
Instructs bindings to generate code that will successfully build, regardless of
whether the method is implemented or not.
[FTP-021] contains more details.

## `[Transport]` {#transport}

**USAGE**: `[Transport = "`_tranport_list_`"]`

**MEANING**:
Allows you to select a transport.
Provide a comma-separated list of values, selected from:

* `Channel` &mdash; use a [Zircon channel][channel].
* `SocketControl` &mdash; use [**zx_socket_write()**][socketwrite] (note:
  no handles allowed for such protocols).
* `OvernetEmbedded` &mdash; uses a transport that makes Overnet available on
  a variety of operating systems by embedding the Overnet runtime as a C++
  library and providing a C++ API for remotable handles.
* `OvernetInternal` &mdash; transport that is used by Overnet internally to
  communicate between peers.
  Each end of the transport implements both client and server parts of the
  protocol, and no handles can be transferred.

The default is `Channel` if none specified.
If you do specify a value or values, then only those values are used (e.g.,
specifying `[Transport="SocketControl"]` disables `Channel` and uses only
`SocketControl`).

<!-- xrefs -->
[channel]: /zircon/docs/objects/channel.md
[FTP-013]: ftp/ftp-013.md
[FTP-020]: ftp/ftp-020.md
[FTP-021]: ftp/ftp-021.md
[FTP-024]: ftp/ftp-024.md
[composition]: /docs/development/languages/fidl/reference/language.md#Protocol-Composition
[socketwrite]: /zircon/docs/syscalls/socket_write.md
