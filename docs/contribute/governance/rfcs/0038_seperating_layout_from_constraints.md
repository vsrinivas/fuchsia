{% set rfcid = "RFC-0038" %}
{% include "docs/contribute/governance/rfcs/_common/_rfc_header.md" %}
# {{ rfc.name }} - {{ rfc.title }}
<!-- *** DO NOT EDIT ABOVE THIS LINE -->
## Rejection rationale

Replaced by [RFC-0050](/docs/contribute/governance/rfcs/0050_syntax_revamp.md).

## Summary

Note: Formerly known as [FTP](../deprecated-ftp-process.md)-038.

We propose a syntax change to convey differences between layout from constraints.

## Motivation


#### Layout vs constraint

Quickly:

*   If two types have a different layout, it is not possible to soft transition from one to the other, and vice versa (not easily at least)
*   The layout describes how the bytes are laid out, vs how they are interpreted
*   The constraint of a type is the validation step done during encoding/decoding
*   Constraints can evolve, and as long as writers are more constrained than readers, things are compatible


#### Same syntax for different things

Types which have different layout, and types which have the same layout (but different constraints) look alike.

Same layout:

*   vector&lt;T>:6 vs vector&lt;T>:10
*   T? Where T is a xunion, vector, string
*   handle, handle&lt;vmo>, or handle&lt;channel>

Different layout:

*   array&lt;T>:6 vs array&lt;T>:10
*   S? where S is a struct

## Design

Align on the syntax

	`layout:constraint`

For types, i.e. anything that controls layout is before the colon, anything that controls constraint is after the colon.

Suggested changes:

```
    array<T>:N	becomes	array<T, N>
```



    `S?	becomes	box&lt;S>:nullable`	`S` is a struct \
	_commonly_	optional&lt;S>


    `T?`	_becomes_	`T:nullable`	`T` is a vector, xunion


```
    string	is an alias for	vector<uint8>:utf8
    handle<K>	becomes	handle:K
```


Notes:

* Not all constraints are meaningful for all types, for instance it’s not possible to mark a struct nullable, it’s not possible to ‘relax’ that.
* Not everything can be boxed, initially only structs (goal is to change syntax, not introduce more ways to have optionality).

## Implementation Strategy

TBD

## Ergonomics

This proposal improves ergonomics by conveying ABI implications to developers through syntax.

## Documentation and Examples

At least:

*   [Language Specification](/docs/reference/fidl/language/language.md)
*   [Grammar](/docs/reference/fidl/language/grammar.md)
*   Examples using structs

## Backwards Compatibility

This is not source level backwards compatible.

## Performance

No impact.


## Security

No impact.


## Testing

Unit testing in `fidlc`.


## Drawbacks, Alternatives, and Unknowns

TBD


## Prior Art and References

TBD

