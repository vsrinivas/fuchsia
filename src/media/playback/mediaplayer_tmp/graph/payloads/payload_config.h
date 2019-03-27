// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_CONFIG_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_CONFIG_H_

namespace media_player {

// Indicates how an input or output wants to access/allocate payload buffers.
enum class PayloadMode {
  kNotConfigured,

  // Payloads are in process virtual memory and are allocated using a
  // |PayloadAllocator|.
  kUsesLocalMemory,

  // Only outputs can use this mode. Payloads are in process virtual memory
  // and are allocated by the output.
  kProvidesLocalMemory,

  // Payloads are in VMOs obtained through |PayloadVmos|.
  kUsesVmos,

  // Payloads are in VMOs provided by the connector via |PayloadVmoProvision|.
  kProvidesVmos
};

// Indicates how buffers should be allocated from VMOs.
enum class VmoAllocation {
  kNotApplicable,

  // There's just one VMO, and all buffers should be allocated from it.
  kSingleVmo,

  // Each buffer occupies its own VMO. Each buffers starts at offset zero in
  // its VMO and can be smaller than the VMO.
  kVmoPerBuffer,

  // Buffers may be allocated from VMOs arbitrarily.
  kUnrestricted
};

// Configuration constraints of an input or output.
//
// The max_xxx fields are used to determine how much memory will be required
// for payloads on the connection. An output must account for the payloads that
// will be held internal to its node as well as payloads queued on the
// connection. An input only needs to account for payloads that will be held
// internal to its node.
//
// The max_xxx fields overlap to some degree. This allows nodes that have only
// partial information to provide enough context for the payload manager to
// configure allocators correctly.
//
// In some cases, the payload manager needs to determine memory requirements
// for the output and input together. In other cases, it needs to determine
// memory requirements for just the output or just the input.
//
// For the combined case, memory requirement calculations look like this:
//
//     max_payload_count = output.max_payload_count + input.max_payload_count
//     max_payload_size = max(output.max_payload_size, input.max_payload_size)
//     max_aggregate_payload_size = max(output.max_aggregate_payload_size +
//                                          input.max_aggregate_payload_size,
//                                      max_payload_count * max_payload_size)
//
// max_aggregate_payload_size is then rounded up to the nearest multiple of
// max_payload_size. For the separate input or output case, the calculations
// look like this:
//
//     max_payload_count = this.max_payload_count
//     max_payload_size = max(this.max_payload_size, other.max_payload_size)
//     max_aggregate_payload_size = max(this.max_aggregate_payload_size,
//                                      max_payload_count * max_payload_size)
//
// Note that in either case, lack of good |max_aggregate_payload_size_| values
// can be compensated for by good |max_payload_count_| and |max_payload_size_|
// values. Here are some examples of how that plays out:
//
// 1) A video renderer input doesn't know max payload size, either individually
//    or in aggregate, but it does know how many payloads it needs to keep
//    around. The decoder output that feeds renderer knows the max payload size
//    and the number of payloads. This information is enough for the payload
//    manager to determine how much memory is needed for the output and input
//    combined and for each individually.
// 2) An audio renderer input doesn't know how big payloads will be, but it
//    knows how much payload it needs to hold in terms of time and therefore in
//    terms of bytes, so it has a good aggregate size. The decoder output that
//    feeds the renderer knows the size of the payloads it will produce and how
//    many it will need to keep around. Again, this is enough information for
//    the required calculations.
// TODO(dalesat): More.
//
struct PayloadConfig {
  // Indicates how the input/output will operate with respect to payload
  // allocation. See comments on |PayloadMode| above.
  PayloadMode mode_ = PayloadMode::kNotConfigured;

  // Indicates the amount of memory in bytes the input/output will require.
  // See the class comment above.
  //
  // When an input/output is used a kExternalX mode, it doesn't provide this
  // value. Outputs using |kProvidesLocalMemory| mode are assumed to be able to
  // allocated an indefinite amount of payload memory. When an input/output
  // uses kProvidesVmos mode, the manager examines the provided VMOs to see if
  // they fulfill the requirements of the connected output/input.
  uint64_t max_aggregate_payload_size_ = 0;

  // Indicates the maximum number of payloads the input/output will required.
  // See the class comment above.
  uint32_t max_payload_count_ = 0;

  // Indicates the maximum size for a payload. Only outputs that aren't using
  // a kExternalX mode provide this value. This value is used to ensure that
  // VMOs allocated for payloads are sufficiently large.
  //
  // When an input uses |kProvidesVmos| mode, the manager examines the provided
  // VMOs to see if they fulfill the requirements of the connected output/input.
  uint64_t max_payload_size_ = 0;

  // Indicates how buffers should or will be allocated from VMOs. For
  // inputs/outputs using a |kExternalVmo| mode, this value indicates how that
  // input/output will allocate buffers. For inputs/output using |kUsesVmos|
  // mode, this value indicates how buffers must be allocated for that
  // input/output.
  //
  // In some cases, incompatible values of |vmo_allocation_| from the input and
  // output in a connection will require that payloads be copied.
  VmoAllocation vmo_allocation_ = VmoAllocation::kNotApplicable;

  // Indicates whether VMOs should or will be physically contiguous. For
  // inputs/outputs using a |kExternalVmo| mode, this value indicates how that
  // input/output will create VMOs. For inputs/output using |kUsesVmos| mode,
  // this value indicates how VMOs must be created for that input/output.
  //
  // In some cases, incompatible values of |physically_contiguous_| from the
  // input and output in a connection will require that payloads be copied.
  bool physically_contiguous_ = false;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_PAYLOADS_PAYLOAD_CONFIG_H_
