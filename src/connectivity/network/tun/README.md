# Fuchsia Network Tun

The `network-tun` package and the [`fuchsia.net.tun`] FIDL library allow for the
creation of virtual interfaces to be added to the network stack, in a similar
fashion to what `TUN/TAP` interfaces provide on other OSes.

## Setup

You can use `network-tun` from the `sys` realm if you add
`--with //src/connectivity/network/tun:config` to your `fx set` line.

To use it in tests inject it in your test's `cmx` file:

```json
{
   "injected-services": {
      "fuchsia.net.tun.Control": "fuchsia-pkg://fuchsia.com/network-tun#meta/network-tun.cmx"
   }
}
```

## Using fuchsia.net.tun

[`fuchsia.net.tun`] offers two types of backing protocols: a [`Device`] and a
[`DevicePair`]. In other POSIX systems, when using `tunctl` or equivalent to
create a virtual interface, that interface is directly installed into the
kernel's networking stack. In Fuchsia, however, no assumptions are made about
any Netstack capabilities being available, hence a virtual device is always
comprised of two ends, one of which is meant to be installed into the Netstack
and the other end is the equivalent of a `TUN/TAP` handle from other systems.

[`Device`] provides a [`fuchsia.hardware.network/Device`] handle to be offered
to the Netstack and exposes FIDL methods to send and receive frames. It is
easier to use and exposes more fine-tuned control over the interface, but it is
less performant due to operation over FIDL.

[`DevicePair`], in turn, offers a pair of [`fuchsia.hardware.network/Device`]
instead, one of which can be held by the user and the other can be installed in
Netstack. It requires a capable client to [`fuchsia.hardware.network/Device`] to
operate the user end, but it is faster and provides better frame batching
support.

[`fuchsia.net.tun`] does not provide separate interfaces for the differences
between the classical definitions of a `TUN` or `TAP` interface. The difference
between an interface that will operate at the Ethernet or IP layers is achieved
with `FrameType` configurations and informing the Netstack at which layer the
device is operable.

See the [rust example] for a short walkthrough creating POSIX TAP-like or
TUN-like interfaces with [`Device`].

[`fuchsia.net.tun`]: https://fuchsia.dev/reference/fidl/fuchsia.net.tun
[`Device`]: https://fuchsia.dev/reference/fidl/fuchsia.net.tun#Device
[`DevicePair`]: https://fuchsia.dev/reference/fidl/fuchsia.net.tun#DevicePair
[`fuchsia.hardware.network/Device`]: https://fuchsia.dev/reference/fidl/fuchsia.hardware.network#Device
[rust example]: examples/src/lib.rs
