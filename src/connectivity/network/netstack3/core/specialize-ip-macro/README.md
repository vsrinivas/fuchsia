# The `#[specialize_ip]` and `#[specialize_ip_addr]` proc macros

This crate defines the `#[specialize_ip]` and `#[specialize_ip_addr]` proc
macros. These allow the creation of functions which are generic over the `Ip` or
`IpAddr` traits, but provide specialized behavior depending on which concrete
type is given. The netstack requires a lot of protocol-specific logic, and these
proc macros make that logic easy.

We will use `#[specialize_ip]` in this explanation, but the same explanation
applies just as well to `#[specialize_ip_addr]`.

The `#[specialize_ip]` attribute can be placed on any function where exactly one
of its type parameters has an `Ip` bound. For example:

```rust
#[specialize_ip]
fn foo<D: EventDispatcher, I: Ip>() { ... }
```

Multiple types with an `Ip` bound are not allowed, and the type with the `Ip`
bound may not also have other bounds. Where clauses are not currently supported.

The result is a function whose signature is identical from the perspective of
those outside the function, but which has different bodies depending on whether
`I` is `Ipv4` or `Ipv6`.

The inside of a specialized function is written as follows:

```rust
#[specialize_ip]
fn foo<D: EventDispatcher, I: Ip>() {
    do_thing_a();

    #[ipv4]
    do_ipv4_thing();

    do_thing_b();

    #[ipv4]
    do_other_ipv4_thing();

    {
        do_thing_c();
        #[ipv6]
        do_ipv6_thing();
    }

    #[ipv6]
    do_other_ipv6_thing();

    do_thing_d();
}
```


The `#[ipv4]` (`#[ipv4addr]`) and `#[ipv6]` (`#[ipv6addr]`) attributes are used
to indicate a statement which should only be included in a particular version of
the function. If multiple statements are required, a block can be used:

```rust
#[ipv4]
{
    do_first_ipv4_thing();
    do_second_ipv4_thing();
}
```

Any statement not annotated with `#[ipv4]` or `#[ipv6]` will be present in both
versions of the function, while statements annotated with `#[ipv4]` will be
removed in the `Ipv6` version of the function, and vice versa. The above example
would compile into:

```rust
/// `Ipv4` version
fn foo<D: EventDispatcher>() {
    do_thing_a();

    do_ipv4_thing();

    do_thing_b();

    do_other_ipv4_thing();

    {
        do_thing_c();
    }

    do_thing_d();
}

/// `Ipv6` version
fn foo<D: EventDispatcher>() {
    do_thing_a();

    do_thing_b();

    {
        do_thing_c();
        do_ipv6_thing();
    }

    do_other_ipv6_thing();

    do_thing_d();
}
```

## Limitations

### Statements vs Expressions

Due to the way the Rust parser works, only statements may be annotated with
`#[ipv4]` or `#[ipv6]`; they cannot be used to annotate expressions. In other
words, the following will fail to parse before the proc macro is ever run:

```rust
#[specialize_ip]
fn address_bits<I: Ip>() -> usize {
    #[ipv4]
    32
    #[ipv6]
    128
}
```

As a workaround, an explicit `return` can be used:

```rust
#[specialize_ip]
fn address_bits<I: Ip>() -> usize {
    #[ipv4]
    return 32;
    #[ipv6]
    return 128;
}
```

Expressions which are not annotated is fine, so expressions inside annotated
blocks will work fine:

```rust
#[specialize_ip]
fn address_bits<I: Ip>() -> usize {
    #[ipv4]
    {
        32
    }

    #[ipv6]
    {
        128
    }
}
```

### Impl Trait

Under the hood, the macros are implemented by generating and implementing
traits. Rust currently doesn't support the impl trait feature for trait
functions and methods, so they are not supported by our macros either.

## Implementation

Under the hood, these proc macros use impl specialization. An extension trait is
defined on the `Ip` or `IpAddr` traits with a function of the appropriate
signature. The extension trait is implemented for each of the two concrete
types, with the method's body containing the generated code. All instances of
the type with the `Ip` or `IpAddr` bound are replaced with `Self`. For example,
this:

```rust
fn foo<D: EventDispatcher, I: Ip>(addr: I::Addr) -> I::Addr {
    do_thing_a();

    #[ipv4]
    let ret = do_ipv4_thing(addr);

    #[ipv6]
    let ret = do_ipv6_thing(addr);

    do_thing_b(&ret);
    ret
}
```

Produces this:

```rust
fn foo<D: EventDispatcher, I: Ip>(addr: I::Addr) -> I::Addr {
    trait Ext: Ip {
        fn f<D: EventDispatcher>(addr: Self::Addr) -> Self::Addr;
    }
    impl<I: Ip> Ext for I {
        default fn f<D: EventDispatcher>(addr: Self::Addr) -> Self::Addr { unimplemented!() }
    }
    impl Ext for Ipv4 {
        fn f<D: EventDispatcher>(addr: Self::Addr) -> Self::Addr {
            do_thing_a();
            let ret = do_ipv4_thing(addr);
            do_thing_b(&ret);
            ret
        }
    }
    impl Ext for Ipv6 {
        fn f<D: EventDispatcher>(addr: Self::Addr) -> Self::Addr {
            do_thing_a();
            let ret = do_ipv6_thing(addr);
            do_thing_b(&ret);
            ret
        }
    }

    I::f::<D>(addr)
}
```
