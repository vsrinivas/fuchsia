# FIDL lexicon

This document defines general terms that have a specific meaning in a FIDL
context. To learn more about specific FIDL topics, refer to the [FIDL
traihead][trailhead]

## Variant, tag, and ordinal {#union-terms}

**Variant** refers to the selected member of a union. For example, the variants
of

```fidl
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/fuchsia.examples.docs/misc.test.fidl" region_tag="command" %}
```

are `create_resource` and `release_resource`. A value of `Command` will have a
**selected** variant of either `create_resource` or `release_resource`.

The **tag** is the target language variant discriminator, i.e. the specific
construct in a FIDL target language that is used to indicate the variant of a
union. For example, in the following target language (in this example,
typescript) representation of the `Command` union:

```typescript
enum CommandTag {
    Create,
    Release,
}

interface Command = {
    tag: CommandTag,
    data: CreateResource | ReleaseResource,
}
```

The tag of `Command` is `Command.tag` and has type `CommandTag`. The actual
values and type representing each variant of `Command` are up to the
implementation.

Note that some languages will not require a tag. For example, some languages use
pattern matching to branch on the variant of a union instead of having an
explicit tag value.

The **ordinal** is the on the wire variant discriminator, i.e. the value used to
indicate the variant of a union in the [FIDL wire format][wire-format]. The
ordinals are explicitly specified in the FIDL definition (in this example, 1 for
`create_resource` and 2 for `release_resource`).

## Encode {#encode}

Encoding refers to the process of serializing values from a target language into
the FIDL wire format.

For the C family of bindings (HLCPP, LLCPP), encode can have a more specific
meaning of taking bytes matching the layout of the FIDL wire format and patching
pointers and handles by replacing them with
`FIDL_ALLOC_PRESENT`/`FIDL_ALLOC_ABSENT` or
`FIDL_HANDLE_PRESENT`/`FIDL_HANDLE_ABSENT` in-place, moving handles into an
out-of-band handle table.

## Decode {#decode}

Decoding refers to the process of deserializing values from raw bytes in the
FIDL wire format into a value in a target language.

For the C family of bindings (HLCPP, LLCPP), decode can have a more specific
meaning of taking bytes matching the layout of the FIDL wire format and patching
pointers and handles by replacing `FIDL_ALLOC_PRESENT`/`FIDL_ALLOC_ABSENT` or
`FIDL_HANDLE_PRESENT`/`FIDL_HANDLE_ABSENT` with the "real" pointer/handle
values in-place, moving handles out of an out-of-band handle table.

## Validate {#validate}

Validation is the process of checking if constraints from the FIDL definition
are satisfied for a given value. Validation occurs both when encoding a value
before being sent, or when decoding a value after receiving it. Example
constraints are vector bounds, handle constraints, and the valid encoding of a
string as UTF-8.

## Result/error type {#result}

For methods with error types specified:

```fidl
DoWork() -> (Data result) error uint32
```

The **result type** refers to the entire message that would be received by a
server for this method, i.e. the union that consists of either a result of
`Data` or an error of `uint32`. The error type in this case is `uint32`, whereas
`Data` can be referred to as either the response type or the success type.

<!-- xrefs -->
[trailhead]: /docs/development/languages/fidl/README.md
[wire-format]: /docs/reference/fidl/language/wire-format
