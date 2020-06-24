// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_AUDIO_H_
#define SYSROOT_ZIRCON_DEVICE_AUDIO_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cassert>
#include <cstdio>

// When communicating with an Audio driver using zx_channel_call, do not use
// the AUDIO_INVALID_TRANSACTION_ID as your message's transaction ID.  It is
// reserved for async notifications sent from the driver to the application.
#define AUDIO_INVALID_TRANSACTION_ID ((zx_txid_t)0)

__BEGIN_CDECLS

typedef uint32_t audio_cmd_t;

// Commands sent on the stream channel
#define AUDIO_STREAM_CMD_GET_FORMATS ((audio_cmd_t)0x1000)
#define AUDIO_STREAM_CMD_SET_FORMAT ((audio_cmd_t)0x1001)
#define AUDIO_STREAM_CMD_GET_GAIN ((audio_cmd_t)0x1002)
#define AUDIO_STREAM_CMD_SET_GAIN ((audio_cmd_t)0x1003)
#define AUDIO_STREAM_CMD_PLUG_DETECT ((audio_cmd_t)0x1004)
#define AUDIO_STREAM_CMD_GET_UNIQUE_ID ((audio_cmd_t)0x1005)
#define AUDIO_STREAM_CMD_GET_STRING ((audio_cmd_t)0x1006)
#define AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN ((audio_cmd_t)0x1007)

// Async notifications sent on the stream channel.
#define AUDIO_STREAM_PLUG_DETECT_NOTIFY ((audio_cmd_t)0x2000)

// Commands sent on the ring buffer channel
#define AUDIO_RB_CMD_GET_FIFO_DEPTH ((audio_cmd_t)0x3000)
#define AUDIO_RB_CMD_GET_BUFFER ((audio_cmd_t)0x3001)
#define AUDIO_RB_CMD_START ((audio_cmd_t)0x3002)
#define AUDIO_RB_CMD_STOP ((audio_cmd_t)0x3003)

// Async notifications sent on the ring buffer channel.
#define AUDIO_RB_POSITION_NOTIFY ((audio_cmd_t)0x4000)

// Flags used to modify commands.
// The NO_ACK flag can be used with the SET_GAIN and PLUG_DETECT commands.
#define AUDIO_FLAG_NO_ACK ((audio_cmd_t)0x80000000)

typedef struct audio_cmd_hdr {
  zx_txid_t transaction_id;
  audio_cmd_t cmd;
} audio_cmd_hdr_t;

static_assert(sizeof(audio_cmd_hdr_t) == 8,
              "audio_cmd_hdr_t should be 8 bytes! "
              "If sizeof(zx_txid_t has changed from 4 to 8, "
              "consider repacking the structs in audio.h");

// audio_sample_format_t
//
// Bitfield which describes audio sample format as they reside in memory.
//
typedef uint32_t audio_sample_format_t;
#define AUDIO_SAMPLE_FORMAT_BITSTREAM ((audio_sample_format_t)(1u << 0))
#define AUDIO_SAMPLE_FORMAT_8BIT ((audio_sample_format_t)(1u << 1))
#define AUDIO_SAMPLE_FORMAT_16BIT ((audio_sample_format_t)(1u << 2))
#define AUDIO_SAMPLE_FORMAT_20BIT_PACKED ((audio_sample_format_t)(1u << 4))
#define AUDIO_SAMPLE_FORMAT_24BIT_PACKED ((audio_sample_format_t)(1u << 5))
#define AUDIO_SAMPLE_FORMAT_20BIT_IN32 ((audio_sample_format_t)(1u << 6))
#define AUDIO_SAMPLE_FORMAT_24BIT_IN32 ((audio_sample_format_t)(1u << 7))
#define AUDIO_SAMPLE_FORMAT_32BIT ((audio_sample_format_t)(1u << 8))
#define AUDIO_SAMPLE_FORMAT_32BIT_FLOAT ((audio_sample_format_t)(1u << 9))
#define AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED ((audio_sample_format_t)(1u << 30))
#define AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN ((audio_sample_format_t)(1u << 31))
#define AUDIO_SAMPLE_FORMAT_FLAG_MASK                          \
  ((audio_sample_format_t)(AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED | \
                           AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN))

