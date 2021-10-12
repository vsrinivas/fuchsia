# Netstack Team's Rust Patterns

This document enumerates patterns that the netstack team has found to produce:

- Code that is more resilient to behavior change at a distance.
- Code that is more easily reviewed with less "working memory".

These guidelines are considered additive to the [Rust API rubric][api-rust].

The patterns suggested here provide centralized guidance and knowledge.
Contributions of all types - edits, additions, removals, etc. - are
encouraged.

Rough edges encountered during code authoring or reviewing under the proposed
guidelines are expected to make their way back into this document, so we can
codify alternative patterns when needed.

> TODO(https://fxbug.dev/86009): Encode process for changes to this page.

## Avoid unused results

This is mostly machine-enforced since <https://fxrev.dev/510442> via rustc's
[unused-results lint][unused-results]. See the
[documentation][unused-results-explanation] for an explanation of the
reasoning.

When discarding results, encode the types of ignored fields, so it becomes part
of the contract.

When discarding a result *prefer* to use a named variable prefixed with `_`
when the semantic meaning is not immediately clear from the type, and it won't
affect `drop` semantics.

The added information serves as a prompt for both author and reviewer:

- Is there an invariant that should be checked using the return?
- Should this function return a value at all?

> *Note:* Historically the team has used the form `let () = ` on assignments as
a statement that no information is being discarded. That practice is still
accepted, but it is no longer necessary with the introduction of the lint.

### Examples

#### Use the prompts

```rust
// Bad. The dropped type is unknown without knowing the signature of write.
let _ = writer.write(payload);
let _n = writer.write(payload);

// Okay. The dropped type is locally visible and enforced. Absence of invariant
// enforcement such as a check for partial writes is obvious and can be flagged
// in code review.
let _: usize = writer.write(payload);
let _n: usize = writer.write(payload);

// Good.
let n = writer.write(payload);
assert_eq!(n, payload.len());
```

#### Adopted dropping formats

```rust
// Encode the type of the ignored return value.
let _: Foo = foo::do_work(foo, bar, baz);

// When destructuring tuples, type the fields not in use.
let (foo, _) : (_, Bar) = foo::foo_and_bar();

// When destructuring named structs, no annotations are necessary, the field's
// type is encoded by the struct.
let Foo{ fi, fo: _ } =  foo::do_work(foo, bar, baz);

// Encode the most specific type possible.
let _: Option<_> = foo.maybe_get_trait_impl(); // Can't name opaque type.
let _: Option<usize> = foo.maybe_get_len(); // Can name concrete type.

// Use best judgement for unwieldy concrete types.
// If the type expands to formatted code that would span >= 5 lines of type
// annotation, use best judgement to cut off the signature.
let _: futures::future::Ready<Result<Buffer<_>>, anyhow::Error> = y();
// Prefer to specify if only a couple of levels of nesting and < 5 lines.
let _: Result<
    Result<(bool, usize), fidl_fuchsia_net_interfaces_admin::AddressRemovalError>,
    fidl::Error,
> = x();
```

#### Defeating patterns

Be mindful of defeating patterns:

```rust
// Bad, this is a drop that does not encode the type.
std::mem::drop(foo());
// Prefer instead:
let _: Foo = foo();
```

[api-rust]: /docs/concepts/api/rust.md
[unused-results]: https://doc.rust-lang.org/rustc/lints/listing/allowed-by-default.html#unused-results
[unused-results-explanation]: https://doc.rust-lang.org/rustc/lints/listing/allowed-by-default.html#explanation-31
