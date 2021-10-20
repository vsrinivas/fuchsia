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

## Process for changes to this page

All are invited and welcome to propose changes to the patterns adopted by the
[Netstack team]. Proposed patterns will be accepted or rejected by the team
after a process of consensus building through discussion, falling back to a
go/no-go simple majority vote.

Follow the steps below to propose a change.

1. Author and publish a CL changing this page.
1. *\[optional\]* Socialize with a small group and iterate.
1. Request review from the entire team through e-mail and chat. Non-Googlers can
   reach out through [discussion groups].
1. Iterate on the CL based on review comments and offline sessions.
   Remember to publish outcomes of offline sessions back to the CL.
1. Team members may express support `+1`, opposition `-1`, or indifference.
   Indifference is voiced through a single comment thread on Gerrit where
   members state indifference. That thread is to be kept unresolved until the
   CL merges. Team members may change their vote at any time.
1. Proposals will be in review for at most 2 weeks. A last call announcement is
   sent at the end of the first week. The timeline may be short-circuited if the
   *entire* team has cast their vote.
1. When consensus can't be achieved, the team will tally the votes and make a
   decision to go ahead or not using a simple majority.

Things to keep in mind:

* Authors and leads will shepherd changes through this process.
* Be respectful of others; address points on their merits alone.
* Avoid long comments; express disagreement with supporting arguments
  succinctly.
* Back-and-forth on the CL is discouraged. Fallback to breakout video or
  in-person sessions (public, preferably) and encode the consensus back into
  the comment thread.
* Controversial points can be dropped and addressed in a follow-up proposal if
  necessary.
* Indifference votes are used to measure the perceived benefit of encoding some
  patterns. A strong indifference signal is interpreted as a hint that the point
  being discussed does not merit encoding as a pattern.

[api-rust]: /docs/concepts/api/rust.md
[unused-results]: https://doc.rust-lang.org/rustc/lints/listing/allowed-by-default.html#unused-results
[unused-results-explanation]: https://doc.rust-lang.org/rustc/lints/listing/allowed-by-default.html#explanation-31
[Netstack team]: /src/connectivity/network/OWNERS
[discussion groups]: /docs/contribute/community/get-involved.md#join_a_discussion_group