// audio_stream_format_range_t
//
// A structure used along with the AUDIO_STREAM_CMD_GET_FORMATS command in order
// to describe the formats supported by an audio stream.
#define ASF_RANGE_FLAG_FPS_CONTINUOUS ((uint16_t)(1u << 0))
#define ASF_RANGE_FLAG_FPS_48000_FAMILY ((uint16_t)(1u << 1))
#define ASF_RANGE_FLAG_FPS_44100_FAMILY ((uint16_t)(1u << 2))
typedef struct audio_stream_format_range {
  audio_sample_format_t sample_formats;
  uint32_t min_frames_per_second;
  uint32_t max_frames_per_second;
  uint8_t min_channels;
  uint8_t max_channels;
  uint16_t flags;
} __PACKED audio_stream_format_range_t;

static_assert(sizeof(audio_stream_format_range_t) == 16,
              "audio_stream_format_range_t should be 16 bytes!");

// audio_set_gain_flags_t
//
// Flags used by the AUDIO_STREAM_CMD_SET_GAIN message.
//
typedef uint32_t audio_set_gain_flags_t;
#define AUDIO_SGF_MUTE_VALID \
  ((audio_set_gain_flags_t)0x1)  // Whether or not the mute flag is valid.
#define AUDIO_SGF_AGC_VALID ((audio_set_gain_flags_t)0x2)  // Whether or not the agc flag is valid.
#define AUDIO_SGF_GAIN_VALID \
  ((audio_set_gain_flags_t)0x4)  // Whether or not the gain float is valid.
#define AUDIO_SGF_MUTE ((audio_set_gain_flags_t)0x40000000)  // Whether or not to mute the stream.
#define AUDIO_SGF_AGC \
  ((audio_set_gain_flags_t)0x80000000)  // Whether or not enable AGC for the stream.

// audio_pd_flags_t
//
// Flags used by AUDIO_STREAM_CMD_PLUG_DETECT commands to enable or disable
// asynchronous plug detect notifications.
//
typedef uint32_t audio_pd_flags_t;
#define AUDIO_PDF_NONE ((audio_pd_flags_t)0)
#define AUDIO_PDF_ENABLE_NOTIFICATIONS ((audio_pd_flags_t)0x40000000)
#define AUDIO_PDF_DISABLE_NOTIFICATIONS ((audio_pd_flags_t)0x80000000)

// audio_pd_notify_flags_t
//
// Flags used by responses to the AUDIO_STREAM_CMD_PLUG_DETECT
// message, and by AUDIO_STREAM_PLUG_DETECT_NOTIFY messages.
//
typedef uint32_t audio_pd_notify_flags_t;
#define AUDIO_PDNF_HARDWIRED \
  ((audio_pd_notify_flags_t)0x1)  // Stream is hardwired (will always be plugged in)
#define AUDIO_PDNF_CAN_NOTIFY \
  ((audio_pd_notify_flags_t)0x2)  // Stream is able to notify of plug state changes.
#define AUDIO_PDNF_PLUGGED ((audio_pd_notify_flags_t)0x80000000)  // Stream is currently plugged in.

// AUDIO_STREAM_CMD_GET_FORMATS
//
// Must not be used with the NO_ACK flag.
#define AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE (15u)
typedef struct audio_stream_cmd_get_formats_req {
  audio_cmd_hdr_t hdr;
} audio_stream_cmd_get_formats_req_t;

// TODO(johngro) : Figure out if zx_txid_t is ever going to go up to 8 bytes or
// not.  If it is, just remove the _pad field below.  If not, either keep it as
// a _pad field, or repurpose it for some flags of some form.  Right now, we use
// it to make sure that format_ranges is aligned to a 16 byte boundary.
typedef struct audio_stream_cmd_get_formats_resp {
  audio_cmd_hdr_t hdr;
  uint32_t _pad;
  uint16_t format_range_count;
  uint16_t first_format_range_ndx;
  audio_stream_format_range_t format_ranges[AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE];
} audio_stream_cmd_get_formats_resp_t;

