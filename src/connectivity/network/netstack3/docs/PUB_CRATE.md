# `pub` and `pub(crate)` in the core

This document describes our use of the `pub` and `pub(crate)` visibility
modifiers in the core.

TL;DR: The core crate has the following rule: **All non-private items are marked
`pub(crate)` unless they are re-exported from the root.** Read on for an
explanation of why.

The core has a somewhat unique architecture - throughout its module structure,
there are many pieces of functionality which are either private to the module or
public only to the rest of the crate, but there are also items which need to be
exported as part of the core's public API. The canonical way of handling this
situation in Rust is through "re-exports" - in the root, we write something like
`pub use crate::foo::bar::do_thing;`, which has the effect of exporting
`do_thing` even if it's not publicly available via the path
`crate::foo::bar::do_thing`.

This works, but has two major downsides. First, there's no way, when looking at
an item declaration, to know whether it's marked `pub` because it's intended to
be used by other modules in the crate, or because it's going to be re-exported
publicly. Second, there's no way to enforce that we don't accidentally use some
item which is *not* re-exported in the API of one that is. So long as both are
`pub`, the compiler can't tell that one is re-exported and the other isn't.
E.g., consider this code:

```rust
pub struct Foo;

pub fn takes_foo(foo: Foo) -> { ... }
```

If `takes_foo` is re-exported, but `Foo` is not, then there's no way for a
caller to actually use this function since they can't construct a `Foo`.

To get around these issues, we introduce the following rule:

**All non-private items are marked `pub(crate)` unless they are re-exported from
the root.**

This addresses both problems. First, it makes it clear from the definition site
whether an item is re-exported or not. Second, it makes it so that using a
non-re-exported item in a re-exported one will result in a compiler error.

*NOTE: This rule is violated in one place: the `ip::types` module. See the
documentation in that module for an explanation of why.*