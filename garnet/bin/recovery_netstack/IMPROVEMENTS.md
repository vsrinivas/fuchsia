# Recovery Netstack Improvements

This document describes various improvements that we might want to make to
structure or algorithms in the future, but that we don't have concrete plans to
implement. It's essentially a bucket of ideas we don't want to forget.

# Keep Devices in `Vec`s, not `HashMap`s

*Contact: joshlf@google.com*

Currently, `device::DeviceId` has the following signature:

```rust
pub struct DeviceId {
    id: u64,
    protocol: DeviceProtocol,
}

enum DeviceProtocol {
    Ethernet,
    ...
}
```

In order to maintain static typing, we store the state associated with a device
in a `HashMap` which is specific to that protocol type. In other words, Ethernet
devices are stored in a `HashMap<u64, EthernetState>`, Token Ring devices would
(if they existed) be stored in a `HashMap<u64, TokenRingState>`, etc.
Device-layer functions use the `protocol` field of `DeviceId` to determine which
`HashMap` to look in, and then use the `id` field to look up the particular
state.

However, `HashMap`s can be slow. If performance becomes a problem, we may want
to switch to using `Vec` instead. For example, Ethernet state would be stored in
a `Vec<Option<EthernetState>>` (the `Option` for deleted devices - more on that
in a bit). Lookup would then consist of a single index operation.

This would have some drawbacks. For one, the netstack guarantees that device IDs
never repeat, even if a device is removed. Thus, over time, the `Vec` would tend
to get sparse. However, it's likely that this would happen very slowly, as
devices are not removed and added with any frequency. Another problem would be
that, because all device IDs share the same namespace regardless of device type,
all of the `Vec`s would have to be long enough to accomodate every device ID,
even device IDs of other types.

Another concern is security - is it important that device IDs be unpredictable?
Are we concerned that sequential device IDs might leak information that we don't
want to leak? My feeling is that the answer is no, but it's worth considering.

One way to solve these problems might be to have the device collection be an
enum of a `HashMap` and a `Vec`, and have a cutoff point at which, once the
`Vec` grows sufficiently large, a `HashMap` is used instead.

Another option would be to retain randomly-generated IDs, and simply keep
devices in a `Vec` which is scanned linearly on lookup. For small numbers of
devices, this might still be faster than a `HashMap`.