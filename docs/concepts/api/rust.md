# Rust Rubric

[TOC]

This document lists conventions to follow when writing Rust in the Fuchsia Source Tree. These conventions are a combination of best practices, project preferences, and some choices made for the sake of consistency.

<!-- TODO(adamperry) add collapsible <details> sections around guideline bodies -->
<!-- TODO(adamperry) inline text of upstream guidelines once fuchsia-specific guidelines settle -->

## Guidelines

### Naming

#### Casing conforms to Rust idioms
See [C-CASE](https://rust-lang.github.io/api-guidelines/naming.html#c-case).

#### Ad-hoc conversions follow `as_`, `to_`, `into_` conventions
See [C-CONV](https://rust-lang.github.io/api-guidelines/naming.html#c-conv).

#### Getter names follow Rust convention

With a few exceptions, the `get_` prefix is not used for getters in Rust code.

See [C-GETTER](https://rust-lang.github.io/api-guidelines/naming.html#c-getter).

#### Methods on collections that produce iterators follow `iter`, `iter_mut`, `into_iter`
See [C-ITER](https://rust-lang.github.io/api-guidelines/naming.html#c-iter).

#### Iterator type names match the methods that produce them
See [C-ITER-TY](https://rust-lang.github.io/api-guidelines/naming.html#c-iter-ty).

#### Names use a consistent word order
See [C-WORD-ORDER](https://rust-lang.github.io/api-guidelines/naming.html#c-word-order).



### Interoperability

#### Types eagerly implement common traits
`Copy`, `Clone`, `Eq`, `PartialEq`, `Ord`, `PartialOrd`, `Hash`, `Debug`, `Display`, `Default` should all be implemented when appropriate.

See [C-COMMON-TRAITS](https://rust-lang.github.io/api-guidelines/interoperability.html#c-common-traits).

#### Conversions use the standard traits `From`, `AsRef`, `AsMut`
See [C-CONV-TRAITS](https://rust-lang.github.io/api-guidelines/interoperability.html#c-conv-traits).

#### Collections implement `FromIterator` and `Extend`
See [C-COLLECT](https://rust-lang.github.io/api-guidelines/interoperability.html#c-collect).

#### Data structures implement Serde's `Serialize`, `Deserialize`
See [C-SERDE](https://rust-lang.github.io/api-guidelines/interoperability.html#c-serde).

#### Types are `Send` and `Sync` where possible
See [C-SEND-SYNC](https://rust-lang.github.io/api-guidelines/interoperability.html#c-send-sync).

#### Error types are meaningful and well-behaved
See [C-GOOD-ERR](https://rust-lang.github.io/api-guidelines/interoperability.html#c-good-err).

#### Binary number types provide `Hex`, `Octal`, `Binary` formatting
See [C-NUM-FMT](https://rust-lang.github.io/api-guidelines/interoperability.html#c-num-fmt).

#### Generic reader/writer functions take `R: Read` and `W: Write` by value
See [C-RW-VALUE](https://rust-lang.github.io/api-guidelines/interoperability.html#c-rw-value).



### Macros

#### Input syntax is evocative of the output
See [C-EVOCATIVE](https://rust-lang.github.io/api-guidelines/macros.html#c-evocative).

#### Macros compose well with attributes
See [C-MACRO-ATTR](https://rust-lang.github.io/api-guidelines/macros.html#c-macro-attr).

#### Item macros work anywhere that items are allowed
See [C-ANYWHERE](https://rust-lang.github.io/api-guidelines/macros.html#c-anywhere).

#### Item macros support visibility specifiers
See [C-MACRO-VIS](https://rust-lang.github.io/api-guidelines/macros.html#c-macro-vis).

#### Type fragments are flexible
See [C-MACRO-TY](https://rust-lang.github.io/api-guidelines/macros.html#c-macro-ty).



### Documentation

#### Crate level docs are thorough and include examples
See [C-CRATE-DOC](https://rust-lang.github.io/api-guidelines/documentation.html#c-crate-doc).

#### All items have a rustdoc example
See [C-EXAMPLE](https://rust-lang.github.io/api-guidelines/documentation.html#c-example).

> Note: this guideline is not reasonable to enforce for targets which build on Fuchsia until
> doctests are supported on Fuchsia targets. See
> [#38215](https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=38215).

#### Examples use `?`, not `try!`, not `unwrap`
See [C-QUESTION-MARK](https://rust-lang.github.io/api-guidelines/documentation.html#c-question-mark).

<!-- TODO how does this interact with avoiding ? in tests? -->

#### Function docs include error, panic, and safety considerations
See [C-FAILURE](https://rust-lang.github.io/api-guidelines/documentation.html#c-failure).

#### Prose contains hyperlinks to relevant things
See [C-LINK](https://rust-lang.github.io/api-guidelines/documentation.html#c-link).

#### Rustdoc does not show unhelpful implementation details
See [C-HIDDEN](https://rust-lang.github.io/api-guidelines/documentation.html#c-hidden).



### Predictability

#### Smart pointers do not add inherent methods
See [C-SMART-PTR](https://rust-lang.github.io/api-guidelines/predictability.html#c-smart-ptr).

#### Conversions live on the most specific type involved
See [C-CONV-SPECIFIC](https://rust-lang.github.io/api-guidelines/predictability.html#c-conv-specific)

#### Functions with a clear receiver are methods
See [C-METHOD](https://rust-lang.github.io/api-guidelines/predictability.html#c-method).

#### Functions do not take out-parameters
See [C-NO-OUT](https://rust-lang.github.io/api-guidelines/predictability.html#c-no-out).

#### Operator overloads are unsurprising
See [C-OVERLOAD](https://rust-lang.github.io/api-guidelines/predictability.html#c-overload).

#### Only smart pointers implement `Deref` and `DerefMut`
See [C-DEREF](https://rust-lang.github.io/api-guidelines/predictability.html#c-deref).

#### Constructors are static, inherent methods
See [C-CTOR](https://rust-lang.github.io/api-guidelines/predictability.html#c-ctor).



### Flexibility

#### Functions expose intermediate results to avoid duplicate work
See [C-INTERMEDIATE](https://rust-lang.github.io/api-guidelines/flexibility.html#c-intermediate).

#### Caller decides where to copy and place data
See [C-CALLER-CONTROL](https://rust-lang.github.io/api-guidelines/flexibility.html#c-caller-control).

#### Functions minimize assumptions about parameters by using generics
See [C-GENERIC](https://rust-lang.github.io/api-guidelines/flexibility.html#c-generic).

#### Traits are object-safe if they may be useful as a trait object
See [C-OBJECT](https://rust-lang.github.io/api-guidelines/flexibility.html#c-object).



### Type safety

#### Newtypes provide static distinctions
See [C-NEWTYPE](https://rust-lang.github.io/api-guidelines/type-safety.html#c-newtype).

#### Arguments convey meaning through types, not `bool` or `Option`
See [C-CUSTOM-TYPE](https://rust-lang.github.io/api-guidelines/type-safety.html#c-custom-type).

#### Types for a set of flags are `bitflags`, not enums
See [C-BITFLAG](https://rust-lang.github.io/api-guidelines/type-safety.html#c-bitflag).

#### Builders enable construction of complex values
See [C-BUILDER](https://rust-lang.github.io/api-guidelines/type-safety.html#c-builder).



### Dependability

#### Functions validate their arguments
See [C-VALIDATE](https://rust-lang.github.io/api-guidelines/dependability.html#c-validate).

#### Destructors never fail
See [C-DTOR-FAIL](https://rust-lang.github.io/api-guidelines/dependability.html#c-dtor-fail).

#### Destructors that may block have alternatives
See [C-DTOR-BLOCK](https://rust-lang.github.io/api-guidelines/dependability.html#c-dtor-block).



### Debuggability

#### All public types implement `Debug`
See [C-DEBUG](https://rust-lang.github.io/api-guidelines/debuggability.html#c-debug).

#### `Debug` representation is never empty
See [C-DEBUG-NONEMPTY](https://rust-lang.github.io/api-guidelines/debuggability.html#c-debug-nonempty).



### Future Proofing

#### Sealed traits protect against downstream implementations
See [C-SEALED](https://rust-lang.github.io/api-guidelines/future-proofing.html#c-sealed).

#### Structs have private fields
See [C-STRUCT-PRIVATE](https://rust-lang.github.io/api-guidelines/future-proofing.html#c-struct-private).

#### Newtypes encapsulate implementation details
See [C-NEWTYPE-HIDE](https://rust-lang.github.io/api-guidelines/future-proofing.html#c-newtype-hide).

#### Data structures do not duplicate derived trait bounds
See [C-STRUCT-BOUNDS](https://rust-lang.github.io/api-guidelines/future-proofing.html#c-struct-bounds).


## Updating the guidelines

This rubric is currently maintained by the Rust on Fuchsia Working Group. To propose additions or
modifications, open a CL and cc [`fuchsia-rust-api-rubric@google.com`][rubric-group] to ensure it
is reviewed.

[rubric-group]: mailto:fuchsia-rust-api-rubric@google.com

## Relationship with upstream Rust API guidelines

This rubric contains most of the [Rust API Guidelines][rust-guidelines], however the following
official guidelines are omitted:

* [C-FEATURE](https://rust-lang.github.io/api-guidelines/naming.html#c-feature) as Fuchsia does not
  currently support features for crates.
* [C-METADATA](https://rust-lang.github.io/api-guidelines/documentation.html#c-metadata) as Fuchsia
  does not maintain internal `Cargo.toml` files.
* [C-HTML-ROOT](https://rust-lang.github.io/api-guidelines/documentation.html#c-html-root) as
  Fuchsia does not currently publish most Rust code to `crates.io`.
* [C-RELNOTES](https://rust-lang.github.io/api-guidelines/documentation.html#c-relnotes) as most
  Rust code in Fuchsia "lives at HEAD".
* [C-STABLE](https://rust-lang.github.io/api-guidelines/necessities.html#c-stable) as Fuchsia does
  not currently publish most Rust code to `crates.io`.
* [C-PERMISSIVE](https://rust-lang.github.io/api-guidelines/necessities.html#c-permissive) as all of
  Fuchsia's Rust code is under the Fuchsia license.

<!--
The following Fuchsia-specific guidelines are included:

* TODO
-->

[rust-guidelines]: https://rust-lang.github.io/api-guidelines/about.html
