# FIDL How To: Max out pagination

Author: pascallouis@google.com

To maximize throughput through a channel, it’s common to batch large responses
as multiple vectors of things, for instance by [using a pagination
API][pagination-api]. Since channels are capped at [64K bytes and 64
handles][channel-byte-and-handle-caps], comes the question of how many elements
can be batched in the vector to max out the capacity (and yet, be just under the
byte size and handle count thresholds).

To explain how to best max out pagination, we will look at the
[WatchPeers][watch-peers-method] method of the `fuchsia.bluetooth.sys.Access
protocol`. The method is defined as:

```fidl
WatchPeers() -> (vector<Peer> updated, vector<bt.PeerId> removed);
```

The key reference document for the following is the [FIDL wire
format][fidl-wire-format] specification.

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

<!-- xrefs -->
[pagination-api]: /docs/concepts/api/fidl.md#pagination
[fidl-wire-format]: /docs/development/languages/fidl/reference/wire-format

[channel-byte-and-handle-caps]: https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/zircon/system/public/zircon/types.h#296
[watch-peers-method]:           https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth.sys/access.fidl#100
[fidl-message-header-t]:        https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/zircon/system/public/zircon/fidl.h#358
[fidl-table-t]:                 https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/zircon/system/public/zircon/fidl.h#328
[bt-peer-id]:                   https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth/id.fidl#13
[bts-peer]:                     https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth.sys/peer.fidl#16
[bt-address]:                   https://fuchsia.googlesource.com/fuchsia/+/b7840e772fccb93be4fff73a9cb83f978095eac2/sdk/fidl/fuchsia.bluetooth/address.fidl#16
