# Bindings Specification

This document is a specification of requirements on the Fuchsia Interface
Definition Language (**FIDL**) bindings.

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD",
"SHOULD NOT", "RECOMMENDED",  "MAY", and "OPTIONAL" in this document are to be
interpreted as described in [RFC2119][RFC2119].

## Requirements

Items described in this section **MUST** be met for bindings to be considered
conformant.

_TODO_

## Recommendations

Items described in this section **SHOULD** be met for bindings to be considered
conformant.

_TODO_

## Best Practices

Items described in this section **MAY** be met for bindings to be considered
conformant.

### Bits support

It is RECOMMENDED to support the following operators over generated values:

* bitwise and, i.e `&`
* bitwise or, i.e `|`
* bitwise exclusive-or, i.e `^`
* bitwise not, i.e `~`

To provide bitwise operations which always result in valid bits values,
implementations of bitwise not should further mask the resulting value with
the mask of all values. In pseudo code:

```
~value1   means   mask & ~bits_of(value1)
```

This mask value is provided in the [JSON IR][jsonir] for convenience.

Bindings SHOULD NOT support other operators since they could result in
invalid bits value (or risk a non-obvious translation of their meaning), e.g.:

* bitwise shifts, i.e `<<` or `>>`
* bitwise unsigned shift, i.e `>>>`

## Related Documents

* [FTP-024: Mandatory Source Compatibility][ftp024]

<!-- xrefs -->
[jsonir]: /docs/development/languages/fidl/reference/json-ir.md
[ftp024]: /docs/development/languages/fidl/reference/ftp/ftp-024.md
[RFC2119]: https://tools.ietf.org/html/rfc2119
