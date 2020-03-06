# FIDL How To: Max out pagination

Author: pascallouis@google.com

_How to best calculate the size (both in terms of bytes and handles) of elements
as they are added to a vector, in order to maximize the number of elements which
can be batched at once, all the while satisfying the kernel caps on channel
writes._

## Summary

To maximize throughput through a channel, it’s common to batch large responses
as multiple vectors of things, for instance by [using a pagination
API][pagination-api]. Since channels are capped at [64K bytes and 64
handles][channel-byte-and-handle-caps], comes the question of how many elements
can be batched in the vector to max out the capacity (and yet, be just under the
byte size and handle count thresholds).

The key reference document for the following is the [FIDL wire
format][fidl-wire-format] specification.

To explain how to best max out pagination, we look at various examples.

## Bluetooth `WatchPeers` method

Consider the [WatchPeers][bts-watch-peers] method of the
`fuchsia.bluetooth.sys.Access` protocol, defined as:

```fidl
WatchPeers() -> (vector<Peer> updated, vector<bt.PeerId> removed);
```

First, a request or response is preceded by a header, i.e. a fixed 16 bytes or
`sizeof(fidl_message_header_t)` as [defined here][fidl-message-header-t].

Each vector has a 16 bytes header `sizeof(fidl_vector_t)`, followed by the
content.

Since `bt.PeerId` is a `struct{uint64}` ([defined here][bt-peer-id]) it is a
fixed 8 bytes, and therefore the `removed` vector’s content is the number of
elements *  8 bytes.

Next, we need to estimate the size of `Peer` which is [defined as a
table][bts-peer]. Tables are essentially a [vector of envelopes][fidl-table-t],
where each envelope then points to the field content. Estimating the size must
be done in two steps: 1. Determine the largest field ordinal used (a.k.a.
`max_ordinal`); 2. Determine the size of each present field.

The size of `Peer` is then the table header -- i.e. `sizeof(fidl_table_t)` --
plus largest ordinal * envelope header -- i.e. `max_ordinal *
sizeof(fidl_envelope_t)` -- plus the total size of the content, that is, each
present field’s content added.

Fields are relatively easy to size, many are primitives or wrappers thereof,
hence result in 8 bytes (due to padding). The `bt.Address` [field][bt-address]
is also 8 bytes since it’s definition reduces to `struct{uint8;
array<uint8>:6}`. The `string` field is a vector of bytes, i.e.
`sizeof(fidl_vector_t) + len(name)`, and padded to the nearest 8 bytes boundary.

## Scenic `Enqueue` method

Consider the [Enqueue][scenic-enqueue] method of the
`fuchsia.scenic.Session` protocol, defined as:

```fidl
Enqueue(vector<Command> cmds);
```

First, a request or response is preceded by a header, i.e. a fixed 16 bytes or
`sizeof(fidl_message_header_t)` as [defined here][fidl-message-header-t]. Then,
vector has a 16 bytes header `sizeof(fidl_vector_t)`, followed by the content
(the commands themselves). As a result, before we account for the size of each
individual command, we have a fixed 32 bytes.

Let's now consider how to size [commmands][scenic-command]. Since a command is
a [union][fidl-wire-format-union] we have a 24 bytes header -- i.e.
`sizeof(fidl_xunion_t)` -- followed by the content which is 8 bytes aligned.

The size of a `Command` union content depends on the variant selected. For this
example, we will choose the `input` variant of type
[`fuchsia.ui.input.Command`][input-command].

The `input` variant (of the scenic command) is itself a union, we therefore have
another 24 bytes header, followed by the content of that union, e.g. a
`send_pointer_input` of type
[`SendPointerInputCmd`][input-send-pointer-input-cmd].

The simplified definition of `SendPointerInputCmd` and all transitively
reachable types through this struct is provided below:

```fidl
struct SendPointerInputCmd {
    uint32 compositor_id;
    PointerEvent pointer_event;
};

struct PointerEvent {
    uint64 event_time;
    uint32 device_id;
    uint32 pointer_id;
    PointerEventType type;
    PointerEventPhase phase;
    float32 x;
    float32 y;
    float32 radius_major;
    float32 radius_minor;
    uint32 buttons;
};

enum PointerEventType {
    // members elided
};

enum PointerEventPhase {
    // members elided
};
```

Both enums `PointerEventType` and `PointerEventPhase`
[default][fidl-language-enums] to an underlying representation of `uint32`. We
can therefore reduce the sizing of `SendPointerInputCmd` to the struct:

```fidl
struct {
    uint32;   // 4 bytes, total 4
              // 4 bytes (padding due to increase in alignment), total 8
    uint64;   // 8 bytes, total 16
    uint32;   // 4 bytes, total 20
    uint32;   // 4 bytes, total 24
    uint32;   // 4 bytes, total 28
    uint32;   // 4 bytes, total 32
    float32;  // 4 bytes, total 36
    float32;  // 4 bytes, total 40
    float32;  // 4 bytes, total 44
    float32;  // 4 bytes, total 48
    uint32;   // 4 bytes, total 52
};
```

Therefore, we see that the size of the `SendPointerInputCmd` struct is 52 bytes.
(For details on struct sizing calculation, a good reference is [The Lost Art of
Structure Packing][lostart].)

Now that we have sized all pieces of one command, we can put things back
together:

* header of `fuchsia.ui.scenic.Command`: 24 bytes, i.e `sizeof(fidl_xunion_t)`
* content with variant `input`:
  * header of `fuchsia.ui.input.Command`: 24 bytes, i.e `sizeof(fidl_xunion_t)`
  * content with variant `set_hard_keyboard_delivery`:
    * struct `SendPointerInputCmd`: 52 bytes
    * padding to align to 8 bytes: 4 bytes

Which results in a total size of 84 bytes.

<!-- xrefs -->
[lostart]: http://www.catb.org/esr/structure-packing/
[pagination-api]: /docs/concepts/api/fidl.md#pagination

[fidl-wire-format]: /docs/development/languages/fidl/reference/wire-format
[fidl-wire-format-union]: /docs/development/languages/fidl/reference/wire-format#unions
[fidl-language-enums]: /docs/development/languages/fidl/reference/language.md#enums

[channel-byte-and-handle-caps]: https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/zircon/system/public/zircon/types.h#296
[fidl-message-header-t]:        https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/zircon/system/public/zircon/fidl.h#358
[fidl-table-t]:                 https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/zircon/system/public/zircon/fidl.h#328
[bt-peer-id]:                   https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth/id.fidl#13
[bt-address]:                   https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth/address.fidl#16
[bts-watch-peers]:              https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth.sys/access.fidl#100
[bts-peer]:                     https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth.sys/peer.fidl#16
[scenic-enqueue]:               https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.ui.scenic/session.fidl#54
[scenic-command]:               https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.ui.scenic/commands.fidl#12
[input-command]:                https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.ui.input/commands.fidl#7
[input-send-pointer-input-cmd]: https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.ui.input/commands.fidl#25
