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
pub fn get_ip_addr<A: IpAddress>(state: &mut StackState, device_id: u64) -> Option<A>;

pub fn get_ip_addr<Ipv4Addr>(state: &mut StackState, device_id: u64) -> Option<Ipv4Addr> {
    get_device_state(state, device_id).ipv4_addr
}

pub fn get_ip_addr<Ipv6Addr>(state: &mut StackState, device_id: u64) -> Option<Ipv6Addr> {
    get_device_state(state, device_id).ipv6_addr
}
```

This is a feature called "bare function specialization", and it unfortunately
doesn't yet exist in Rust. It's possible to emulate this with the unstable
[`min_specialization` feature][min_specialization], but since that's unlikely to
be stabilized soon, we rely on extension traits and matching on the IP version
instead. The latter is fairly simple:

```
/// Returns the bytes of the address as a Vec<u8>
pub fn get_addr_bytes<A: IpAddress>(a: A) -> Vec<u8> {
    match IpAddr::from(a) {
        IpAddr::V4(a) => a.ipv4_bytes().to_vec(),
        IpAddr::V6(a) => a.ipv6_bytes().to_vec(),
    }
}
```

Using extension traits requires a bit more boilerplate:
```
trait IpAddressExt: IpAddress {
    fn get_ip_addr(state: &EthernetDeviceState) -> Option<Self>;
}

impl IpAddressExt for Ipv4Addr {
    fn get_ip_addr(state: &EthernetDeviceState) -> Option<Self> {
        state.ipv4_addr
    }
}

impl IpAddressExt for Ipv6Addr {
    fn get_ip_addr(state: &EthernetDeviceState) -> Option<Self> {
        state.ipv6_addr
    }
}

/// Get the IPv4 address associated with this Ethernet device.
pub fn get_ip_addr<A: IpAddressExt>(state: &mut StackState, device_id: u64) -> Option<A> {
    // Note the bound on `A`: using `IpAddressExt` lets us call this method.
    A::get_ip_addr(get_device_state(state, device_id))
}
```

`get_ip_addr` is added as an associated function on the `IpAddressExt` trait,
which is implemented separately for `Ipv4Addr` and `Ipv6Addr`. The trait bound
for the free function `get_ip_addr` is `A: IpAddressExt`, which allows it to
invoke the trait method as `A::get_ip_addr`.

[min_specialization]: https://github.com/rust-lang/rust/issues/31844