static_assert(sizeof(audio_stream_cmd_get_formats_resp_t) == 256,
              "audio_stream_cmd_get_formats_resp_t must be 256 bytes");

#define AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED ((uint64_t)0)

// AUDIO_STREAM_CMD_SET_FORMAT
//
// Must not be used with the NO_ACK flag.
typedef struct audio_stream_cmd_set_format_req {
  audio_cmd_hdr_t hdr;
  uint32_t frames_per_second;
  audio_sample_format_t sample_format;
  uint16_t channels;
  uint64_t channels_to_use_bitmask;
} audio_stream_cmd_set_format_req_t;

typedef struct audio_stream_cmd_set_format_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;
  uint64_t external_delay_nsec;

  // Note: Upon success, a channel used to control the audio buffer will also
  // be returned.
} audio_stream_cmd_set_format_resp_t;

// AUDIO_STREAM_CMD_GET_GAIN
//
// Request that a gain notification be sent with the current details of the
// streams current gain settings as well as gain setting capabilities.
//
// Must not be used with the NO_ACK flag.
typedef struct audio_stream_cmd_get_gain_req {
  audio_cmd_hdr_t hdr;
} audio_stream_cmd_get_gain_req_t;

typedef struct audio_stream_cmd_get_gain_resp {
  // TODO(johngro) : Is there value in exposing the gain step to the level
  // above the lowest level stream interface, or should we have all drivers
  // behave as if they have continuous control at all times?
  audio_cmd_hdr_t hdr;

  bool cur_mute;   // True if the stream is currently muted.
  bool cur_agc;    // True if the stream has AGC currently enabled.
  float cur_gain;  // The current setting gain of the stream in dB

  bool can_mute;    // True if the stream is capable of muting
  bool can_agc;     // True if the stream has support for AGC
  float min_gain;   // The minimum valid gain setting, in dB
  float max_gain;   // The maximum valid gain setting, in dB
  float gain_step;  // The smallest valid gain increment, counted from the minimum gain.
} audio_stream_cmd_get_gain_resp_t;

// AUDIO_STREAM_CMD_SET_GAIN
//
// Request that a stream change its gain settings to most closely match those
// requested.  Gain values for Valid requests will be rounded to the nearest
// gain step.  For example, if a stream can control its gain on the range from
// -60.0 to 0.0 dB, a request to set the gain to -33.3 dB will result in a gain
// of -33.5 being applied.
//
// Gain change requests outside of the capabilities of the stream's
// amplifier will be rejected with a result of ZX_ERR_INVALID_ARGS.  Using the
// previous example, requests for gains of -65.0 or +3dB would be rejected.
// Similarly,  If an amplifier is capable of gain control but cannot mute, a
// request to mute will be rejected.
//
// TODO(johngro) : Is this the correct behavior?  Should we just apply sensible
// limits instead?  IOW - If the user requests a gain of -1000 dB, should we
// just set the gain to -60dB?  Likewise, if they request mute but the amplifier
// has no hard mute feature, should we just set the gain to the minimum
// permitted gain?
//
// May be used with the NO_ACK flag.
typedef struct audio_stream_cmd_set_gain_req {
  audio_cmd_hdr_t hdr;
  audio_set_gain_flags_t flags;
  float gain;
} audio_stream_cmd_set_gain_req_t;

typedef struct audio_stream_cmd_set_gain_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;
  // The current gain settings observed immediately after processing the set
  // gain request.
  bool cur_mute;
  bool cur_agc;
  float cur_gain;
} audio_stream_cmd_set_gain_resp_t;

// AUDIO_STREAM_CMD_PLUG_DETECT
//
// Trigger a plug detect operation and/or enable/disable asynchronous plug
// detect notifications.
//
// May be used with the NO_ACK flag.
typedef struct audio_stream_cmd_plug_detect_req {
  audio_cmd_hdr_t hdr;
  audio_pd_flags_t flags;  // Options used to enable or disable notifications
} audio_stream_cmd_plug_detect_req_t;

