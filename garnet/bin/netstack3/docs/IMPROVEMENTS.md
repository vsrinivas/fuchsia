# Recovery Netstack Improvements

This document describes various improvements that we might want to make to
structure or algorithms in the future, but that we don't have concrete plans to
implement. It's essentially a bucket of ideas we don't want to forget.

## Performance Issues

This section documents design and implementation details that we speculate may
cause performance issues.

### Single-threaded execution

*Contact: joshlf@google.com*

Our single-threaded execution model is great for simplicity, but will likely run
into problems in practice depending on our performance goals. These include:
- Large state changes (add/remove device, update forwarding table, etc) take
  much longer to compute than a normal event like receipt of a packet, and stall
  the entire event loop until they complete. This may introduce unacceptably
  large latency spikes.
- If packet flow is high enough (e.g., in the case of a router forwarding
  traffic for a network), the single execution thread may not be able to keep up
  with throughput.

#### Possible solutions
- Use [read-copy-update](https://en.wikipedia.org/wiki/Read-copy-update) (RCU)
  or any other read-optimized architecture. It is likely that any performance
  goals we have regardless of application domain will heavily favor read
  throughput over write throughput. "Reading" here refers to reading shared data
  structures like device state, connection state, forwarding tables, etc. In a
  read-optimized architecture, most threads could spend most of their time
  handling common events like packet receipts or application client messages,
  and stay on the optimized, read-only hot path most of the time. Any write
  operations such as device insertion/removal, forwarding table update, etc
  would likely perform much worse than in the current model, but that's probably
  an acceptable tradeoff.

  Note that RCU and similar schemes can incur significant complexity. However, I
  believe that Rust should make it much easier to encapsulate this complexity
  such that most of the stack can remain agnostic to these details.

  Another thing to keep in mind with this approach is that the read/write
  distinction is not a perfect one. For example, handling a segment in a TCP
  connection involves read-only operations on global data structures such as the
  forwarding table, but involves write operations on the TCP connection state.
  It's likely that, even in an read-optimized world, coarser-grained
  synchronization around less global state like TCP connection state would be
  acceptable.

### `debug_err!` macro

*Contact: joshlf@google.com*

The `debug_err!` macro is used to emit a debug log and then return an error
value. The `debug_err_fn!` macro produces a closure which calls `debug_err!`.
They are both used extensively in the `wire` module during packet parsing.
As of this writing, `debug_err!` is implemented as follows:

```rust
/// Emit a debug message and return an error.
///
/// Invoke the `debug!` macro on all but the first argument. A call to
/// `debug_err!(err, ...)` is an expression whose value is the expression `err`.
macro_rules! debug_err {
    ($err:expr, $($arg:tt)*) => (
        {
            use ::log::debug;
            debug!($($arg)*);
            $err
        }
    )
}
```

I speculate that the unconditional use of the `debug!` macro may cause
performance problems as, by default, it checks to see whether debug logging is
enabled. One way to alleviate this issue would be some kind of conditional
compilation. E.g., we could add a compile-time flag to enable or disable the
`debug!` invocation entirely. We could also use the `log` crate's
`release_max_level_xxx` features to disable debug logging crate-wide.

### Pervasive use of `HashMap`s

*Contact: joshlf@google.com*

In order to play nicely with Rust's ownership model, we generally pass `HashMap`
keys around where implementations in other languages might pass pointers. This
allows most functions to take a mutable reference to the entire state, but also
requires that we do fairly frequent hash map lookups even in the hot path.

In certain cases, it may make sense to replace hash maps with vectors (e.g., see
"Devices in `HashMap`s" section below). However, vectors require either linear
scanning or keys which can be used to index into the vector. Neither of these is
viable in the general case due to either large sets of data (too large to
linearly scan) or large, unstructured keys (unsuitable for indexing directly
into a vector).

#### Possible solutions
- Use `Rc<RefCell<T>>` for hash map entries. This allows us to cache
  reference-counted pointers which point directly to the hash map entries. It
  requires a reference count check in order to access the `RefCell`, although
  this is undoubtedly much faster than a hash map lookup. The big disadvantage
  is that it requires heap allocating the hash map entries rather than storing
  them inline, which could significantly worsen both cache locality and cache
  utilization.
- Use `BTreeMap`s. `BTreeMap`s are not quite as general as `HashMap`s since they
  require their keys to be ordered, but in practice, most of our keys are. For
  smaller maps, `BTreeMap`s are superior due to not having to compute a hash.
  They are still pretty good for cache locality and utilization, although not as
  good as `HashMap`s. When they get large, they lag behind `HashMap`s. We would
  need to figure out where the cutoff point is, and how large we expect our
  working sets to be for various map instances. DoS vulnerability may be also be
  a concern for `BTreeMap`s (a maliciously-chosen set of keys could induce the
  pathalogical case of a tree becoming a linked list). `HashMap`s can protect
  against this with DoS-resistant hash functions.
- Use more special-purpose data structures in particular cases. For example,
  maps keyed by IPv4 addresses could be stored in a trie where each level is
  keyed off of a byte of the IPv4 address, leading to a maximum trie depth of 4.
- For any of these solutions, dynamically choose based on working set size or
  other runtime characteristics. For example, we could provide a map type which
  uses a `BTreeMap` below a certain size, and switches to a `HashMap` at larger
  sizes.

### Devices in `HashMap`s

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
even device IDs of other types. This could, in turn, be alleviated by allocating
device IDs in pools (e.g., using the top 32 bits to identify the type of device,
and the bottom 32 bits to identify the device itself).

Another concern is security - is it important that device IDs be unpredictable?
Are we concerned that sequential device IDs might leak information that we don't
want to leak? My feeling is that the answer is no, but it's worth considering.

One way to solve these problems might be to have the device collection be an
enum of a `HashMap` and a `Vec`, and have a cutoff point at which, once the
`Vec` grows sufficiently large, a `HashMap` is used instead.

Another option would be to retain randomly-generated IDs, and simply keep
devices in a `Vec` which is scanned linearly on lookup. For small numbers of
devices, this might still be faster than a `HashMap`.

*NOTE: It [seems likely](https://github.com/rust-lang/rust/pull/56241) that
Rust's `HashMap` will see significant speed improvements in the near future,
which may make the tradeoffs here lean somewhat more in favor of using
`HashMap`s*
