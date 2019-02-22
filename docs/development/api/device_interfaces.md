# Fuchsia Device Interface Rubric

The Fuchsia device interfaces are expressed as FIDL protocols.  These FIDL
definitions should conform to the [FIDL Readability Rubric][fidl-readability-rubric].

## Identifiers

Prefer descriptive identifiers.  If you are using domain-specific abbreviations,
document the expansion or provide a reference for further information.

Every identifier that is defined as part of a protocol must be documented with
a comment explaining its interpretation (in the case of fields, types, and
parameters) or behavior (in the case of methods).

## Protocols

All device interface protocols must use the `[Layout = "Simple"]` attribute.  This
restriction exists to allow ease of implementing protocols in any of our
supported languages for driver development.

## Method Statuses

Use a `zx.status` return to represent success and failure.  If a method should not be
able to fail, do not provide a `zx.status` return.  If the method returns multiple
values, the `zx.status` should come first.

## Arrays, Strings, and Vectors

All arrays, strings, and vectors must be of bounded length.  For arbitrarily
selected bounds, prefer to use a `const` identifier as the length so that
protocol consumers can programmatically inspect the length.

## Enums

Prefer enums with explicit sizes (e.g. `enum Foo : uint32 { ... }`) to plain
integer types when a field has a constrained set of non-arithmetic values.

## Bitfields

If your protocol has a bitfield, represent its values using `bits` values
(for details, see [`FTP-025`: "Bit Flags."][ftp-025])

For example:

```fidl
// Bit definitions for Info.features field

bits InfoFeatures : uint32 {
    WLAN = 0x00000001;      // If present, this device represents WLAN hardware
    SYNTH = 0x00000002;     // If present, this device is synthetic (not backed by h/w)
    LOOPBACK = 0x00000004;  // If present, this device receives all messages it sends
};
```

This indicates that the `InfoFeatures` bit field is backed by an unsigned 32-bit
integer, and then goes on to define the three bits that are used.

You can also express the values in binary (as opposed to hex) using the `0b`
notation:

```fidl
bits InfoFeatures : uint32 {
    WLAN =     0b00000001;  // If present, this device represents WLAN hardware
    SYNTH =    0b00000010;  // If present, this device is synthetic (not backed by h/w)
    LOOPBACK = 0b00000100;  // If present, this device receives all messages it sends
};
```

This is the same as the previous example.

## Non-channel based protocols

Some interface protocols may negotiate a non-channel protocol as a performance
optimization (e.g. the zircon.ethernet.Device's GetFifos/SetIOBuffer methods).
FIDL does not currently support expressing these protocols.  For now, represent
any shared data structures with `struct` definitions and provide detailed
documentation about participation in the protocol.  Packed structures are not
currently supported.

[fidl-readability-rubric]: https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/api/fidl.md
[ftp-025]: https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/languages/fidl/reference/ftp/ftp-025.md

