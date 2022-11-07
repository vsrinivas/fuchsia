# async-broadcast

[![Build](https://github.com/smol-rs/async-broadcast/workflows/Build%20and%20test/badge.svg)](
https://github.com/smol-rs/async-broadcast/actions)
[![License](https://img.shields.io/badge/license-Apache--2.0_OR_MIT-blue.svg)](
https://github.com/smol-rs/async-broadcast)
[![Cargo](https://img.shields.io/crates/v/async-broadcast.svg)](
https://crates.io/crates/async-broadcast)
[![Documentation](https://docs.rs/async-broadcast/badge.svg)](
https://docs.rs/async-broadcast)

An async multi-producer multi-consumer broadcast channel, where each consumer gets a clone of every
message sent on the channel. For obvious reasons, the channel can only be used to broadcast types
that implement `Clone`.

A channel has the `Sender` and `Receiver` side. Both sides are cloneable and can be shared
among multiple threads.

When all `Sender`s or all `Receiver`s are dropped, the channel becomes closed. When a channel is
closed, no more messages can be sent, but remaining messages can still be received.

The channel can also be closed manually by calling `Sender::close()` or
`Receiver::close()`.

## Examples

```rust
use async_broadcast::{broadcast, TryRecvError};
use futures_lite::{future::block_on, stream::StreamExt};

block_on(async move {
    let (s1, mut r1) = broadcast(2);
    let s2 = s1.clone();
    let mut r2 = r1.clone();

    // Send 2 messages from two different senders.
    s1.broadcast(7).await.unwrap();
    s2.broadcast(8).await.unwrap();

    // Channel is now at capacity so sending more messages will result in an error.
    assert!(s2.try_broadcast(9).unwrap_err().is_full());
    assert!(s1.try_broadcast(10).unwrap_err().is_full());

    // We can use `recv` method of the `Stream` implementation to receive messages.
    assert_eq!(r1.next().await.unwrap(), 7);
    assert_eq!(r1.recv().await.unwrap(), 8);
    assert_eq!(r2.next().await.unwrap(), 7);
    assert_eq!(r2.recv().await.unwrap(), 8);

    // All receiver got all messages so channel is now empty.
    assert_eq!(r1.try_recv(), Err(TryRecvError::Empty));
    assert_eq!(r2.try_recv(), Err(TryRecvError::Empty));

    // Drop both senders, which closes the channel.
    drop(s1);
    drop(s2);

    assert_eq!(r1.try_recv(), Err(TryRecvError::Closed));
    assert_eq!(r2.try_recv(), Err(TryRecvError::Closed));
})
```

## Safety
This crate uses ``#![deny(unsafe_code)]`` to ensure everything is implemented in
100% Safe Rust.

## Contributing
Want to join us? Check out our ["Contributing" guide][contributing] and take a
look at some of these issues:

- [Issues labeled "good first issue"][good-first-issue]
- [Issues labeled "help wanted"][help-wanted]

[contributing]: https://github.com/smol-rs/async-broadcast/blob/master/.github/CONTRIBUTING.md
[good-first-issue]: https://github.com/smol-rs/async-broadcast/labels/good%20first%20issue
[help-wanted]: https://github.com/smol-rs/async-broadcast/labels/help%20wanted

## License

<sup>
Licensed under either of <a href="LICENSE-APACHE">Apache License, Version
2.0</a> or <a href="LICENSE-MIT">MIT license</a> at your option.
</sup>

<br/>

<sub>
Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in this crate by you, as defined in the Apache-2.0 license, shall
be dual licensed as above, without any additional terms or conditions.
</sub>
