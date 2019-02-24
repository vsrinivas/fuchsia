# Static Typing

This document describes and motivates the use of static typing in the recovery
netstack.

The recovery netstack makes heavy use of static typing largely for the same
reasons that any program does - to catch bugs at compile time. But there are a
number of benefits to static typing that are more specific to our use case that
are worth calling out.

## Guaranteeing invariants and preventing denial-of-service attacks

Often, there are invariants that we want to uphold which are not as simple as
type safety. For example, consider the act of setting the address and subnet on
an interface. Here's one way we might write a function to do this:

```rust
pub fn set_ip_addr(iface: Interface, addr: IpAddr, network: IpAddr, prefix: u8) { ... }
```

In this example, we'll assume that `IpAddr` is either an IPv4 address or an IPv6
address, just like the standard library's `std::net::IpAddr`. We can observe a
number of invariants which the caller must uphold:
- `addr` and `network` must be of the same IP version
- `prefix` must be a valid prefix length for this IP version (no more than 32
  bits for IPv4 or 128 bits for IPv6)
- `network` must only have its uppermost `prefix` bits set
- `addr` must be an address in the subnet identified by `network` and `prefix`

Assuming we wanted to design the interface this way, the canonical way of
enforcing these invariants would be a) to document them in a doc comment and, b)
to verify them at runtime, panicking if they are not upheld.

Now let's assume that `set_ip_addr` is called at the bottom of a long,
complicated sequence of function calls that originate with a request from a
local application client. How can we ensure that the invariants required by
`set_ip_addr` are bubbled all the way up through the application to the point of
accepting external input? This is a crucial question because, if we get this
wrong, a client of the netstack could pass invalid parameters, and have those
parameters make it to `set_ip_addr`, which would panic and crash the entire
netstack.

To address this problem, we instead write `set_ip_addr` like this:

```rust
pub fn set_ip_addr<A: IpAddress>(iface: Interface, addr_subnet: AddrSubnet<A>) { ... }
```

An `AddrSubnet` is a type which guarantees all of the invariants we listed
above. Instead of having to worry about documenting the invariants and hoping
that the caller upholds them, we can simply assume that, by virtue of the fact
that the caller has an `AddrSubnet` value, the invariants *must* be upheld.

What's more, `AddrSubnet` has the following constructor:

```rust
impl<A: IpAddress> AddrSubnet<A> {
    pub fn new(addr: A, prefix: u8) -> Option<AddrSubnet<A>> { ... }
}
```

The caller must guarantee that `prefix` is valid for the IP version as described
above. However, instead of panicking if `prefix` is invalid, `new` simply
returns `None`. This forces the programmer to be cognizant of the potential for
error even if they didn't read the documentation. If `new` panicked on an
invalid `prefix`, we could write the innocuous code:

```rust
let (addr, prefix) = read_addr_prefix_from_client();
let addr_subnet = AddrSubnet::new(addr, prefix);
...
```

To someone reading this code, it's not at all clear that there's a
denial-of-service waiting to happen if a client passes an invalid `prefix`.
Instead, with `new` returning an `Option`, this code becomes:

```rust
let (addr, prefix) = read_addr_prefix_from_client();
let addr_subnet = AddrSubnet::new(addr, prefix).unwrap();
...
```

The `unwrap` makes it clear what's happening, and will hopefully tip off a code
reviewer to the issue.

There's one final benefit: Function authors are encouraged to take these types
as arguments, rather than the raw inputs from which the types are produced. This
has the effect of naturally pushing input validation as close to client-provided
input as possible, which is exactly where we want it.

To summarize, using static typing to uphold invariants gives us the following benefits:
- It allows us to express our invariants in code rather than documentation,
  making it much harder to violate them accidentally
- It provides us with types whose existence *proves* that the invariants are
  upheld, meaning that any function accepting those types as arguments can
  simply rely on the invariants being maintained by the caller
- As a result of the previous property, it becomes both trivial and natural to
  perform input validation as close to the client as possible, leaving the core
  of the application to simply operate on already-verified values