typedef struct audio_stream_cmd_plug_detect_resp {
  audio_cmd_hdr_t hdr;
  audio_pd_notify_flags_t flags;  // The current plug state and capabilities
  zx_time_t plug_state_time;      // The time of the plug state last change.
} audio_stream_cmd_plug_detect_resp_t;

// AUDIO_STREAM_PLUG_DETECT_NOTIFY
//
// Message asynchronously in response to a plug state change to clients who have
// registered for plug state notifications.
//
// Note: Solicited and unsolicited plug detect messages currently use the same
// structure and contain the same information.  The difference between the two
// is that Solicited messages, use AUDIO_STREAM_CMD_PLUG_DETECT as the value of
// the `cmd` field of their header and the transaction ID of the request sent by
// the client.  Unsolicited messages use AUDIO_STREAM_PLUG_DETECT_NOTIFY as the
// value value of the `cmd` field of their header, and
// AUDIO_INVALID_TRANSACTION_ID for their transaction ID.
typedef audio_stream_cmd_plug_detect_resp_t audio_stream_plug_detect_notify_t;

// AUDIO_STREAM_CMD_GET_UNIQUE_ID
//
// Fetch a globally unique, but persistent ID for the stream.
//
// Drivers should make every effort to return as unique an identifier as
// possible for each stream that they publish.  This ID must not change between
// boots.  When available, using a globally unique device serial number is
// strongly encouraged.  Other possible sources of unique-ness include a
// driver's physical connection path, driver binding information, manufacturer
// calibration data, and so on.
//
// Note: a small number of hardcoded unique ID has been provided for built-in
// devices.  Platform drivers for systems with hardwired audio devices may use
// these unique IDs as appropriate to signal which audio streams represent the
// built-in devices for the system.  Drivers for hot-pluggable audio devices
// should *never* use these identifiers.
//
// Even given this, higher level code should *not* depend on these identifiers
// being perfectly unique, and should be prepared to take steps to de-dupe
// identifiers when needed.
typedef struct audio_stream_cmd_get_unique_id_req {
  audio_cmd_hdr_t hdr;
} audio_stream_cmd_get_unique_id_req_t;

typedef struct audio_stream_unique_id {
  uint8_t data[16];
} audio_stream_unique_id_t;

#define AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS \
  {                                             \
    .data = { 0x01, 0x00 }                      \
  }
#define AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK \
  {                                                   \
    .data = { 0x02, 0x00 }                            \
  }
#define AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE \
  {                                               \
    .data = { 0x03, 0x00 }                        \
  }
#define AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADSET_JACK \
  {                                                 \
    .data = { 0x04, 0x00 }                          \
  }
#define AUDIO_STREAM_UNIQUE_ID_BUILTIN_BT \
  {                                       \
    .data = { 0x05, 0x00 }                \
  }

typedef struct audio_stream_cmd_get_unique_id_resp {
  audio_cmd_hdr_t hdr;
  audio_stream_unique_id_t unique_id;
} audio_stream_cmd_get_unique_id_resp_t;

// AUDIO_STREAM_CMD_GET_STRING
//
// Fetch the specified string from a device's static string table.  Strings
// returned by the device driver...
//
// ++ Must be encoded using UTF8
// ++ May contain embedded NULLs
// ++ May not be NULL terminated
//
// Drivers are encouraged to NULL terminate all of their strings whenever
// possible, but are not required to do so if the response buffer is too small.
//
typedef uint32_t audio_stream_string_id_t;
#define AUDIO_STREAM_STR_ID_MANUFACTURER ((audio_stream_string_id_t)0x80000000)
#define AUDIO_STREAM_STR_ID_PRODUCT ((audio_stream_string_id_t)0x80000001)

typedef struct audio_stream_cmd_get_string_req {
  audio_cmd_hdr_t hdr;
  audio_stream_string_id_t id;
} audio_stream_cmd_get_string_req_t;

typedef struct audio_stream_cmd_get_string_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;
  audio_stream_string_id_t id;
  uint32_t strlen;
  uint8_t str[256 - sizeof(audio_cmd_hdr_t) - (3 * sizeof(uint32_t))];
} audio_stream_cmd_get_string_resp_t;

