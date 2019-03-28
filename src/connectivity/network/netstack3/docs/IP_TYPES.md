# IP types

In order to fully leverage Rust's type system, we use two traits - `Ip` and
`IpAddr` - to abstract over the versions of the IP protocol. The `Ip` trait
represents the protocol version itself, including details such as the protocol
version number, the protocol's loopback subnet, etc. The `IpAddr` trait
represents an actual IP address - `Ipv4Addr` or `Ipv6Addr`.

As much as possible, code which must be aware of IP version should be generic
over either the `Ip` or `IpAddr` traits. This allows common code to be only
written once, while still allowing protocol-specific logic when needed. It also
leverages the type system to provide a level of compile-time assurance that
would not be possible using runtime types like an enum with one variant for each
address type. Consider, for example, this function from the `device` module:

```
/// Get the IP address associated with this device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device: DeviceAddr) -> Option<A>
```

Without trait bounds, this would either need to take an object at runtime
identifying which IP version was desired, which would lose type safety, or would
require two distinct functions, `get_ipv4_addr` and `get_ipv6_addr`, which would
result in a large amount of code duplication (if this pattern were used
throughout the codebase).

### Specialization

Sometimes, it is necessary to execute IP version-specific logic. In these cases,
it is necessary to have Rust run different code depending on the concrete type
that a function is instantiated with. We could imagine code along the lines of:

```
/// Get the IPv4 address associated with this Ethernet device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device_id: u64) -> Option<A>;

pub fn get_ip_addr<Ipv4Addr>(state: &mut StackState, device_id: u64) -> Option<Ipv4Addr> {
    get_device_state(state, device_id).ipv4_addr
}

pub fn get_ip_addr<Ipv6Addr>(state: &mut StackState, device_id: u64) -> Option<Ipv6Addr> {
    get_device_state(state, device_id).ipv6_addr
}
```

This is a feature called "bare function specialization", and it unfortunately
doesn't yet exist in Rust. However, a function called "impl specialization" does
exist, and we can leverage it to accomplish something similar. While the details
of implementing this logic using impl specialization involve some annoying
boilerplate, we provide the `specialize_ip!` and `specialize_ip_addr!` macros to
make it easier. Using `specialize_ip_addr!`, the above hypothetical code can be
written today as:

```
/// Get the IPv4 address associated with this Ethernet device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device_id: u64) -> Option<A> {
    specialize_ip_addr!(
        fn get_ip_addr(state: &EthernetDeviceState) -> Option<Self> {
            Ipv4Addr => { state.ipv4_addr }
            Ipv6Addr => { state.ipv6_addr }
        }
    );
    A::get_ip_addr(get_device_state(state, device_id))
}
```

`get_ip_addr` is added as an associated function on the `IpAddr` trait, where
the implementation for `Ipv4Addr` is given in the first block, and the
implementation for `Ipv6Addr` is given in the second block. This allows us to
invoke it as `A::get_ip_addr`.

A few oddities to note: For reasons having to do with limitations of Rust
macros, the type which implements `IpAddr` must be written as `Self` in the
function definition. That's where the `Option<Self>` comes from. The body is
structured like a sort of type-level macro, where the branches are labeled by
the concrete types - `Ipv4Addr` or `Ipv6Addr` - that the function is
instantiated with. Finally, the blocks associated with these labels become the
function bodies, and so must return, in this case, `Option<Ipv4Addr>` or
`Option<Ipv6Addr>` respectively.
