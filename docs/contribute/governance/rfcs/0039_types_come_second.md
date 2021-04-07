{% set rfcid = "RFC-0039" %}
{% include "docs/contribute/governance/rfcs/_common/_rfc_header.md" %}
# {{ rfc.name }} - {{ rfc.title }}
<!-- *** DO NOT EDIT ABOVE THIS LINE -->
## Rejection rationale

Replaced by [RFC-0050](/docs/contribute/governance/rfcs/0050_syntax_revamp.md).

## Summary

_You're go-ing to love it_

Note: Formerly known as [FTP](../deprecated-ftp-process.md)-039.

We propose to:

*   Allow **inline declarations of structs, xunions, and tables**;
*   Flip the order in which field names (or argument/return names) appear with respect to types, specifically to have the **type appear after names**.

(We stop short of introducing anonymous declarations, since we would likely want improved bindings support to ensure the ergonomics are good.)

## Motivation

Quickly:

*   We’re starting to see more patterns where combination of various declarations to describe ‘one conceptual message’ is routine. For instance:
    *   Container struct, whose last field is a table (initially empty) to leave the door open to extensions.
    *   Container xunion, where variants are tables to have flexibility.
    *   Container table, where fields are grouped in structs, and ordinals loosely match to ‘version numbers’.
*   Additionally, support for empty struct, xunions, and tables offers the low-level pieces to build Algebraic Data Type support from (from a layout standpoint, not bindings).
*   All of these use cases are pushing to allow inline declarations.
*   With inline declarations, it’s easier to read the field name first, then have a type description, which could straddle multiple lines. See examples below.

Holistic view of assumptions? And goals?

*   Important to have one coherent view of how FIDL files should look like (vs multiple FTPs which are ‘disjoint’)

## Design

Some examples:

* Simple struct or table:

  ```
  struct Name { \
      field int32;`**
  };
  table Name {
      1: field int32;
  };
  ```

* Protocols:

  ```
  protocol Name {
    Method(arg int32) -> (ret int32);
  };
  ```

* Struct with extension:

  ```
  struct Name { \
      field1 T1; \
      field2 T2; \
      … \
      ext table NameExt {}; \
  };
  ```

* Flexible union variants:

  ```
  xunion Name {
    variant1 table NameVariant1 {
      ...
    };
    variant2 table NameVariant2 {
      ...
    };
    ...
  };
  ```

 *Grouped fields by version:

  ```
  table Name {
    1: v1 struct NameGroupV1 {
      ...
    };
    2: v2 struct NameGroupV2 {
      ...
    };
    ...
  };
  ```

Notes:

*   Scoping wise, while we would consider all declaration names to be top-level (and hence enforce uniqueness on a per-library basis), we would not allow inline declarations from being referenced, i.e. only single use.

## Implementation strategy

TBD

## Ergonomics

This proposal improves ergonomics by conveying ABI implications to developers through syntax.

## Documentation and examples

At least:

*   [Language Specification](/docs/reference/fidl/language/language.md)
*   [Grammar](/docs/reference/fidl/language/grammar.md)
*   Examples using structs


## Backwards compatibility

This is not source level backwards compatible.


## Performance

No impact.


## Security

No impact.


## Testing

Unit testing in `fidlc`.


## Drawbacks, alternatives, and unknowns

TBD


## Prior art and references

TBD