static_assert(sizeof(audio_stream_cmd_get_string_resp_t) == 256,
              "audio_stream_cmd_get_string_resp_t must be exactly 256 bytes");

// AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN
//
// Fetch the hardware clock domain for this device.
// On products containing audio devices that are not locked to the local system clock, the board
// driver will provide a clock tree entry to the audio driver at driver startup time. From that,
// the audio driver can extract the clock domain and provide it to the sender, upon receiving this
// command. This domain value is all that the sender needs, in order to locate controls for that
// clock domain in the clock tree and trim that clock domain's rate.
// On products containing audio devices that are locked to the local system monotonic clock, a clock
// domain value of 0 should be returned.
//
// Must not be used with the NO_ACK flag.
typedef struct audio_stream_cmd_get_clock_domain_req {
  audio_cmd_hdr_t hdr;
} audio_stream_cmd_get_clock_domain_req_t;

typedef struct audio_stream_cmd_get_clock_domain_resp {
  audio_cmd_hdr_t hdr;
  int32_t clock_domain;
} audio_stream_cmd_get_clock_domain_resp_t;

//
// Ring-buffer commands
//

// AUDIO_RB_CMD_GET_FIFO_DEPTH
//
// TODO(johngro) : Is calling this "FIFO" depth appropriate?  Should it be some
// direction neutral form of something like "max-read-ahead-amount" or something
// instead?
//
// Must not be used with the NO_ACK flag.
typedef struct audio_rb_cmd_get_fifo_depth_req {
  audio_cmd_hdr_t hdr;
} audio_rb_cmd_get_fifo_depth_req_t;

typedef struct audio_rb_cmd_get_fifo_depth_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;

  // A representation (in bytes) of how far ahead audio hardware may read
  // into the stream (in the case of output) or may hold onto audio before
  // writing it to memory (in the case of input).
  uint32_t fifo_depth;
} audio_rb_cmd_get_fifo_depth_resp_t;

// AUDIO_RB_CMD_GET_BUFFER
typedef struct audio_rb_cmd_get_buffer_req {
  audio_cmd_hdr_t hdr;

  uint32_t min_ring_buffer_frames;
  uint32_t notifications_per_ring;
} audio_rb_cmd_get_buffer_req_t;

typedef struct audio_rb_cmd_get_buffer_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;
  uint32_t num_ring_buffer_frames;

  // NOTE: If result == ZX_OK, a VMO handle representing the ring buffer to
  // be used will be returned as well.  Clients may map this buffer with
  // read-write permissions in the case of an output stream, or read-only
  // permissions in the case of an input stream.  The size of the VMO
  // indicates where the wrap point of the ring (in bytes) is located in the
  // VMO.  This size *must* always be an integral number of audio frames.
  //
  // TODO(johngro) : Should we provide some indication of whether or not this
  // memory is being used directly for HW DMA and may need explicit cache
  // flushing/invalidation?
} audio_rb_cmd_get_buffer_resp_t;

// AUDIO_RB_CMD_START
typedef struct audio_rb_cmd_start_req {
  audio_cmd_hdr_t hdr;
} audio_rb_cmd_start_req_t;

typedef struct audio_rb_cmd_start_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;
  uint64_t start_time;
} audio_rb_cmd_start_resp_t;

// AUDIO_RB_CMD_STOP
typedef struct audio_rb_cmd_stop_req {
  audio_cmd_hdr_t hdr;
} audio_rb_cmd_stop_req_t;

typedef struct audio_rb_cmd_stop_resp {
  audio_cmd_hdr_t hdr;
  zx_status_t result;
} audio_rb_cmd_stop_resp_t;

// AUDIO_RB_POSITION_NOTIFY
typedef struct audio_rb_position_notify {
  audio_cmd_hdr_t hdr;

  // The time, per system monotonic clock, of the below byte position.
  zx_time_t monotonic_time;

  // The current position (in bytes) of the driver/hardware's read (output) or
  // write (input) pointer in the ring buffer.
  uint32_t ring_buffer_pos;
} audio_rb_position_notify_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_DEVICE_AUDIO_H_
