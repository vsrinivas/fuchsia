# Review process for external Rust crates

**Here because you need to do a review? Skip straight to the [PROCESS](#process)
and [REVIEW GUIDELINES](#review-guidelines).**

## SUMMARY

Rust's external ecosystem of libraries, or crates, is an enormous benefit of
using the language. This body of existing code can be a force multiplier for a
team of any size, accelerating and de-risking development.

At the same time, external code, like all code, comes with risks. We should use
external crates, but use them wisely! This doc is about how to maximize the
benefits while understanding and minimizing risks.

## OBJECTIVE

Propose and establish a set of guidelines for anyone reviewing external Rust
code.

## BACKGROUND

With roughly 1 million lines of external Rust code in our tree at the time of
writing, code review of new crates and updates to existing crates takes
considerable work. In order to cope with the scale of Rust in our codebase
today, we must empower any team using Rust to review crates that the team
depends on.

At the same time, there is uncertainty about how to review external code.
Reviews of external code are different from first-party code reviews, and norms
in the industry are not well established. In fact, many smaller organizations do
not do external code review at all, relying entirely on other quality signals.

On a project like Fuchsia, we cannot tolerate such risks and must review code
that we use to ship our products. At the same time, we should focus our efforts
to maximize benefits while minimizing process overhead.

## PRINCIPLES

**Allocate review effort according to risk.** Time and effort are limited.
Identify the biggest risks that external code may introduce, and focus efforts
on minimizing these. Compare these risks to those of writing a new
implementation.

**Consider future costs and benefits.** Look beyond the here and now – a
convenient shortcut today could become a big problem later. Solving a more
general class of problems now can have repeated benefits over the life of the
project.

**Be wary of surprises.** Remember that we will pay the cost of clearing up a
confusing piece of code or its API many times over.

**No default outcome.** All of these principles apply both to the decision to
use external code **and** to the decision to implement ourselves.

**Give back.** Open source libraries can be a major benefit to the project. When
we review them, make the details of those reviews accessible and legible to
people outside the project.

## PROCESS {#process}

**Crate status**    | [Architecture] | [Quality] | [Code] | [OSRB]\*
------------------- | -------------- | --------- | ------ | ---------
Added to Cargo.toml | ✅              | ✅         | ✅      | ✅
Added dependency    |                | ✅         | ✅      | ✅
Version updated     |                |           | ✅      | see below

[Architecture]: #architecture
[Quality]: #quality
[Code]: #code-review
[OSRB]: /docs/contribute/governance/policy/osrb-process.md

*\* Complete before uploading.*

*OSRB review for crate updates is only required when licenses (including
per-file licenses) change.*

Adding a crate to Cargo.toml means Fuchsia code can use it directly. "Added
dependency" refers to newly vendored crates which are not added to Cargo.toml,
but are a dependency of some other crate.

**When adding or updating an external crate:**

1.  Follow the
    [external Rust crates](/docs/development/languages/rust/external_crates.md)
    documentation to add the crate to your tree, but **do not** upload a change
    to Gerrit yet.
2.  If there are any new crates, including dependencies, or changes to licenses
    in new versions of existing crates, request
    [OSRB approval](/docs/contribute/governance/policy/osrb-process.md).
3.  After receiving OSRB approval for any new crates, upload a change to Gerrit.
    *   Cite OSRB approval bugs in the commit message.
    *   Separate first-party code changes from external code changes where
        possible.
    *   If the crate requires updating a number of transitive dependencies,
        consider using
        [`cargo update`](https://doc.rust-lang.org/cargo/commands/cargo-update.html)
        to batch the transitive updates into one or more batches to reduce the
        CL size. In cases where a dependency isn't used on our supported
        platforms, we can minimize review overhead by
        [replacing the crate](/docs/development/languages/rust/external_crates.md#importing_a_subset_of_files_in_a_crate)
        with a stub crate using cargo's patch feature.
4.  Add reviewers to the CL. Anyone (including you) can review, as long as they
    understand this section and the guidelines below.
    *   You can find reviewers by asking in #rust on the Fuchsia discord (or
        other chat rooms where Fuchsia rustaceans can be found), or on
        [this page](/docs/development/languages/rust#rust_idiomatic_usage_review).[^1]
    *   If the review requires domain-specific expertise (including unsafe
        code), look for reviewers with that expertise.
    *   A reviewer should know which crates they are being asked to review, if
        not all of them. You may assign crates to individual reviewers, which
        helps with large CLs.
5.  Once all code has been reviewed, add one of the
    [`rust_crates` `OWNERS`](https://cs.opensource.google/fuchsia/fuchsia/+/master:third_party/rust_crates/OWNERS)
    for final approval.[^2] Their job is to make sure the process has been
    correctly followed, which should be clearly supported in CL comments. Being
    proactive will help ensure a quick approval.

**When reviewing an external crate:**

1.  Review the code according to the guidelines below, applying your best
    judgment and seeking outside assistance when necessary.
2.  Comment on the code saying which crates you reviewed. Note any concerns you
    have or caveats about the review. Note any surprising behavior or bugs
    inline. **In general, note any risks, even if you do not think they should
    block merging.**
    1.  **On the top line of your CL-level comment**, say which crate(s) and
        versions you reviewed, along with and use an asterisk if there are any
        caveats or risks noted in your review. You can abbreviate this by saying
        "all crates" or "all crates except…" This is so the OWNERS can quickly
        skim the comments before giving final approval.
3.  If the code looks acceptable for merging, add CR+1. If you found significant
    bugs or other red flags, add CR-1 and optionally suggest a resolution:
    1.  Patching the crate upstream before merging.
    2.  Closing the CL and looking for alternatives.
    3.  In uncommon cases when the crate is a dependency that isn't used on our
        supported platforms, we can
        [replace the offending crate](/docs/development/languages/rust/external_crates.md#importing_a_subset_of_files_in_a_crate)
        with a customized version or a stub crate using the cargo patch feature.

**When approving an external addition or update (OWNERS):**

1.  Look for evidence the guidelines below have been followed on the CL.
    1.  Also see the above checklist as a reminder.
    2.  Prefer comments on the CL to informal communication, which leaves no
        audit trail, whenever possible.
    3.  If evidence is missing, point the CL owner to this document and ask them
        to complete the process.
2.  Review any risks noted by the reviewers.
    1.  If you need clarification, ask for it.
    2.  If you think any risks should block merging or warrant further
        discussion, say so and add CR-2.
3.  If you can see that the guidelines were appropriately followed, that any
    risks are acceptable, and you are comfortable with merging, add CR+2.

## REVIEW GUIDELINES {#review-guidelines}

There are three categories of code, each of which includes the category before
it: new crates being added for first-party use, new crates being added for any
use, and all new code. For any given category, the guidelines of the later
categories also apply to it.

### Architecture review for crates used by Fuchsia code {#architecture}

When adding a new crate to be used by code in our tree (in other words, it is
listed in Cargo.toml), new users *and* reviewers should ask the questions below.

The architectural review is meant to catch "obvious" problems, and is intended
to be fairly lightweight. If there is uncertainty as to whether a crate makes
sense from an architectural perspective, it should be noted on the CL so the
author and approvers can make a final judgment.

Note that these questions do *not* apply to transitive dependencies that we
won't use directly. (When adding existing transitive dependencies to Cargo.toml,
we should review them for these questions.)

**Do we have a similar crate in the tree that could be used instead?**

**How many dependencies will this crate pull in, and how big are they?**

Is the cost of reviewing and updating them disproportionate to the benefit we
gain by using the crate?

**Does this crate make sense in the context of our architecture?**

Examples for code running on Fuchsia targets:

*   Usability in an async context (does the API require blocking semantics?)
*   Heavy reliance on POSIX emulation

**Does this crate have a sensible API with sufficient documentation?**

*   If the API is simple and self-evident, minimal documentation is OK.
*   If the API contains complex abstractions, lack of documentation has a cost.
*   If the API has undocumented invariants, especially unsafe, it's highly
    risky.

### Quality review for newly vendored crates {#quality}

In addition to the review guidelines below, reviewers should give extra
consideration to new crates, whether they are being used directly by us or as a
dependency of another crate.

#### Make sure the crate is [OSRB approved](/docs/contribute/governance/policy/osrb-process.md).

Warning: You must receive approval from the OSRB before pushing a commit to
Gerrit that adds external code.

#### Think about the future.

**Is the review of this crate *and* its dependencies worth the benefits of using
its API?** Include future reviews of updates.

**What other contexts might this crate be used in?** Consider that the
implementation will not be reviewed again for each new use.

In particular, if you think a crate should only ever be used on a particular
platform, or review it with that assumption in mind, this should be stated in a
comment in Cargo.toml. If a crate is not safe to use in certain contexts, those
must be restricted from the build or the crate cannot be imported.

**What would happen if the maintainer abandons the crate?** Would we be willing
to fork and maintain it ourselves?

#### Pay attention to signals of quality.

##### First-order signals

*   **Testing:** Lack of testing in a crate is a red flag. Tests don't all need
    to be reviewed, but it is worth spot checking some tests for meaningful
    semantic checks and good coverage. Also check if a crate's tests are passing
    in its CI, or if they pass in a local checkout with `cargo test`. Solid
    testing is a very good sign.

##### Second-order signals

*   **Multiple maintainers**
*   **Well-known authors**
*   **Well-used reverse dependencies** ("Dependents" on crates.io)
*   **Activity in the repository and issue tracker**

All of these can be gleaned from [crates.io](https://crates.io/), or the source
repository which is usually linked to from there.

Missing second-order criteria should not disqualify the crate, but they can be a
useful data point. Code review, while always required, is almost never
exhaustive. These signals can help fill in gaps and tip the balance in moments
of ambiguity.

### Code review for all external code {#code-review}

When reviewing external code, whether it is a new crate or updates to an
existing one:

#### Look for risks.

**`unsafe` code.** Unsafe code should only be used when necessary. It should be
easy to follow and/or document its invariants and how they are preserved. Unsafe
APIs should be very rare and must always document the invariants the caller
needs to uphold.[^3]

It is common practice to ask someone more experienced in Rust to review unsafe
code. If you aren't completely comfortable reviewing unsafe, please ask another
reviewer.

**Code that requires specialized domain expertise to understand.** If possible,
find a domain expert to review this code. Examples include unsafe, low-level
atomics and concurrency, cryptography, and network protocol implementations.

**Code meant to be used in critical paths.** This includes security-critical
paths, like cryptography, as well as performance-critical paths. Pay special
attention to this code to make sure it doesn't compromise the critical path.

**Overly complicated code.** Idiomatic Rust leverages appropriate levels of
abstraction, using the type system to manage invariants when possible. If the
code is hard to follow and leaves you without confidence that those invariants
are well managed, it may be of low quality and contain avoidable bugs.

**Remember the alternative.** Assuming we need this functionality, would we be
navigating the same risks if we wrote the code ourselves? Would doing so
actually produce better results, and would it be worth the effort of writing and
maintaining that code?

#### Verify for correctness, but don't go overboard.

**Verify that the implementation makes sense** given the function signature,
trait, and so on.

**Look for surprises**, and note any that you find in CL comments (ideally
inline with the code).

**Focus on the code in front of you.** It's okay to assume that other functions
do what they say (you'll review them eventually). Tracing the entire function
call graph shouldn't be necessary; if it is, that's usually a bad sign. Use your
best judgment to focus your efforts.

**`build.rs` changes should be reflected in our build.** [`build.rs` scripts] do
not run in our build, but need to get translated when vendoring the crate.
cargo-gnaw has limited support for this, but it doesn't catch everything. Look
over changes to these and verify anything relevant to our build is reflected in
our build rules. If you aren't comfortable reviewing these, ask the CL owner to
find another reviewer for them.

[`build.rs` scripts]: https://doc.rust-lang.org/cargo/reference/build-scripts.html

#### Skip what's irrelevant.

You don't need to review the following:

*   Code style
*   Unchanged implementation code that was already reviewed
*   Individual test cases and benchmarks
*   Platform-specific code on platforms we are sure we would never use it on[^4]
*   Documentation that isn't directly helpful in understanding the API surface
    and implementation well enough to review it

Some of these are still relevant when assessing the quality of new crates,
discussed above.

**Note:** When a second major version of a crate is added, the old version will
be moved to a new path, which makes it show up as new code in Gerrit. If that
code was already in our tree, you can skip it as "already reviewed". Check
Cargo.lock to see which versions are pre-existing. **The checksum hash for this
version in Cargo.lock should *not* have changed.**

## DOCUMENT HISTORY

Prior to landing on fuchsia.dev, this document existed as an [internal document]
for a couple of years.

**Created:** 2021-03-04

**Author:** tmandry

**Contributors:** adamperry, raggi, dnordstrom, dhobsd, etryzelaar

**Reviewers:** computerdruid, adamperry, etryzelaar, dhobsd, raggi, sadmac,
bruodalbo, robtsuk, bryanhenry

**Timeline:**

*   2021-03-10: Initial draft
*   2021-04-07: Second draft
*   2021-04-13: Final / adopted draft

[internal document]: https://goto.google.com/tq-rust-external-reviews-v1

## APPENDIX

##### More reading

*   [Fuchsia Rust 3p OWNERS](http://fxrev.dev/502938)

## Notes

[^1]: In the near future we expect to create a Rust reviewer list that
    automatically finds and assigns reviewers from a pool of volunteers.
[^2]: In the near future we
    [expect Rust third party crates to be assigned more granular ownership](http://fxrev.dev/502938)
    for reviews which will allow crate upgrades to be managed by a broader set
    of people.
[^3]: Also see our
    [guidelines for unsafe](/docs/development/languages/rust/unsafe.md)
    in first-party code.
[^4]: We support Fuchsia, Linux, and Mac, and should expect to support Windows
    at some point. Some of our Rust code is also compiled for wasm targets.
