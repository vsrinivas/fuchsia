# The `#[ip_test]` and `#[ip_addr_test]` macros

The `#[ip_test]` and `#[ip_addr_test]` macros provide a shorthand to define
tests that are parameterized and need to run on both IP versions.

We will use `#[ip_test]` in this explanation, but the same explanation
applies just as well to `#[ip_addr_test]`.

You can define a test that is parameterized over an IP version as follows:

```rust
#[ip_test]
fn test_foo<I: Ip>() {
   assert!(do_ip_specific_thing::<I>());
   /* ... */
}
```

A function marked with `#[ip_test]` or `#[ip_addr_test]` must *always*:
* Receive zero arguments
* Have *exactly one* type parameter that
   * Has an `Ip` trait bound for `#[ip_test]`
   * Has an `IpAddress` trait bound for `#[ip_addr_test]`

The `#[ip_test]` and `#[ip_addr_test]` macros generate code from that example
that looks like:

```rust
fn test_foo<I: Ip>() {
   assert!(do_ip_specific_thing::<I>());
   /* ... */
}

#[test]
fn test_foo_v4() {
   test_foo::<Ipv4>();
}

#[test]
fn test_foo_v6() {
   test_foo::<Ipv6>();
}
```

For test attributes, you can just add them as you would for a regular test:

```rust
#[ip_test]
#[should_panic]
fn test_foo_panics<I: Ip>() {
    /* ... */
   do_ip_thing_that_panics::<I>();
}
```