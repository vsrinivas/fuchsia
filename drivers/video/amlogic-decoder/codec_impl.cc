// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_impl.h"

#include "device_ctx.h"

#include <fbl/auto_call.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/debug/debugger.h>
#include <lib/fxl/logging.h>

#include <threads.h>

// The VLOGF() and LOGF() macros are here because we want the calls sites to
// look like FX_VLOGF and FX_LOGF, but without hard-wiring to those.  For now,
// printf() seems to work fine.

#define VLOG_ENABLED 0

#if (VLOG_ENABLED)
#define VLOGF(...) printf(__VA_ARGS__)
#else
#define VLOGF(...) \
  do {             \
  } while (0)
#endif

#define LOGF(...) printf(__VA_ARGS__)

namespace {

// The protocol does not permit an unbounded number of in-flight streams, as
// that would potentially result in unbounded data queued in the incoming
// channel with no valid circuit-breaker value for the incoming channel data.
constexpr size_t kMaxInFlightStreams = 10;

constexpr uint64_t kInputBufferConstraintsVersionOrdinal = 1;
constexpr uint64_t kInputDefaultBufferConstraintsVersionOrdinal =
    kInputBufferConstraintsVersionOrdinal;

// TODO(dustingreen): Make these defaults/settings overridable per CodecAdapter
// implementation.  For a few of them, maybe require the CodecAdapter to
// specify (as in no default for some of them).

constexpr uint32_t kInputPacketCountForCodecMin = 2;
// This is fairly arbitrary, but roughly speaking, 1 to be decoding, 1 to be in
// flight from the client, 1 to be in flight back to the client.  We may want
// to adjust this upward if we find it's needed to keep the HW busy when there's
// any backlog.
constexpr uint32_t kInputPacketCountForCodecRecommended = 3;
constexpr uint32_t kInputPacketCountForCodecRecommendedMax = 16;
constexpr uint32_t kInputPacketCountForCodecMax = 64;

constexpr uint32_t kInputDefaultPacketCountForCodec =
    kInputPacketCountForCodecRecommended;

constexpr uint32_t kInputPacketCountForClientMax =
    std::numeric_limits<uint32_t>::max();
// This is fairly arbitrary, but rough speaking, 1 to be filling, 1 to be in
// flight toward the codec, and 1 to be in flight from the codec.  This doesn't
// intend to be large enough to ride out any hypothetical decoder performance
// variability vs. needed decode rate.
constexpr uint32_t kInputDefaultPacketCountForClient = 3;

// TODO(dustingreen): Implement and permit single-buffer mode.  (The default
// will probably remain buffer per packet mode though.)
constexpr bool kInputSingleBufferModeAllowed = false;
constexpr bool kInputDefaultSingleBufferMode = false;

// A client using the min shouldn't necessarily expect performance to be
// acceptable when running higher bit-rates.
constexpr uint32_t kInputPerPacketBufferBytesMin = 8 * 1024;
// This is fairly arbitrary, but roughly speaking, ~266 KiB for an average frame
// at 50 Mbps for 4k video, rounded up to 512 KiB buffer space per packet to
// allow most but not all frames to fit in one packet.  It could be equally
// reasonable to say the average-size compressed from should barely fit in one
// packet's buffer space, or the average-size compressed frame should split to
// ~1.5 packets, but we don't want an excessive number of packets required per
// frame (not even for I frames).
constexpr uint32_t kInputPerPacketBufferBytesRecommended = 512 * 1024;
// This is an arbitrary cap for now.  The only reason it's larger than
// recommended is to allow some room to profile whether larger buffer space per
// packet might be useful for performance.
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;

constexpr uint32_t kInputDefaultPerPacketBufferBytes =
    kInputPerPacketBufferBytesRecommended;

class ScopedUnlock {
 public:
  explicit ScopedUnlock(std::unique_lock<std::mutex>& unique_lock)
      : unique_lock_(unique_lock) {
    unique_lock_.unlock();
  }
  ~ScopedUnlock() { unique_lock_.lock(); }

 private:
  std::unique_lock<std::mutex>& unique_lock_;
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(ScopedUnlock);
};

// Used within ScopedUnlock only.  Normally we'd just leave a std::unique_lock
// locked until it's destructed.
class ScopedRelock {
 public:
  explicit ScopedRelock(std::unique_lock<std::mutex>& unique_lock)
      : unique_lock_(unique_lock) {
    unique_lock_.lock();
  }
  ~ScopedRelock() { unique_lock_.unlock(); }

 private:
  std::unique_lock<std::mutex>& unique_lock_;
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(ScopedRelock);
};

uint32_t PacketCountFromPortSettings(
    const fuchsia::mediacodec::CodecPortBufferSettings& settings) {
  return settings.packet_count_for_codec + settings.packet_count_for_client;
}

uint32_t BufferCountFromPortSettings(
    const fuchsia::mediacodec::CodecPortBufferSettings& settings) {
  if (settings.single_buffer_mode) {
    return 1;
  }
  return PacketCountFromPortSettings(settings);
}

}  // namespace

CodecImpl::CodecImpl(
    std::unique_ptr<CodecAdmission> codec_admission, DeviceCtx* device,
    std::unique_ptr<fuchsia::mediacodec::CreateDecoder_Params> decoder_params,
    fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request)
    // The parameters to CodecAdapter constructor here aren't important.
    : CodecAdapter(lock_, this),
      codec_admission_(std::move(codec_admission)),
      device_(device),
      // TODO(dustingreen): Maybe have another parameter for encoder params, or
      // maybe separate constructor.
      decoder_params_(std::move(decoder_params)),
      tmp_interface_request_(std::move(codec_request)),
      binding_(this),
      stream_control_loop_(&kAsyncLoopConfigNoAttachToThread) {
  // For now, decoder_params is required.
  //
  // TODO(dustingreen): Make decoder_params || encoder_params required.
  FXL_DCHECK(decoder_params_);
  FXL_DCHECK(tmp_interface_request_);
  // This is the binding_'s error handler, not the owner_error_handler_ which
  // is related but separate.
  binding_.set_error_handler(fit::bind_member(this, &CodecImpl::Unbind));
  initial_input_format_details_ = &decoder_params_->input_details;
}

CodecImpl::~CodecImpl() {
  // We need ~binding_ to run on fidl_thread() else it's not safe to
  // un-bind unilaterally.  Unless not ever bound in the first place.
  FXL_DCHECK(thrd_current() == fidl_thread());

  FXL_DCHECK(was_unbind_started_ && was_unbind_completed_ ||
             !was_logically_bound_);

  // Ensure the CodecAdmission is deleted entirely after ~this, including after
  // any relevant base class destructors have run.
  device_->driver()->PostToSharedFidl(
      [codec_admission = std::move(codec_admission_)] {
        // Nothing else to do here.
        //
        // ~codec_admission
      });
}

std::mutex& CodecImpl::lock() { return lock_; }

void CodecImpl::SetCoreCodecAdapter(
    std::unique_ptr<CodecAdapter> codec_adapter) {
  FXL_DCHECK(!codec_adapter_);
  codec_adapter_ = std::move(codec_adapter);
}

void CodecImpl::BindAsync(fit::closure error_handler) {
  // While it would potentially be safe to call Bind() from a thread other than
  // fidl_thread(), we have no reason to permit that.
  FXL_DCHECK(thrd_current() == fidl_thread());
  // Up to once only.  No re-use.
  FXL_DCHECK(!was_bind_async_called_);
  FXL_DCHECK(!binding_.is_bound());
  FXL_DCHECK(tmp_interface_request_);
  was_bind_async_called_ = true;

  zx_status_t start_thread_result = stream_control_loop_.StartThread(
      "StreamControl_loop", &stream_control_thread_);
  if (start_thread_result != ZX_OK) {
    // Handle the error async, to be consistent with later errors that must
    // occur async anyway.  Inability to start StreamControl is tne only case
    // where we just allow the owner to "delete this" without using
    // UnbindLocked(), since UnbindLocked() relies on StreamControl.
    PostToSharedFidl(std::move(error_handler));
    return;
  }

  // From here on, we'll only fail the CodecImpl via UnbindLocked().
  was_logically_bound_ = true;

  // This doesn't really need to be set until the start of the posted lambda
  // below, but here is also fine.
  owner_error_handler_ = std::move(error_handler);

  // Do most of the bind work on StreamControl async, since CoreCodecInit()
  // might potentially take a little while longer than makes sense to run on
  // fidl_thread().  Potential examples: if CoreCodecInit() ends up
  // essentially evicting some other CodecImpl, or if setting up HW can take a
  // while, or if getting a scheduling slot on decode HW can require some
  // waiting, or similar.
  PostToStreamControl([this] {
    // This is allowed to take a little while if necessary, using the current
    // StreamControl thread, which is not shared with any other CodecImpl.
    CoreCodecInit(*initial_input_format_details_);
    is_core_codec_init_called_ = true;

    // We touch FIDL stuff only from the fidl_thread().  While it would
    // be more efficient to post once to bind and send up to two messages below,
    // by posting individually we can share more code and have simpler rules for
    // calling that code.

    // Once this is posted, we can be dispatching incoming FIDL messages,
    // concurrent with the rest of the current lambda.  Aside from Sync(), most
    // of that dispatching would tend to land in FailLocked().  The concurrency
    // is just worth keeping in mind for the rest of the current lambda is all.
    PostToSharedFidl([this] {
      zx_status_t bind_result =
          binding_.Bind(std::move(tmp_interface_request_),
                        device_->driver()->shared_fidl_loop()->dispatcher());
      if (bind_result != ZX_OK) {
        Fail("binding_.Bind() failed");
        return;
      }
      FXL_DCHECK(!tmp_interface_request_);
    });

    input_constraints_ =
        std::make_unique<fuchsia::mediacodec::CodecBufferConstraints>(
            fuchsia::mediacodec::CodecBufferConstraints{
                .buffer_constraints_version_ordinal =
                    kInputBufferConstraintsVersionOrdinal,
                // This is not really a suggestion; actual values must be odd,
                // and the client should be the source of this value.
                .default_settings.buffer_lifetime_ordinal = 0,
                .default_settings.buffer_constraints_version_ordinal =
                    kInputDefaultBufferConstraintsVersionOrdinal,
                .default_settings.packet_count_for_codec =
                    kInputDefaultPacketCountForCodec,
                .default_settings.packet_count_for_client =
                    kInputDefaultPacketCountForClient,
                .default_settings.per_packet_buffer_bytes =
                    kInputDefaultPerPacketBufferBytes,
                .default_settings.single_buffer_mode =
                    kInputDefaultSingleBufferMode,
                .per_packet_buffer_bytes_min = kInputPerPacketBufferBytesMin,
                .per_packet_buffer_bytes_recommended =
                    kInputPerPacketBufferBytesRecommended,
                .per_packet_buffer_bytes_max = kInputPerPacketBufferBytesMax,
                .packet_count_for_codec_min = kInputPacketCountForCodecMin,
                .packet_count_for_codec_recommended =
                    kInputPacketCountForCodecRecommended,
                .packet_count_for_codec_recommended_max =
                    kInputPacketCountForCodecRecommendedMax,
                .packet_count_for_codec_max = kInputPacketCountForCodecMax,
                .packet_count_for_client_max = kInputPacketCountForClientMax,
                .single_buffer_mode_allowed = kInputSingleBufferModeAllowed,
            });

    // If/when this sends OnOutputConfig(), it posts to do so.
    onInputConstraintsReady();

    sent_buffer_constraints_version_ordinal_[kInputPort] =
        kInputBufferConstraintsVersionOrdinal;
    PostToSharedFidl([this] {
      binding_.events().OnInputConstraints(fidl::Clone(*input_constraints_));
    });
  });
}

void CodecImpl::EnableOnStreamFailed() {
  FXL_DCHECK(thrd_current() == fidl_thread());
  is_on_stream_failed_enabled_ = true;
}

void CodecImpl::SetInputBufferSettings(
    fuchsia::mediacodec::CodecPortBufferSettings input_settings) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  PostToStreamControl([this, input_settings = std::move(input_settings)] {
    SetInputBufferSettings_StreamControl(std::move(input_settings));
  });
}

void CodecImpl::SetInputBufferSettings_StreamControl(
    fuchsia::mediacodec::CodecPortBufferSettings input_settings) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }
    if (IsStreamActiveLocked()) {
      Fail("client sent SetInputBufferSettings() with stream active");
      return;
    }
    SetBufferSettingsCommon(lock, kInputPort, input_settings,
                            *input_constraints_);
  }  // ~lock
}

void CodecImpl::AddInputBuffer(fuchsia::mediacodec::CodecBuffer buffer) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  PostToStreamControl([this, buffer = std::move(buffer)]() mutable {
    AddInputBuffer_StreamControl(std::move(buffer));
  });
}

void CodecImpl::AddInputBuffer_StreamControl(
    fuchsia::mediacodec::CodecBuffer buffer) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  if (IsStopping()) {
    return;
  }
  // We must check, because __MUST_CHECK_RETURN, and it's worth it for the
  // enforcement and consistency.
  if (!AddBufferCommon(kInputPort, std::move(buffer))) {
    return;
  }
}

void CodecImpl::SetOutputBufferSettings(
    fuchsia::mediacodec::CodecPortBufferSettings output_settings) {
  FXL_DCHECK(thrd_current() == fidl_thread());

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (!output_config_) {
      // invalid client behavior
      //
      // client must have received at least the initial OnOutputConfig() first
      // before sending SetOutputBufferSettings().
      FailLocked(
          "client sent SetOutputBufferSettings() when no output_config_");
      return;
    }

    // For a mid-stream output format change, this also enforces that the client
    // can only catch up to the mid-stream format change once.  In other words,
    // if the client has already caught up to the mid-stream config change, the
    // client no longer has an excuse to re-configure again with a stream
    // active.
    //
    // There's a check in SetBufferSettingsCommonLocked() that ignores this
    // message if the client's buffer_constraints_version_ordinal is behind
    // last_required_buffer_constraints_version_ordinal_, which gets updated
    // under the same lock hold interval as the server's de-configuring of
    // output buffers.
    //
    // There's a check in SetBufferSettingsCommonLocked() that closes the
    // channel if the client is sending a buffer_constraints_version_ordinal
    // that's newer than the last sent_buffer_constraints_version_ordinal_.
    if (IsOutputConfiguredLocked() && IsStreamActiveLocked()) {
      FailLocked(
          "client sent SetOutputBufferSettings() with IsStreamActiveLocked() + "
          "already-configured output");
      return;
    }

    SetBufferSettingsCommon(lock, kOutputPort, output_settings,
                            output_config_->buffer_constraints);
  }  // ~lock
}

void CodecImpl::AddOutputBuffer(fuchsia::mediacodec::CodecBuffer buffer) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  bool output_done_configuring =
      AddBufferCommon(kOutputPort, std::move(buffer));
  if (output_done_configuring) {
    // The StreamControl domain _might_ be waiting for output to be configured.
    wake_stream_control_condition_.notify_all();
  }
}

void CodecImpl::FlushEndOfStreamAndCloseStream(
    uint64_t stream_lifetime_ordinal) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!EnsureFutureStreamFlushSeenLocked(stream_lifetime_ordinal)) {
      return;
    }
  }
  PostToStreamControl([this, stream_lifetime_ordinal] {
    FlushEndOfStreamAndCloseStream_StreamControl(stream_lifetime_ordinal);
  });
}

void CodecImpl::FlushEndOfStreamAndCloseStream_StreamControl(
    uint64_t stream_lifetime_ordinal) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }

    // We re-check some things which were already future-verified a different
    // way, to allow for flexibility in the future-tracking stuff to permit less
    // checking in the Output ordering domain (fidl_thread()) without
    // breaking overall verification of a flush.  Any checking in the Output
    // ordering domain is for the future-tracking's own convenience only. The
    // checking here is the real checking.

    if (!CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal)) {
      return;
    }
    FXL_DCHECK(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
    if (!IsStreamActiveLocked() ||
        stream_lifetime_ordinal != stream_lifetime_ordinal_) {
      // TODO(dustingreen): epitaph
      FailLocked(
          "FlushEndOfStreamAndCloseStream() only valid on an active current "
          "stream (flush does not auto-create a new stream)");
      return;
    }
    // At this point we know that the stream is not discarded, and not already
    // flushed previously (because flush will discard the stream as there's
    // nothing more that the stream is permitted to do).
    FXL_DCHECK(stream_);
    FXL_DCHECK(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal);
    if (!stream_->input_end_of_stream()) {
      FailLocked(
          "FlushEndOfStreamAndCloseStream() is only permitted after "
          "QueueInputEndOfStream()");
      return;
    }
    while (!stream_->output_end_of_stream()) {
      // While waiting, we'll continue to send OnOutputPacket(),
      // OnOutputConfig(), and continue to process RecycleOutputPacket(), until
      // the client catches up to the latest config (as needed) and we've
      // started the send of output end_of_stream packet to the client.
      //
      // There is no way for the client to cancel a
      // FlushEndOfStreamAndCloseStream() short of closing the Codec channel.
      // Before long, the server will either send the OnOutputEndOfStream(), or
      // will send OnOmxStreamFailed(), or will close the Codec channel.  The
      // server must do one of those things before long (not allowed to get
      // stuck while flushing).
      //
      // Some core codecs (such as OMX codecs) have no way to report mid-stream
      // input data corruption errors or similar without it being a stream
      // failure, so if there's any stream error it turns into OnStreamFailed().
      // It's also permitted for a server to set error_detected_ bool(s) on
      // output packets and send OnOutputEndOfStream() despite detected errors,
      // but this is only a reasonable behavior for the server if the server
      // normally would detect and report mid-stream input corruption errors
      // without an OnStreamFailed().
      output_end_of_stream_seen_.wait(lock);
    }

    // Now that flush is done, we close the current stream because there is not
    // any subsequent message for the current stream that's valid.
    EnsureStreamClosed(lock);
  }  // ~lock
}

void CodecImpl::CloseCurrentStream(uint64_t stream_lifetime_ordinal,
                                   bool release_input_buffers,
                                   bool release_output_buffers) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!EnsureFutureStreamCloseSeenLocked(stream_lifetime_ordinal)) {
      return;
    }
  }  // ~lock
  PostToStreamControl([this, stream_lifetime_ordinal, release_input_buffers,
                       release_output_buffers] {
    CloseCurrentStream_StreamControl(
        stream_lifetime_ordinal, release_input_buffers, release_output_buffers);
  });
}

void CodecImpl::CloseCurrentStream_StreamControl(
    uint64_t stream_lifetime_ordinal, bool release_input_buffers,
    bool release_output_buffers) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  std::unique_lock<std::mutex> lock(lock_);
  if (IsStoppingLocked()) {
    return;
  }
  EnsureStreamClosed(lock);
  if (release_input_buffers) {
    EnsureBuffersNotConfigured(lock, kInputPort);
  }
  if (release_output_buffers) {
    EnsureBuffersNotConfigured(lock, kOutputPort);
  }
}

void CodecImpl::Sync(SyncCallback callback) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  // By posting to StreamControl ordering domain, we sync both Output ordering
  // domain (on fidl_thread()) and the StreamControl ordering domain.
  PostToStreamControl([this, callback = std::move(callback)]() mutable {
    Sync_StreamControl(std::move(callback));
  });
}

void CodecImpl::Sync_StreamControl(SyncCallback callback) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  if (IsStopping()) {
    // In this case ~callback will happen instead of callback(), in which case
    // the response won't be sent, which is appropriate - the channel is getting
    // closed soon instead, and the client has to tolerate that.
    return;
  }
  callback();
}

void CodecImpl::RecycleOutputPacket(
    fuchsia::mediacodec::CodecPacketHeader available_output_packet) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  CodecPacket* packet = nullptr;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!CheckOldBufferLifetimeOrdinalLocked(
            kOutputPort, available_output_packet.buffer_lifetime_ordinal)) {
      return;
    }
    if (available_output_packet.buffer_lifetime_ordinal <
        buffer_lifetime_ordinal_[kOutputPort]) {
      // ignore arbitrarily-stale required by protocol
      //
      // Thanks to even values from the client being prohibited, this also
      // covers mid-stream output config change where the server has already
      // de-configured output buffers but the client doesn't know about that
      // yet. We include that case here by setting
      // buffer_lifetime_ordinal_[kOutputPort] to the next even value
      // when de-configuring output server-side until the client has
      // re-configured output.
      return;
    }
    FXL_DCHECK(available_output_packet.buffer_lifetime_ordinal ==
               buffer_lifetime_ordinal_[kOutputPort]);
    if (!IsOutputConfiguredLocked()) {
      FailLocked(
          "client sent RecycleOutputPacket() for buffer_lifetime_ordinal that "
          "isn't fully configured yet - bad client behavior");
      return;
    }
    FXL_DCHECK(IsOutputConfiguredLocked());
    if (available_output_packet.packet_index >=
        all_packets_[kOutputPort].size()) {
      FailLocked(
          "out of range packet_index from client in RecycleOutputPacket()");
      return;
    }
    uint32_t packet_index = available_output_packet.packet_index;
    if (all_packets_[kOutputPort][packet_index]->is_free()) {
      FailLocked(
          "packet_index already free at protocol level - invalid client "
          "message");
      return;
    }
    // Mark free at protocol level.
    all_packets_[kOutputPort][packet_index]->SetFree(true);

    // Before handing the packet to the core codec, clear some fields that the
    // core codec is expected to set (or optionally set in the case of
    // timstamp_ish).  In addition to these parameters, a core codec can emit
    // output config changes via onCoreCodecMidStreamOutputConfigChange().
    packet = all_packets_[kOutputPort][packet_index].get();
    packet->ClearStartOffset();
    packet->ClearValidLengthBytes();
    packet->ClearTimestampIsh();
  }

  // Recycle to core codec.
  CoreCodecRecycleOutputPacket(packet);
}

void CodecImpl::QueueInputFormatDetails(
    uint64_t stream_lifetime_ordinal,
    fuchsia::mediacodec::CodecFormatDetails format_details) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!EnsureFutureStreamSeenLocked(stream_lifetime_ordinal)) {
      return;
    }
  }  // ~lock
  PostToStreamControl([this, stream_lifetime_ordinal,
                       format_details = std::move(format_details)]() mutable {
    QueueInputFormatDetails_StreamControl(stream_lifetime_ordinal,
                                          std::move(format_details));
  });
}

// TODO(dustingreen): Need test coverage for this method, to cover at least
// the same format including OOB bytes as were specified during codec creation,
// and codec creation with no OOB bytes then this method setting OOB bytes (not
// the ideal client usage pattern in the long run since the CreateDecoder()
// might decline to provide a optimized but partial Codec implementation, but
// should be allowed nonetheless).
void CodecImpl::QueueInputFormatDetails_StreamControl(
    uint64_t stream_lifetime_ordinal,
    fuchsia::mediacodec::CodecFormatDetails format_details) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);

  std::unique_lock<std::mutex> lock(lock_);
  if (IsStoppingLocked()) {
    return;
  }
  if (!CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal)) {
    return;
  }
  FXL_DCHECK(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
  if (stream_lifetime_ordinal > stream_lifetime_ordinal_) {
    if (!StartNewStream(lock, stream_lifetime_ordinal)) {
      return;
    }
  }
  FXL_DCHECK(stream_lifetime_ordinal == stream_lifetime_ordinal_);
  if (stream_->input_end_of_stream()) {
    FailLocked(
        "QueueInputFormatDetails() after QueueInputEndOfStream() unexpected");
    return;
  }
  if (stream_->future_discarded()) {
    // No reason to handle since the stream is future-discarded.
    return;
  }
  stream_->SetInputFormatDetails(
      std::make_unique<fuchsia::mediacodec::CodecFormatDetails>(
          std::move(format_details)));
  // SetOobConfigPending(true) to ensure oob_config_pending() is true.
  //
  // This call is needed only to properly handle a call to
  // QueueInputFormatDetails() mid-stream.  For new streams that lack any calls
  // to QueueInputFormatDetails() before an input packet arrives, the
  // oob_config_pending() will already be true because it starts true for a new
  // stream.  For QueueInputFormatDetails() at the start of a stream before any
  // packets, oob_config_pending() will already be true.
  //
  // For decoders this is basically a pending codec_oob_bytes.  For encoders
  // this pending config change can potentially include uncompressed format
  // details, if mid-stream format change is supported by the encoder.
  stream_->SetOobConfigPending(true);
}

void CodecImpl::QueueInputPacket(fuchsia::mediacodec::CodecPacket packet) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }
    if (!EnsureFutureStreamSeenLocked(packet.stream_lifetime_ordinal)) {
      return;
    }
  }  // ~lock
  PostToStreamControl([this, packet = std::move(packet)]() mutable {
    QueueInputPacket_StreamControl(std::move(packet));
  });
}

void CodecImpl::QueueInputPacket_StreamControl(
    fuchsia::mediacodec::CodecPacket packet) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);

  fuchsia::mediacodec::CodecPacketHeader temp_header_copy =
      fidl::Clone(packet.header);

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }

    // Unless we cancel this cleanup, we'll free the input packet back to the
    // client.
    auto send_free_input_packet_locked = fbl::MakeAutoCall(
        [this, header = std::move(temp_header_copy)]() mutable {
          // Mute sending this if FailLocked() was called previously, in case
          // the reason we're here is something horribly wrong with the packet
          // header. This way we avoid repeating gibberish back to the client.
          // While that gibberish might be a slight clue for debugging in some
          // cases, it's not valid protocol, so don't send it.  If
          // IsStoppingLocked(), the Codec channel will close soon, making this
          // response unnecessary.
          if (!IsStoppingLocked()) {
            SendFreeInputPacketLocked(std::move(header));
          }
        });

    if (!CheckOldBufferLifetimeOrdinalLocked(
            kInputPort, packet.header.buffer_lifetime_ordinal)) {
      return;
    }

    // For input, mid-stream config changes are not a thing and input buffers
    // are never unilaterally de-configured by the Codec server.
    FXL_DCHECK(buffer_lifetime_ordinal_[kInputPort] ==
               port_settings_[kInputPort]->buffer_lifetime_ordinal);
    // For this message we're extra-strict re. buffer_lifetime_ordinal, at least
    // for now.
    //
    // In contrast to output, the server doesn't use even values to track config
    // changes that the client doesn't know about yet, since the server can't
    // unilaterally demand any changes to the input settings after initially
    // specifying the input constraints.
    //
    // One could somewhat-convincingly argue that this field in this particular
    // message is a bit pointless, but it might serve to detect client-side
    // bugs faster thanks to this check.
    if (packet.header.buffer_lifetime_ordinal !=
        port_settings_[kInputPort]->buffer_lifetime_ordinal) {
      FailLocked(
          "client QueueInputPacket() with invalid buffer_lifetime_ordinal.");
      return;
    }

    if (!CheckStreamLifetimeOrdinalLocked(packet.stream_lifetime_ordinal)) {
      return;
    }
    FXL_DCHECK(packet.stream_lifetime_ordinal >= stream_lifetime_ordinal_);

    if (packet.stream_lifetime_ordinal > stream_lifetime_ordinal_) {
      // This case implicitly starts a new stream.  If the client wanted to
      // ensure that the old stream would be fully processed, the client would
      // have sent FlushEndOfStreamAndCloseStream() previously, whose
      // processing (previous to reaching here) takes care of the flush.
      //
      // Start a new stream, synchronously.
      if (!StartNewStream(lock, packet.stream_lifetime_ordinal)) {
        return;
      }
    }
    FXL_DCHECK(packet.stream_lifetime_ordinal == stream_lifetime_ordinal_);

    if (!IsInputConfiguredLocked()) {
      FailLocked("client QueueInputPacket() with input buffers not configured");
      return;
    }
    if (packet.header.packet_index >= all_packets_[kInputPort].size()) {
      FailLocked("client QueueInputPacket() with packet_index out of range");
      return;
    }

    // Protocol check re. free/busy coherency.
    if (!all_packets_[kInputPort][packet.header.packet_index]->is_free()) {
      FailLocked("client QueueInputPacket() with packet_index !free");
      return;
    }
    all_packets_[kInputPort][packet.header.packet_index]->SetFree(false);

    if (stream_->input_end_of_stream()) {
      FailLocked("QueueInputPacket() after QueueInputEndOfStream() unexpeted");
      return;
    }

    if (stream_->future_discarded()) {
      // Don't queue to core codec.  The stream_ may have never fully started,
      // or may have been future-discarded since.  Either way, skip queueing to
      // the core codec.
      //
      // If the stream didn't fully start - as in, the client moved on to
      // another stream before fully configuring output, then the core codec is
      // not presently in a state compatible with queueing input, but the Codec
      // interface is.  So in that case, we must avoid queueing to the core
      // codec for correctness.
      //
      // If the stream was just future-discarded after fully starting, then this
      // is just an optimization to avoid giving the core codec more work to do
      // for a stream the client has already discarded.
      //
      // ~send_free_input_packet_locked
      // ~lock
      return;
    }

    // Sending OnFreeInputPacket() will happen later instead, when the core
    // codec gives back the packet.
    send_free_input_packet_locked.cancel();
  }  // ~lock

  if (stream_->oob_config_pending()) {
    HandlePendingInputFormatDetails();
    stream_->SetOobConfigPending(false);
  }

  CodecPacket* core_codec_packet =
      all_packets_[kInputPort][packet.header.packet_index].get();
  core_codec_packet->SetStartOffset(packet.start_offset);
  core_codec_packet->SetValidLengthBytes(packet.valid_length_bytes);
  if (packet.has_timestamp_ish) {
    core_codec_packet->SetTimstampIsh(packet.timestamp_ish);
  } else {
    core_codec_packet->ClearTimestampIsh();
  }

  // We don't need to be under lock for this, because the fact that we're on the
  // StreamControl domain is enough to guarantee that any other control of the
  // core codec will occur after this.
  CoreCodecQueueInputPacket(core_codec_packet);
}

void CodecImpl::QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!EnsureFutureStreamSeenLocked(stream_lifetime_ordinal)) {
      return;
    }
  }  // ~lock
  PostToStreamControl([this, stream_lifetime_ordinal] {
    QueueInputEndOfStream_StreamControl(stream_lifetime_ordinal);
  });
}

void CodecImpl::QueueInputEndOfStream_StreamControl(
    uint64_t stream_lifetime_ordinal) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }
    if (!CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal)) {
      return;
    }
    FXL_DCHECK(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
    if (stream_lifetime_ordinal > stream_lifetime_ordinal_) {
      // We start a new stream given an end-of-stream for a stream we've not
      // seen before, since allowing empty streams to not be errors may be nicer
      // to use.
      if (!StartNewStream(lock, stream_lifetime_ordinal)) {
        return;
      }
    }

    if (stream_->future_discarded()) {
      // Don't queue to OMX.  The stream_ may have never fully started, or may
      // have been future-discarded since.  Either way, skip queueing to OMX. We
      // only really must do this because the stream may not have ever fully
      // started, in the case where the client moves on to a new stream before
      // catching up to latest output config.
      return;
    }
  }  // ~lock

  CoreCodecQueueInputEndOfStream();
}

void CodecImpl::onInputConstraintsReady() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  if (!IsCoreCodecRequiringOutputConfigForFormatDetection()) {
    return;
  }
  std::unique_lock<std::mutex> lock(lock_);
  StartIgnoringClientOldOutputConfigLocked();
  GenerateAndSendNewOutputConfig(lock, true);
}

void CodecImpl::UnbindLocked() {
  // We must have first gotten far enough through BindAsync() before calling
  // UnbindLocked().
  FXL_DCHECK(was_logically_bound_);

  if (was_unbind_started_) {
    // Ignore the second trigger if we have a near-simultaneous failure from
    // StreamControl thread (for example) and from fidl_thread() (for
    // example).  The first will start unbinding, and the second will be
    // ignored.  Since completion of the Unbind() call doesn't imply anything
    // about how done the unbind is, there's no need for the second caller to
    // be blocked waiting for the first caller's unbind to be done.
    return;
  }
  was_unbind_started_ = true;
  wake_stream_control_condition_.notify_all();

  // Unbind() / UnbindLocked() can be called from any thread.
  //
  // Regardless of what thread UnbindLocked() is called on, "this" will remain
  // allocated at least until the caller of UnbindLocked() releases lock_.
  //
  // The shutdown sequence here is meant to be general enough to accomodate code
  // changes without being super brittle.  Not all the potential cases accounted
  // for in this sequence can necessarily happen currently, but it seems good to
  // stop all activity in a way that'll hold up even if a change posts another
  // lambda or similar.
  //
  // In all cases, this posted lambda runs after BindAsync()'s work that's
  // posted to StreamControl, because any/all calls to UnbindLocked() happen
  // after BindAsync() has posted to StreamControl.
  PostToStreamControl([this] {
    // At this point we know that no more streams will be started by
    // StreamControl ordering domain (thanks to was_unbind_started_ /
    // IsStoppingLocked() checks), but lambdas posted to the StreamControl
    // ordering domain (by the fidl_thread() or by core codec) may still
    // be creating other activity such as posting lambdas to StreamControl or
    // fidl_thread().
    //
    // There are two purposes to this lock acquire, one of which is subtle.
    //
    // This lock acquire also delays execution here until the caller of
    // UnbindLocked() has released lock_.  This delay is nice to do on the
    // stream control thread instead of later on the fidl_thread(), and
    // we need the lock here to call EnsureStreamClosed() anyway.
    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);
      // Stop core codec associated with this CodecImpl, partly to make sure it
      // stops running code that could make calls into this CodecImpl, and
      // partly to ensure the core codec isn't in the middle of anything when it
      // gets deleted.
      //
      // We know the core codec won't start more activity because the core codec
      // isn't allowed to initiate actions while there's no active stream, and
      // because no new active stream will be created.  All _StreamControl
      // methods check IsStoppingLocked() at the start, and the StreamControl
      // ordering domain is the only domain that ever starts a stream.
      //
      // We intentionally don't check for IsStoppingLocked() in protocol
      // dispatch methods running on fidl_thread(). For example the codec
      // must tolerate calls to configure buffers after EnsureStreamClosed()
      // here.  The Unbind() later is what silences the protocol message
      // dispatch methods.  Checking for IsStoppingLocked() in protocol dispatch
      // methods would only decrease the probability of certain event orderings,
      // not eliminate those orderings, so it's actually better to let them
      // happen to get more coverage of those orderings.
      if (is_core_codec_init_called_) {
        EnsureStreamClosed(lock);
      }
    }  // ~lock

    PostToSharedFidl([this] {
      FXL_DCHECK(thrd_current() == fidl_thread());
      // If not being called from binding_'s error handler, unbind from the
      // channel so we won't see any more incoming FIDL messages.  This binding
      // doesn't own "this".
      //
      // The Unbind() stops any additional FIDL dispatching re. this CodecImpl,
      // but it doesn't stop lambdas re. this CodecImpl from being queued to
      // fidl_thread().  Potentially such lambdas can be coming from
      // StreamControl domain still at this point (even after the Unbind()).
      if (binding_.is_bound()) {
        binding_.Unbind();
      }

      // We need to shut down the StreamControl thread, which can be shut down
      // quickly (it's not waiting any significant duration on anything) thanks
      // to was_unbind_started_ and wake_stream_control_condition_.  Normally
      // the fidl_thread() waiting for the StreamControl thread to do
      // anything would be bad, because the fidl_thread() is non-blocking
      // and the StreamControl thread can block on stuff, but StreamControl
      // thread behavior after was_unbind_started_ = true and
      // wake_stream_control_condition_.notify_all() does not block and does not
      // wait on fidl_thread().  So in this case it's ok to wait here.
      stream_control_loop_.Quit();
      stream_control_loop_.JoinThreads();
      // This is when we first know that StreamControl can't be queueing any
      // more lambdas re. this CodecImpl toward fidl_thread().  (We
      // already know the core codec isn't queuing any more).  If any lambdas
      // are queued to StreamControl at/beyond this point, we rely on those
      // being safe to just delete.
      stream_control_loop_.Shutdown();

      // Before calling the owner_error_handler_, we declare that unbind is
      // done so that during the destructor we can check that unbind is done.
      was_unbind_completed_ = true;

      // This post ensures that any other items posted to the
      // fidl_thread() for this CodecImpl run before "delete this". By
      // the time we post here, we know that no further lambdas will be posted
      // to fidl_thread() regarding this CodecImpl other than this post
      // itself - specifically:
      //   * The core codec has been stopped, in the sense that it has no
      //     current stream.  The core codec is required to be delete-able when
      //     it has no current stream, and required not to asynchronously post
      //     more work to the CodecImpl (because calling onCoreCodec... methods
      //     is not allowed when there is no current stream).
      //   * The binding_.Unbind() has run, so no more FIDL dispatching to this
      //     CodecImpl.
      //   * The stream_control_loop_.JoinThreads() has run, so no more posting
      //     from the stream_control_thread_ since it's no longer running.
      //   * The previous bullets are the complete list of sources of items
      //     posted to the fidl_thread() regarding this CodecImpl.
      //
      // By posting to run _after_ any of the above sources, we know that by the
      // time this posted lambda runs, the "delete this" in this lambda will be
      // after any other posted lambdas.
      //
      // For example, any lambdas previously posted to send a message via
      // this->binding_ (which is soon to be deleted) will run before the lambda
      // postead here.
      //
      // This relies on other lambdas running on fidl_thread() re. this
      // CodecImpl to not re-post to the fidl_thread().
      device_->driver()->PostToSharedFidl(
          [client_error_handler = std::move(owner_error_handler_)] {
            // This call deletes the CodecImpl.
            client_error_handler();
          });
      // "this" will be deleted shortly async when lambda posted just above
      // runs.
      return;
    });
  });
  // "this" remains allocated until caller releases lock_.
}

void CodecImpl::Unbind() {
  std::lock_guard<std::mutex> lock(lock_);
  UnbindLocked();
  // ~lock
  //
  // "this" may be deleted very shortly after ~lock, depending on what thread
  // Unbind() is called from.
}

bool CodecImpl::IsStreamActiveLocked() {
  return stream_lifetime_ordinal_ % 2 == 1;
}

void CodecImpl::SetBufferSettingsCommon(
    std::unique_lock<std::mutex>& lock, CodecPort port,
    const fuchsia::mediacodec::CodecPortBufferSettings& settings,
    const fuchsia::mediacodec::CodecBufferConstraints& constraints) {
  FXL_DCHECK(port == kInputPort && thrd_current() == stream_control_thread_ ||
             port == kOutputPort && thrd_current() == fidl_thread());
  FXL_DCHECK(!IsStoppingLocked());
  // Invariant
  //
  // Either we've never seen settings, or the logical buffer_lifetime_ordinal_
  // is either the last accepted from the client or one more than that as a way
  // of cleanly permitting the server to unilaterally de-configure output
  // buffers.
  FXL_DCHECK((!port_settings_[port && buffer_lifetime_ordinal_[port] == 0]) ||
             (buffer_lifetime_ordinal_[port] >=
                  port_settings_[port]->buffer_lifetime_ordinal &&
              buffer_lifetime_ordinal_[port] <=
                  port_settings_[port]->buffer_lifetime_ordinal + 1));
  if (settings.buffer_lifetime_ordinal <=
      protocol_buffer_lifetime_ordinal_[port]) {
    FailLocked(
        "settings.buffer_lifetime_ordinal <= "
        "protocol_buffer_lifetime_ordinal_[port] - port: %d",
        port);
    return;
  }
  protocol_buffer_lifetime_ordinal_[port] = settings.buffer_lifetime_ordinal;

  if (settings.buffer_lifetime_ordinal % 2 == 0) {
    FailLocked(
        "Only odd values for buffer_lifetime_ordinal are permitted - port: %d "
        "value %lu",
        port, settings.buffer_lifetime_ordinal);
    return;
  }

  if (settings.buffer_constraints_version_ordinal >
      sent_buffer_constraints_version_ordinal_[port]) {
    FailLocked(
        "Client sent too-new buffer_constraints_version_ordinal - port: %d",
        port);
    return;
  }

  if (settings.buffer_constraints_version_ordinal <
      last_required_buffer_constraints_version_ordinal_[port]) {
    // ignore - client will probably catch up later
    return;
  }

  // We've peeled off too new and too old above.
  FXL_DCHECK(settings.buffer_constraints_version_ordinal >=
                 last_required_buffer_constraints_version_ordinal_[port] &&
             settings.buffer_constraints_version_ordinal <=
                 sent_buffer_constraints_version_ordinal_[port]);

  // We've already checked above that the buffer_lifetime_ordinal is in
  // sequence.
  FXL_DCHECK(!port_settings_[port] ||
             settings.buffer_lifetime_ordinal > buffer_lifetime_ordinal_[port]);

  if (!ValidateBufferSettingsVsConstraintsLocked(port, settings, constraints)) {
    // This assert is safe only because this thread still holds lock_.
    FXL_DCHECK(IsStoppingLocked());
    return;
  }

  // Little if any reason to do this outside the lock.
  EnsureBuffersNotConfigured(lock, port);

  // This also starts the new buffer_lifetime_ordinal.
  port_settings_[port] =
      std::make_unique<fuchsia::mediacodec::CodecPortBufferSettings>(
          std::move(settings));
  buffer_lifetime_ordinal_[port] =
      port_settings_[port]->buffer_lifetime_ordinal;
}

void CodecImpl::EnsureBuffersNotConfigured(std::unique_lock<std::mutex>& lock,
                                           CodecPort port) {
  // This method can be called on input only if there's no current stream.
  //
  // On output, this method can be called if there's no current stream or if
  // we're in the middle of an ouput config change.
  //
  // On input, this can only be called on stream_control_thread_.
  //
  // On output, this can be called on stream_control_thread_ or output_thread_.
  FXL_DCHECK(thrd_current() == stream_control_thread_ ||
             (port == kOutputPort && (thrd_current() == fidl_thread())));

  is_port_configured_[port] = false;

  // Ensure that buffers aren't with the core codec.
  {  // scope unlock
    ScopedUnlock unlock(lock);
    CoreCodecEnsureBuffersNotConfigured(port);
  }

  // For mid-stream output config change, the caller is responsible for ensuring
  // that buffers are not with the HW first.
  //
  // TODO(dustingreen): Check anything relelvant to buffers not presently being
  // with the HW.
  // FXL_DCHECK(all_packets_[port].empty() ||
  // !all_packets_[port][0]->is_with_hw());

  all_packets_[port].clear();
  all_buffers_[port].clear();
  FXL_DCHECK(all_packets_[port].empty());
  FXL_DCHECK(all_buffers_[port].empty());
}

bool CodecImpl::ValidateBufferSettingsVsConstraintsLocked(
    CodecPort port,
    const fuchsia::mediacodec::CodecPortBufferSettings& settings,
    const fuchsia::mediacodec::CodecBufferConstraints& constraints) {
  if (settings.packet_count_for_codec <
      constraints.packet_count_for_codec_min) {
    FailLocked("packet_count_for_codec < packet_count_for_codec_min");
    return false;
  }
  if (settings.packet_count_for_codec >
      constraints.packet_count_for_codec_max) {
    FailLocked("packet_count_for_codec > packet_count_for_codec_max");
    return false;
  }
  if (settings.packet_count_for_client >
      constraints.packet_count_for_client_max) {
    FailLocked("packet_count_for_client > packet_count_for_client_max");
    return false;
  }
  if (settings.per_packet_buffer_bytes <
      constraints.per_packet_buffer_bytes_min) {
    FailLocked("per_packet_buffer_bytes < per_packet_buffer_bytes_min");
    return false;
  }
  if (settings.per_packet_buffer_bytes >
      constraints.per_packet_buffer_bytes_max) {
    FailLocked("per_packet_buffer_bytes > per_packet_buffer_bytes_max");
    return false;
  }
  if (settings.single_buffer_mode && !constraints.single_buffer_mode_allowed) {
    FailLocked("single_buffer_mode && !single_buffer_mode_allowed");
    return false;
  }
  return true;
}

bool CodecImpl::AddBufferCommon(CodecPort port,
                                fuchsia::mediacodec::CodecBuffer buffer) {
  FXL_DCHECK(port == kInputPort && (thrd_current() == stream_control_thread_) ||
             port == kOutputPort && (thrd_current() == fidl_thread()));
  bool done_configuring = false;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (buffer.buffer_lifetime_ordinal % 2 == 0) {
      FailLocked(
          "Client sent even buffer_lifetime_ordinal, but must be odd - exiting "
          "- port: %u\n",
          port);
      return false;
    }

    if (buffer.buffer_lifetime_ordinal !=
        protocol_buffer_lifetime_ordinal_[port]) {
      FailLocked(
          "Incoherent SetOutputBufferSettings()/SetInputBufferSettings() + "
          "AddOutputBuffer()/AddInputBuffer()s - exiting - port: %d\n",
          port);
      return false;
    }

    // If the server is not interested in the client's buffer_lifetime_ordinal,
    // the client's buffer_lifetime_ordinal won't match the server's
    // buffer_lifetime_ordinal_.  The client will probably later catch up.
    if (buffer.buffer_lifetime_ordinal != buffer_lifetime_ordinal_[port]) {
      // The case that ends up here is when a client's output configuration
      // (whole or last part) is being ignored because it's not yet caught up
      // with last_required_buffer_constraints_version_ordinal_.

      // This case won't happen for input, at least for now.  This is an assert
      // rather than a client behavior check, because previous client protocol
      // checks have already peeled off any invalid client behavior that might
      // otherwise cause this assert to trigger.
      FXL_DCHECK(port == kOutputPort);

      // Ignore the client's message.  The client will probably catch up later.
      return false;
    }

    if (buffer.buffer_index != all_buffers_[port].size()) {
      FailLocked(
          "AddOutputBuffer()/AddInputBuffer() had buffer_index out of sequence "
          "- port: %d buffer_index: %u all_buffers_[port].size(): %lu",
          port, buffer.buffer_index, all_buffers_[port].size());
      return false;
    }

    uint32_t required_buffer_count =
        BufferCountFromPortSettings(*port_settings_[port]);
    if (buffer.buffer_index >= required_buffer_count) {
      FailLocked("AddOutputBuffer()/AddInputBuffer() extra buffer - port: %d",
                 port);
      return false;
    }

    // So far, there's little reason to avoid doing the Init() part under the
    // lock, even if it can be a bit more time consuming, since there's no data
    // processing happening at this point anyway, and there wouldn't be any
    // happening in any other code location where we could potentially move the
    // Init() either.

    std::unique_ptr<CodecBuffer> local_buffer = std::unique_ptr<CodecBuffer>(
        new CodecBuffer(this, port, std::move(buffer)));
    if (!local_buffer->Init()) {
      FailLocked(
          "AddOutputBuffer()/AddInputBuffer() couldn't Init() new buffer - "
          "port: %d",
          port);
      return false;
    }
    // Inform the core codec up-front about each buffer.
    CoreCodecAddBuffer(port, local_buffer.get());
    all_buffers_[port].push_back(std::move(local_buffer));
    if (all_buffers_[port].size() == required_buffer_count) {
      // Stash this while we can, before the client de-configures.
      last_provided_buffer_constraints_version_ordinal_[port] =
          port_settings_[port]->buffer_constraints_version_ordinal;
      // Now we allocate all_packets_[port].
      FXL_DCHECK(all_packets_[port].empty());
      uint32_t packet_count =
          PacketCountFromPortSettings(*port_settings_[port]);
      for (uint32_t i = 0; i < packet_count; i++) {
        uint32_t buffer_index = required_buffer_count == 1 ? 0 : i;
        CodecBuffer* buffer = all_buffers_[port][buffer_index].get();
        FXL_DCHECK(buffer_lifetime_ordinal_[port] ==
                   port_settings_[port]->buffer_lifetime_ordinal);
        // Private constructor to prevent core codec maybe creating its own
        // Packet instances (which isn't the intent) seems worth the hassle of
        // not using make_unique<>() here.
        all_packets_[port].push_back(
            std::unique_ptr<CodecPacket>(new CodecPacket(
                port_settings_[port]->buffer_lifetime_ordinal, i, buffer)));
      }

      {  // scope unlock
        ScopedUnlock unlock(lock);

        // A core codec can take action here to finish configuring buffers if
        // it's able, or can delay configuring buffers until
        // CoreCodecStartStream() if that works better for the core codec.
        CoreCodecConfigureBuffers(port, all_packets_[port]);

        // All output packets need to start with the core codec.  This is
        // implicit for the Codec interface (implied by adding the last output
        // buffer) but explicit in the CodecAdapter interface.
        if (port == kOutputPort) {
          for (uint32_t i = 0; i < packet_count; i++) {
            CoreCodecRecycleOutputPacket(all_packets_[kOutputPort][i].get());
          }
        }
      }  // ~unlock

      // For OMX case, we tell OMX about the potentially-new buffer count
      // separately later, just before moving from OMX loaded to OMX idle, or as
      // part of mid-stream output config change.

      // For OMX case, we don't allocate OMX_BUFFERHEADERTYPE yet here by
      // calling OMX UseBuffer() yet, because we can be in OMX_StateLoaded
      // currently, and OMX UseBuffer() isn't valid until we're moving from
      // OMX_StateLoaded to OMX_StateIdle.

      is_port_configured_[port] = true;
      done_configuring = true;
    }
  }
  return done_configuring;
}

bool CodecImpl::CheckOldBufferLifetimeOrdinalLocked(
    CodecPort port, uint64_t buffer_lifetime_ordinal) {
  // The client must only send odd values.  0 is even so we don't need a
  // separate check for that.
  if (buffer_lifetime_ordinal % 2 == 0) {
    FailLocked(
        "CheckOldBufferLifetimeOrdinalLocked() - buffer_lifetime_ordinal must "
        "be odd");
    return false;
  }
  if (buffer_lifetime_ordinal > protocol_buffer_lifetime_ordinal_[port]) {
    FailLocked(
        "client sent new buffer_lifetime_ordinal in message type that doesn't "
        "allow new buffer_lifetime_ordinals");
    return false;
  }
  return true;
}

bool CodecImpl::CheckStreamLifetimeOrdinalLocked(
    uint64_t stream_lifetime_ordinal) {
  if (stream_lifetime_ordinal % 2 != 1) {
    FailLocked("stream_lifetime_ordinal must be odd.\n");
    return false;
  }
  if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
    FailLocked("client sent stream_lifetime_ordinal that went backwards");
    return false;
  }
  return true;
}

bool CodecImpl::StartNewStream(std::unique_lock<std::mutex>& lock,
                               uint64_t stream_lifetime_ordinal) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  FXL_DCHECK((stream_lifetime_ordinal % 2 == 1) &&
             "new stream_lifetime_ordinal must be odd");

  if (IsStoppingLocked()) {
    // Don't start a new stream if the whole CodecImpl is already stopping.
    //
    // A completely different path will take care of calling
    // EnsureStreamClosed() during CodecImpl stop.
    //
    // TODO(dustingreen): If all callers are already checking this at the top
    // of each relevant .*_StreamControl method, then we don't necessarily need
    // this check, but consider any intervals where lock_ isn't held also - we
    // don't want the wait for stream_control_thread_ to exit to ever be long
    // when stopping this CodecImpl.
    return false;
  }

  EnsureStreamClosed(lock);

  FXL_DCHECK((stream_lifetime_ordinal_ % 2 == 0) &&
             "expecting no current stream");
  FXL_DCHECK(!stream_);

  // Now it's time to start the new stream.  We start the new stream at
  // Codec layer first then core codec layer.

  if (!IsInputConfiguredLocked()) {
    FailLocked(
        "input not configured before start of stream (QueueInputPacket())");
    return false;
  }

  FXL_DCHECK(stream_queue_.size() >= 1);
  FXL_DCHECK(stream_lifetime_ordinal ==
             stream_queue_.front()->stream_lifetime_ordinal());
  stream_ = stream_queue_.front().get();
  // Update the stream_lifetime_ordinal_ to the new stream.  We need to do
  // this before we send new output config, since the output config will be
  // generated using the current stream ordinal.
  FXL_DCHECK(stream_lifetime_ordinal > stream_lifetime_ordinal_);
  stream_lifetime_ordinal_ = stream_lifetime_ordinal;
  FXL_DCHECK(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal_);

  // The client is not permitted to unilaterally re-configure output while a
  // stream is active, but the client may still be responding to a previous
  // server-initiated mid-stream format change.
  //
  // ###########################################################################
  // We don't attempt to optimize every case as much as might be possible here.
  // The main overall optimization is that it's possible to switch streams
  // without reallocating buffers.  We also need to make sure it's possible to
  // detect output format at the start of a stream regardless of what happened
  // before, and possible to perform a mid-stream format change.
  // ###########################################################################
  //
  // Given the above, our *main concern* here is that we get to a state where we
  // *know* the client isn't trying to re-configure output during format
  // detection, which at best would be confusing to allow, so we avoid that
  // possiblity here by forcing a client to catch up with the server, if there's
  // *any possibility* that the client might still be working on catching up
  // with the server.
  //
  // If the client's most recently fully-completed output config is less than
  // the most recently sent output constraints with action_required true, then
  // we force an even fresher output constraints here tagged as being relevant
  // to the current stream, and wait for the client to catch up to that before
  // continuing.  By marking as being for this stream, we ensure that the client
  // will bother to finish configuring output, which gets us to a state where we
  // know it's safe to do another mid-stream format change as needed (vs. the
  // client maybe finishing the old config or maybe not).
  //
  // We also force the client to catch up if the core codec previously indicated
  // that the current config is "meh".  This may not be strictly necessary since
  // the "meh" was with respect to the old stream, but just in case a core codec
  // cares, we move on from the old config before delivering new stream data.
  //
  // Some core codecs (such as OMX codecs) require the output to be configured
  // to _something_ as they don't support giving us the real output config
  // unless the output is configured to at least something at first.
  //
  // Other core codecs (such as some HW-based codecs) can deal with no output
  // configured while detecting the output format, but even for those codecs, we
  // only do this if the above cases don't apply.  These codecs have to deal
  // with an output config that's already set across a stream switch anyway, to
  // permit buffers to stay configured across a stream switch when possible, so
  // the cases above potentially setting an output config that's not super
  // relevant to the new stream doesn't really complicate the core codec since
  // an old stream's config might not be super relevant to a new stream either.
  //
  // Format detection is separate and handled like a mid-stream format change.
  // This stuff here is just getting output config into a non-changing state
  // before we start format detection.
  bool is_new_config_needed;
  // The statement below could obviously be re-written as a giant boolean
  // expression, but this way seems easier to comment.
  if (last_provided_buffer_constraints_version_ordinal_[kOutputPort] <
      last_required_buffer_constraints_version_ordinal_[kOutputPort]) {
    // The client _might_ still be trying to catch up, so to disambiguate,
    // require an even fresher config with respect to this new stream to
    // unambiguously force the client to catch up to the even newer config.
    is_new_config_needed = true;
  } else if (IsCoreCodecRequiringOutputConfigForFormatDetection() &&
             !IsOutputConfiguredLocked()) {
    // The core codec requires output to be configured before format detection,
    // so we force the client to provide an output config before format
    // detection.
    is_new_config_needed = true;
  } else if (IsOutputConfiguredLocked() &&
             port_settings_[kOutputPort]->buffer_constraints_version_ordinal <=
                 core_codec_meh_output_buffer_constraints_version_ordinal_) {
    // The core codec previously expressed "meh" regarding the current config's
    // buffer_constraints_version_ordinal, so to avoid mixing that with core
    // codec stream switch, force the client to configure output buffers before
    // format detection for the new stream.
    is_new_config_needed = true;
  } else {
    // The core codec is ok to perform format detection in the current state,
    // and we know that a well-behaved client is not currently trying to change
    // the output config.
    is_new_config_needed = false;
  }

  if (is_new_config_needed) {
    StartIgnoringClientOldOutputConfigLocked();
    EnsureBuffersNotConfigured(lock, kOutputPort);
    // This does count as a mid-stream output config change, even when this is
    // at the start of a stream - it's still while a stream is active, and still
    // prevents this stream from outputting any data to the Codec client until
    // the Codec client re-configures output while this stream is active.
    GenerateAndSendNewOutputConfig(lock, true);

    // Now we can wait for the client to catch up to the current output config
    // or for the client to tell the server to discard the current stream.
    while (!stream_->future_discarded() && !IsOutputConfiguredLocked()) {
      wake_stream_control_condition_.wait(lock);
    }

    if (stream_->future_discarded()) {
      // A discarded stream isn't an error for the CodecImpl instance.
      return true;
    }
  }

  // Now we have input configured, and output configured if needed by the core
  // codec, so we can move the core codec to running state.
  {  // scope unlock
    ScopedUnlock unlock(lock);
    CoreCodecStartStream();
  }  // ~unlock

  // Track this so the core codec doesn't have to bother with "ensure"
  // semantics, just start/stop, where stop isn't called unless the core codec
  // has a started stream.
  is_core_codec_stream_started_ = true;

  return true;
}

void CodecImpl::EnsureStreamClosed(std::unique_lock<std::mutex>& lock) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  // Stop the core codec, by using this thread to directly drive the core codec
  // from running to stopped (if not already stopped).  We do this first so the
  // core codec won't try to send us output while we have no stream at the Codec
  // layer.
  if (is_core_codec_stream_started_) {
    {  // scope unlock
      ScopedUnlock unlock(lock);
      CoreCodecStopStream();
    }
    is_core_codec_stream_started_ = false;
  }

  // Now close the old stream at the Codec layer.
  EnsureCodecStreamClosedLockedInternal();

  FXL_DCHECK((stream_lifetime_ordinal_ % 2 == 0) &&
             "expecting no current stream");
  FXL_DCHECK(!stream_);
}

// The only valid caller of this is EnsureStreamClosed().  We have this in a
// separate method only to make it easier to assert a couple things in the
// caller.
void CodecImpl::EnsureCodecStreamClosedLockedInternal() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  if (stream_lifetime_ordinal_ % 2 == 0) {
    // Already closed.
    return;
  }
  FXL_DCHECK(stream_queue_.front()->stream_lifetime_ordinal() ==
             stream_lifetime_ordinal_);
  stream_ = nullptr;
  stream_queue_.pop_front();
  stream_lifetime_ordinal_++;
  // Even values mean no current stream.
  FXL_DCHECK(stream_lifetime_ordinal_ % 2 == 0);
}

// This is called on Output ordering domain (FIDL thread) any time a message is
// received which would be able to start a new stream.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordainal_.
bool CodecImpl::EnsureFutureStreamSeenLocked(uint64_t stream_lifetime_ordinal) {
  if (future_stream_lifetime_ordinal_ == stream_lifetime_ordinal) {
    return true;
  }
  if (stream_lifetime_ordinal < future_stream_lifetime_ordinal_) {
    FailLocked("stream_lifetime_ordinal went backward - exiting\n");
    return false;
  }
  FXL_DCHECK(stream_lifetime_ordinal > future_stream_lifetime_ordinal_);
  if (future_stream_lifetime_ordinal_ % 2 == 1) {
    if (!EnsureFutureStreamCloseSeenLocked(future_stream_lifetime_ordinal_)) {
      return false;
    }
  }
  future_stream_lifetime_ordinal_ = stream_lifetime_ordinal;
  stream_queue_.push_back(std::make_unique<Stream>(stream_lifetime_ordinal));
  if (stream_queue_.size() > kMaxInFlightStreams) {
    FailLocked(
        "kMaxInFlightStreams reached - clients capable of causing this are "
        "instead supposed to wait/postpone to prevent this from occurring - "
        "exiting\n");
    return false;
  }
  return true;
}

// This is called on Output ordering domain (FIDL thread) any time a message is
// received which would close a stream.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordainal_.
bool CodecImpl::EnsureFutureStreamCloseSeenLocked(
    uint64_t stream_lifetime_ordinal) {
  if (future_stream_lifetime_ordinal_ % 2 == 0) {
    // Already closed.
    if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_ - 1) {
      FailLocked(
          "CloseCurrentStream() seen with stream_lifetime_ordinal != "
          "most-recent seen stream");
      return false;
    }
    return true;
  }
  if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_) {
    FailLocked("attempt to close a stream other than the latest seen stream");
    return false;
  }
  FXL_DCHECK(stream_lifetime_ordinal == future_stream_lifetime_ordinal_);
  FXL_DCHECK(stream_queue_.size() >= 1);
  Stream* closing_stream = stream_queue_.back().get();
  FXL_DCHECK(closing_stream->stream_lifetime_ordinal() ==
             stream_lifetime_ordinal);
  // It is permitted to see a FlushCurrentStream() before a CloseCurrentStream()
  // and this can make sense if a client just wants to inform the server of all
  // stream closes, or if the client wants to release_input_buffers or
  // release_output_buffers after the flush is done.
  //
  // If we didn't previously flush, then this close is discarding.
  if (!closing_stream->future_flush_end_of_stream()) {
    closing_stream->SetFutureDiscarded();
  }
  future_stream_lifetime_ordinal_++;
  FXL_DCHECK(future_stream_lifetime_ordinal_ % 2 == 0);
  return true;
}

// This is called on Output ordering domain (FIDL thread) any time a flush is
// seen.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordainal_.
bool CodecImpl::EnsureFutureStreamFlushSeenLocked(
    uint64_t stream_lifetime_ordinal) {
  if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_) {
    FailLocked("FlushCurrentStream() stream_lifetime_ordinal inconsistent");
    return false;
  }
  FXL_DCHECK(stream_queue_.size() >= 1);
  Stream* flushing_stream = stream_queue_.back().get();
  // Thanks to the above future_stream_lifetime_ordinal_ check, we know the
  // future stream is not discarded yet.
  FXL_DCHECK(!flushing_stream->future_discarded());
  if (flushing_stream->future_flush_end_of_stream()) {
    FailLocked("FlushCurrentStream() used twice on same stream");
    return false;
  }

  // We don't future-verify that we have a QueueInputEndOfStream(). We'll verify
  // that later when StreamControl catches up to this stream.

  // Remember the flush so we later know that a close doesn't imply discard.
  flushing_stream->SetFutureFlushEndOfStream();

  // A FlushEndOfStreamAndCloseStream() is also a close, after the flush.  This
  // keeps future_stream_lifetime_ordinal_ consistent.
  if (!EnsureFutureStreamCloseSeenLocked(stream_lifetime_ordinal)) {
    return false;
  }
  return true;
}

// This method is only called when buffer_constraints_action_required will be
// true in an OnOutputConfig() message sent shortly after this method call.
//
// Even if the client is switching streams rapidly without configuring output,
// this method and GenerateAndSendNewOutputConfig() with
// buffer_constraints_action_required true always run in pairs.
//
// This is what starts the interval during which
// OmxTryRecycleOutputPacketLocked() won't call OMX.
//
// If the client is in the middle of configuring output, we'll start ignoring
// the client's messages re. the old buffer_lifetime_ordinal and old
// buffer_constraints_version_ordinal until the client catches up to the new
// last_required_buffer_constraints_version_ordinal_[kOutputPort].
void CodecImpl::StartIgnoringClientOldOutputConfigLocked() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);

  // The buffer_lifetime_ordinal_[kOutputPort] can be even on entry due to at
  // least two cases: 0, and when the client is switching streams repeatedly
  // without setting a new buffer_lifetime_ordinal_[kOutputPort].
  if (buffer_lifetime_ordinal_[kOutputPort] % 2 == 1) {
    FXL_DCHECK(buffer_lifetime_ordinal_[kOutputPort] % 2 == 1);
    FXL_DCHECK(buffer_lifetime_ordinal_[kOutputPort] ==
               port_settings_[kOutputPort]->buffer_lifetime_ordinal);
    buffer_lifetime_ordinal_[kOutputPort]++;
    FXL_DCHECK(buffer_lifetime_ordinal_[kOutputPort] % 2 == 0);
    FXL_DCHECK(buffer_lifetime_ordinal_[kOutputPort] ==
               port_settings_[kOutputPort]->buffer_lifetime_ordinal + 1);
  }

  // When buffer_constraints_action_required true, we can assert in
  // GenerateAndSendNewOutputConfig() that this value is still the
  // next_output_buffer_constraints_version_ordinal_ in that method.
  last_required_buffer_constraints_version_ordinal_[kOutputPort] =
      next_output_buffer_constraints_version_ordinal_;
}

void CodecImpl::GenerateAndSendNewOutputConfig(
    std::unique_lock<std::mutex>& lock,
    bool buffer_constraints_action_required) {
  // When client action is required, this can only happen on the StreamControl
  // ordering domain.  When client action is not required, it can happen from
  // the InputData ordering domain.
  FXL_DCHECK(buffer_constraints_action_required &&
                 thrd_current() == stream_control_thread_ ||
             !buffer_constraints_action_required &&
                 IsPotentiallyCoreCodecThread());

  uint64_t current_stream_lifetime_ordinal = stream_lifetime_ordinal_;
  uint64_t new_output_buffer_constraints_version_ordinal =
      next_output_buffer_constraints_version_ordinal_++;
  uint64_t new_output_format_details_version_ordinal =
      next_output_format_details_version_ordinal_++;

  // If buffer_constraints_action_required true, the caller bumped the
  // last_required_buffer_constraints_version_ordinal_[kOutputPort] before
  // calling this method (using StartIgnoringClientOldOutputConfigLocked()), to
  // ensure any output config messages from the client are ignored until the
  // client catches up to at least
  // last_required_buffer_constraints_version_ordinal_.
  FXL_DCHECK(!buffer_constraints_action_required ||
             (last_required_buffer_constraints_version_ordinal_[kOutputPort] ==
              new_output_buffer_constraints_version_ordinal));

  // printf("GenerateAndSendNewOutputConfig
  // new_output_buffer_constraints_version_ordinal: %lu
  // buffer_constraints_action_required: %d\n",
  // new_output_buffer_constraints_version_ordinal,
  // buffer_constraints_action_required);

  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig> output_config;
  {  // scope unlock
    ScopedUnlock unlock(lock);
    // Don't call the core codec under the lock_, because we can avoid doing so,
    // and to allow the core codec to use this thread to call back into
    // CodecImpl using this stack if needed.  So far we don't have any actual
    // known examples of a core codec using this thread to call back into
    // CodecImpl using this stack.
    output_config = CoreCodecBuildNewOutputConfig(
        current_stream_lifetime_ordinal,
        new_output_buffer_constraints_version_ordinal,
        new_output_format_details_version_ordinal,
        buffer_constraints_action_required);
  }  // ~unlock
  // We only call GenerateAndSendNewOutputConfig() from contexts that won't be
  // changing the stream_lifetime_ordinal_, so the fact that we released the
  // lock above doesn't mean the stream_lifetime_ordinal_ could have changed, so
  // we can assert here that it's still the same as above.
  FXL_DCHECK(current_stream_lifetime_ordinal == stream_lifetime_ordinal_);

  output_config_ = std::move(output_config);

  // Stay under lock after setting output_config_, to get proper ordering of
  // sent messages even if a hostile client deduces the content of this message
  // before we've sent it and manages to get the server to send another
  // subsequent OnOutputConfig().

  FXL_DCHECK(sent_buffer_constraints_version_ordinal_[kOutputPort] + 1 ==
             new_output_buffer_constraints_version_ordinal);
  FXL_DCHECK(sent_format_details_version_ordinal_[kOutputPort] + 1 ==
             new_output_format_details_version_ordinal);

  // Setting this within same lock hold interval as we queue the message to be
  // sent in order vs. other OnOutputConfig() messages.  This way we can verify
  // that the client's incoming messages are not trying to configure with
  // respect to a buffer_constraints_version_ordinal that is newer than we've
  // actually sent the client.
  sent_buffer_constraints_version_ordinal_[kOutputPort] =
      new_output_buffer_constraints_version_ordinal;
  sent_format_details_version_ordinal_[kOutputPort] =
      new_output_format_details_version_ordinal;

  // Intentional copy of fuchsia::mediacodec::OutputConfig output_config_ here,
  // as we want output_config_ to remain valid (at least for debugging reasons
  // for now).
  PostToSharedFidl(
      [this, output_config = fidl::Clone(*output_config_)]() mutable {
        binding_.events().OnOutputConfig(std::move(output_config));
      });
}

void CodecImpl::onStreamFailed_StreamControl(uint64_t stream_lifetime_ordinal) {
  // When we come in here, we've just landed on the StreamControl domain, but
  // nothing has stopped the client from moving on to a new stream before we got
  // here.  The core codec should refuse to process any more stream data of the
  // failed stream, so it's reasonable to just ignore any stale stream failures,
  // since the stream failure would only result in the client moving on to a new
  // stream anyway, so if that's already happened we can ignore the old stream
  // failure.
  //
  // We prefer to check the state of things on the StreamControl domain since
  // this domain is in charge of stream transitions, so it's the easiest to
  // reason about why checking here is safe.  It would probably also be possible
  // to check robustly on the Output ordering domain (fidl_thread()) and
  // avoid creating any invalid message orderings, but checking here is more
  // obviously ok.
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      // This CodecImpl is already stopping due to a previous FailLocked(),
      // which will result in the Codec channel getting closed soon.  So don't
      // send OnStreamFailed().
      return;
    }
    FXL_DCHECK(stream_lifetime_ordinal <= stream_lifetime_ordinal_);
    if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
      // ignore - old stream is already gone - core codec is already moved on
      // from the old stream, and the client has already moved on also.  No
      // point in telling the client about the failure of an old stream that the
      // client has moved on from already.
      return;
    }
    FXL_DCHECK(stream_lifetime_ordinal == stream_lifetime_ordinal_);
    // We're failing the current stream.  We should still queue to the output
    // ordering domain to ensure ordering vs. any previously-sent output on this
    // stream that was sent directly from codec processing thread.
    //
    // This failure is async, in the sense that the client may still be sending
    // input data, and the core codec is expected to just hold onto those
    // packets until the client has moved on from this stream.
    printf("onStreamFailed_StreamControl() - stream_lifetime_ordinal: %lu\n",
           stream_lifetime_ordinal);
    if (!is_on_stream_failed_enabled_) {
      FailLocked(
          "onStreamFailed_StreamControl() with a client that didn't send "
          "EnableOnStreamFailed(), so closing the Codec channel instead.");
      return;
    }
    // There's not actually any need to track that the stream failed anywhere
    // in the CodecImpl.  The client needs to move on from the failed
    // stream to a new stream, or close the Codec channel.
    PostToSharedFidl([this, stream_lifetime_ordinal] {
      binding_.events().OnStreamFailed(stream_lifetime_ordinal);
    });
  }  // ~lock
}

void CodecImpl::MidStreamOutputConfigChange(uint64_t stream_lifetime_ordinal) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
      // ignore; The omx_meh_output_buffer_constraints_version_ordinal_ took
      // care of it.
      return;
    }
    FXL_DCHECK(stream_lifetime_ordinal == stream_lifetime_ordinal_);

    // Now we need to start disabling the port, wait for buffers to come back
    // from OMX, free buffer headers, wait for the port to become fully
    // disabled, unilaterally de-configure output buffers, demand a new output
    // config from the client, wait for the client to configure output (but be
    // willing to bail on waiting for the client if we notice future stream
    // discard), re-enable the output port, allocate headers, wait for the port
    // to be fully enabled, call FillThisBuffer() on the protocol-free buffers.

    // This is what starts the interval during which
    // OmxTryRecycleOutputPacketLocked() won't call OMX, and the interval during
    // which we'll ingore any in-progress client output config until the client
    // catches up.
    StartIgnoringClientOldOutputConfigLocked();

    {  // scope unlock
      ScopedUnlock unlock(lock);
      CoreCodecMidStreamOutputBufferReConfigPrepare();
    }  // ~unlock

    EnsureBuffersNotConfigured(lock, kOutputPort);

    GenerateAndSendNewOutputConfig(lock, true);

    // Now we can wait for the client to catch up to the current output config
    // or for the client to tell the server to discard the current stream.
    while (!stream_->future_discarded() && !IsOutputConfiguredLocked()) {
      wake_stream_control_condition_.wait(lock);
    }

    if (stream_->future_discarded()) {
      // We already know how to handle this case, and
      // core_codec_meh_output_buffer_constraints_version_ordinal_ is still set
      // such that the client will be forced to re-configure output buffers at
      // the start of the new stream.
      return;
    }
  }  // ~lock

  CoreCodecMidStreamOutputBufferReConfigFinish();

  VLOGF("Done with mid-stream format change.\n");
}

thrd_t CodecImpl::fidl_thread() {
  return device_->driver()->shared_fidl_thread();
}

void CodecImpl::SendFreeInputPacketLocked(
    fuchsia::mediacodec::CodecPacketHeader header) {
  // We allow calling this method on StreamControl or InputData ordering domain.
  // Because the InputData ordering domain thread isn't visible to this code,
  // if this isn't the StreamControl then we can only assert that this thread
  // isn't the FIDL thread, because we know the codec's InputData thread isn't
  // the FIDL thread.
  FXL_DCHECK(thrd_current() == stream_control_thread_ ||
             thrd_current() != fidl_thread());
  // We only send using fidl_thread().
  PostToSharedFidl([this, header = std::move(header)] {
    binding_.events().OnFreeInputPacket(std::move(header));
  });
}

bool CodecImpl::IsInputConfiguredLocked() {
  return IsPortConfiguredCommonLocked(kInputPort);
}

bool CodecImpl::IsOutputConfiguredLocked() {
  return IsPortConfiguredCommonLocked(kOutputPort);
}

bool CodecImpl::IsPortConfiguredCommonLocked(CodecPort port) {
  // In addition to what we're able to assert here, when
  // is_port_configured_[port], the core codec also has the port
  // configured.
  FXL_DCHECK(!is_port_configured_[port] ||
             port_settings_[port] &&
                 all_buffers_[port].size() ==
                     BufferCountFromPortSettings(*port_settings_[port]));
  return is_port_configured_[port];
}

void CodecImpl::Fail(const char* format, ...) {
  va_list args;
  va_start(args, format);
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    vFailLocked(false, format, args);
  }  // ~lock
  // "this" can be deallocated by this point (as soon as ~lock above).
  va_end(args);
}

void CodecImpl::FailLocked(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vFailLocked(false, format, args);
  va_end(args);
  // At this point know "this" is still allocated only because we still hold
  // lock_.  As soon as lock_ is released by the caller, "this" can immediately
  // be deallocated by another thread, if this isn't currently the
  // fidl_thread().
}

void CodecImpl::FailFatalLocked(const char* format, ...) {
  va_list args;
  va_start(args, format);
  // This doesn't return.
  vFailLocked(true, format, args);
  va_end(args);
}

void CodecImpl::vFail(bool is_fatal, const char* format, va_list args) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    vFailLocked(false, format, args);
  }  // ~lock
}

// Only meant to be called from Fail() and FailLocked().  Only meant to be
// called for async failure cases after was_logically_bound_ has become true.
// Failures before that point are handled separately.
void CodecImpl::vFailLocked(bool is_fatal, const char* format, va_list args) {
  // TODO(dustingreen): Send epitaph when possible.

  // Let's not have a buffer on the stack, not because it couldn't be done
  // safely, but because we'd potentially run into stack size vs. message length
  // tradeoffs, stack expansion granularity fun, or whatever else.

  va_list args2;
  va_copy(args2, args);

  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;

  // ~buffer never actually runs since this method never returns
  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  size_t buffer_bytes_2 =
      vsnprintf(buffer.get(), buffer_bytes, format, args2) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  FXL_DCHECK(buffer_bytes == buffer_bytes_2);
  va_end(args2);

  // TODO(dustingreen): It might be worth wiring this up to the log in a more
  // official way, especially if doing so would print a timestamp automatically
  // and/or provide filtering goodness etc.
  const char* message =
      is_fatal ? "devhost will fail" : "Codec channel will close async";
  printf("%s  --  %s\n", buffer.get(), message);

  // TODO(dustingreen): Send string in buffer via epitaph, when possible.  First
  // we should switch to events so we'll only have the Codec channel not the
  // CodecEvents channel. Note to self: The channel failing server-side may race
  // with trying to send.

  if (is_fatal) {
    fxl::BreakDebugger();
    exit(-1);
  } else {
    UnbindLocked();
  }

  // At this point we know "this" is still allocated only because we still hold
  // lock_.  As soon as lock_ is released by the caller, "this" can immediately
  // be deallocated by another thread, if this isn't currently the
  // fidl_thread().
}

void CodecImpl::PostSerial(async_dispatcher_t* async, fit::closure to_run) {
  device_->driver()->PostSerial(async, std::move(to_run));
}

void CodecImpl::PostToSharedFidl(fit::closure to_run) {
  // Re-posting to fidl_thread() is potentially problematic because of
  // how CodecImpl::UnbindLocked() relies on re-posting itself to run "delete
  // this" after any other work posted to fidl_thread() previously - that
  // only works if re-posts to the fidl_thread() aren't allowed.
  FXL_DCHECK(thrd_current() != fidl_thread());
  device_->driver()->PostToSharedFidl(std::move(to_run));
}

void CodecImpl::PostToStreamControl(fit::closure to_run) {
  device_->driver()->PostSerial(stream_control_loop_.dispatcher(),
                                std::move(to_run));
}

bool CodecImpl::IsStoppingLocked() { return was_unbind_started_; }

bool CodecImpl::IsStopping() {
  std::lock_guard<std::mutex> lock(lock_);
  return IsStoppingLocked();
}

// true - maybe it's the core codec thread.
// false - it's definitely not the core codec thread.
bool CodecImpl::IsPotentiallyCoreCodecThread() {
  // FXL_CHECK(false) << "not yet implemented";
  return (thrd_current() != stream_control_thread_) &&
         (thrd_current() != fidl_thread());
}

void CodecImpl::HandlePendingInputFormatDetails() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  const fuchsia::mediacodec::CodecFormatDetails* input_details = nullptr;
  if (stream_->input_format_details()) {
    input_details = stream_->input_format_details();
  } else {
    input_details = initial_input_format_details_;
  }
  FXL_DCHECK(input_details);
  CoreCodecQueueInputFormatDetails(*input_details);
}

void CodecImpl::onCoreCodecFailCodec(const char* format, ...) {
  std::string local_format =
      std::string("onCoreCodecFailCodec() called -- ") + format;
  va_list args;
  va_start(args, format);
  vFail(false, local_format.c_str(), args);
  // "this" can be deallocated by this point (as soon as ~lock above).
  va_end(args);
}

void CodecImpl::onCoreCodecFailStream() {
  // To recover, we need to get over to StreamControl domain, and we do
  // care whether the stream is the same stream as when this error was
  // delivered.  For this snap of the stream_lifetime_ordinal to be
  // meaningful we rely on the core codec only calling this method when there's
  // an active stream.
  uint64_t stream_lifetime_ordinal;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    stream_lifetime_ordinal = stream_lifetime_ordinal_;
  }  // ~lock
  PostToStreamControl([this, stream_lifetime_ordinal] {
    onStreamFailed_StreamControl(stream_lifetime_ordinal);
  });
}

void CodecImpl::onCoreCodecMidStreamOutputConfigChange(
    bool output_re_config_required) {
  // For now, the core codec thread is the only thread this gets called from.
  FXL_DCHECK(IsPotentiallyCoreCodecThread());
  // For a OMX_EventPortSettingsChanged that doesn't demand output buffer
  // re-config before more output data, this translates to an ordered emit
  // of a no-action-required OnOutputConfig() that just updates to the new
  // format, without demanding output buffer re-config.  HDR info can be
  // conveyed this way, ordered with respect to output frames.  OMX
  // requires that we use this thread to collect OMX format info during
  // EventHandler().
  if (!output_re_config_required) {
    std::unique_lock<std::mutex> lock(lock_);
    GenerateAndSendNewOutputConfig(
        lock,
        false);  // buffer_constraints_action_required
    return;
  }

  // We have an OMX_EventPortSettingsChanged that does demand output
  // buffer re-config before more output data.
  FXL_DCHECK(output_re_config_required);

  // We post over to StreamControl domain because we need to synchronize
  // with any changes to stream state that might be driven by the client.
  // When we get over there to StreamControl, we'll check if we're still
  // talking about the same stream_lifetime_ordinal, and if not, we ignore
  // the event, because a new stream may or may not have the same output
  // settings, and we'll be re-generating an OnOutputConfig() as needed
  // from current/later OMX output config anyway.  Here are the
  // possibilities:
  //   * Prior to the client moving to a new stream, we process this event
  //     on StreamControl ordering domain and have bumped
  //     buffer_lifetime_ordinal by the time we start any subsequent
  //     new stream from the client, which means we'll require the client
  //     to catch up to the new buffer_lifetime_ordinal before we start
  //     that new stream.
  //   * The client moves to a new stream before this event gets over to
  //     StreamControl.  In this case we ignore the event on StreamControl
  //     domain since its stale by that point, but instead we use
  //     omx_meh_output_buffer_constraints_version_ordinal_ to cause the
  //     client's next stream to start with a new OnOutputConfig() that
  //     the client must catch up to before the stream can fully start.
  //     This way we know we're not ignoring a potential change to
  //     nBufferCountMin or anything like that.
  uint64_t local_stream_lifetime_ordinal;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // This part is not speculative.  OMX has indicated that it's at least
    // meh about the current output config, so ensure we do a required
    // OnOutputConfig() before the next stream starts, even if the client
    // moves on to a new stream such that the speculative part below becomes
    // stale.
    core_codec_meh_output_buffer_constraints_version_ordinal_ =
        port_settings_[kOutputPort]
            ? port_settings_[kOutputPort]->buffer_constraints_version_ordinal
            : 0;
    // Speculative part - this part is speculative, in that we don't know if
    // this post over to StreamControl will beat any client driving to a new
    // stream.  So we snap the stream_lifetime_ordinal so we know whether to
    // ignore the post once it reaches StreamControl.
    local_stream_lifetime_ordinal = stream_lifetime_ordinal_;
  }  // ~lock
  PostToStreamControl(
      [this, stream_lifetime_ordinal = local_stream_lifetime_ordinal] {
        MidStreamOutputConfigChange(stream_lifetime_ordinal);
      });
}

void CodecImpl::onCoreCodecInputPacketDone(const CodecPacket* packet) {
  // Free/busy coherency from Codec interface to OMX doesn't involve trusting
  // the client, so assert we're doing it right server-side.
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    FXL_DCHECK(!all_packets_[kInputPort][packet->packet_index()]->is_free());
    all_packets_[kInputPort][packet->packet_index()]->SetFree(true);
    SendFreeInputPacketLocked(fuchsia::mediacodec::CodecPacketHeader{
        .buffer_lifetime_ordinal = packet->buffer_lifetime_ordinal(),
        .packet_index = packet->packet_index()});
  }  // ~lock
}

void CodecImpl::onCoreCodecOutputPacket(CodecPacket* packet,
                                        bool error_detected_before,
                                        bool error_detected_during) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    all_packets_[kOutputPort][packet->packet_index()]->SetFree(false);
    FXL_DCHECK(packet->has_start_offset());
    FXL_DCHECK(packet->has_valid_length_bytes());
    // packet->has_timestamp_ish() is optional even if
    // promise_separate_access_units_on_input is true.  When
    // !has_timestamp_ish(), timestamp_ish() returns kTimsestampIshNotSet which
    // is what we want, so no need to redundantly check has_timestamp_ish()
    // here.  We do want to enforce that the client gets no set timestamp_ish
    // values if the client didn't promise_separate_access_units_on_input.
    bool has_timestamp_ish =
        decoder_params_->promise_separate_access_units_on_input &&
        packet->has_timestamp_ish();
    uint64_t timestamp_ish = has_timestamp_ish ? packet->timestamp_ish() : 0;
    PostToSharedFidl(
        [this,
         p =
             fuchsia::mediacodec::CodecPacket{
                 .header.buffer_lifetime_ordinal =
                     packet->buffer_lifetime_ordinal(),
                 .header.packet_index = packet->packet_index(),
                 .stream_lifetime_ordinal = stream_lifetime_ordinal_,
                 .start_offset = packet->start_offset(),
                 .valid_length_bytes = packet->valid_length_bytes(),
                 .has_timestamp_ish = has_timestamp_ish,
                 .timestamp_ish = timestamp_ish,
                 // TODO(dustingreen): These two "true" values should be fine
                 // for decoders, but need to revisit here for encoders.
                 .start_access_unit = decoder_params_ ? true : false,
                 .known_end_access_unit = decoder_params_ ? true : false,
             },
         error_detected_before, error_detected_during] {
          binding_.events().OnOutputPacket(std::move(p), error_detected_before,
                                           error_detected_during);
        });
  }  // ~lock
}

void CodecImpl::onCoreCodecOutputEndOfStream(bool error_detected_before) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    VLOGF("sending OnOutputEndOfStream()\n");
    PostToSharedFidl([this, stream_lifetime_ordinal = stream_lifetime_ordinal_,
                      error_detected_before] {
      binding_.events().OnOutputEndOfStream(stream_lifetime_ordinal,
                                            error_detected_before);
    });
  }  // ~lock
}

CodecImpl::Stream::Stream(uint64_t stream_lifetime_ordinal)
    : stream_lifetime_ordinal_(stream_lifetime_ordinal) {
  // nothing else to do here
}

uint64_t CodecImpl::Stream::stream_lifetime_ordinal() {
  return stream_lifetime_ordinal_;
}

void CodecImpl::Stream::SetFutureDiscarded() {
  FXL_DCHECK(!future_discarded_);
  future_discarded_ = true;
}

bool CodecImpl::Stream::future_discarded() { return future_discarded_; }

void CodecImpl::Stream::SetFutureFlushEndOfStream() {
  FXL_DCHECK(!future_flush_end_of_stream_);
  future_flush_end_of_stream_ = true;
}

bool CodecImpl::Stream::future_flush_end_of_stream() {
  return future_flush_end_of_stream_;
}

CodecImpl::Stream::~Stream() {
  VLOGF("~Stream() stream_lifetime_ordinal: %lu\n", stream_lifetime_ordinal_);
}

void CodecImpl::Stream::SetInputFormatDetails(
    std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
        input_format_details) {
  // This is allowed to happen multiple times per stream.
  input_format_details_ = std::move(input_format_details);
}

const fuchsia::mediacodec::CodecFormatDetails*
CodecImpl::Stream::input_format_details() {
  return input_format_details_.get();
}

void CodecImpl::Stream::SetOobConfigPending(bool pending) {
  // SetOobConfigPending(true) is legal regardless of current state, but
  // SetOobConfigPending(false) is only legal if the state is currently true.
  FXL_DCHECK(pending || oob_config_pending_);
  oob_config_pending_ = pending;
}

bool CodecImpl::Stream::oob_config_pending() { return oob_config_pending_; }

void CodecImpl::Stream::SetInputEndOfStream() {
  FXL_DCHECK(!input_end_of_stream_);
  input_end_of_stream_ = true;
}

bool CodecImpl::Stream::input_end_of_stream() { return input_end_of_stream_; }

void CodecImpl::Stream::SetOutputEndOfStream() {
  FXL_DCHECK(!output_end_of_stream_);
  output_end_of_stream_ = true;
}

bool CodecImpl::Stream::output_end_of_stream() { return output_end_of_stream_; }

//
// CoreCodec wrappers, for the asserts.  These asserts, and the way we ensure
// at compile time that this class has a method for every method of
// CodecAdapter, are essentially costing a double vtable call instead of a
// single vtable call.  If we don't like that at some point, we can remove the
// private CodecAdapter inheritance from CodecImpl and have these be normal
// methods instead of virtual methods.
//

void CodecImpl::CoreCodecInit(const fuchsia::mediacodec::CodecFormatDetails&
                                  initial_input_format_details) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecInit(initial_input_format_details);
}

void CodecImpl::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  FXL_DCHECK(port == kInputPort && thrd_current() == stream_control_thread_ ||
             port == kOutputPort && thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecAddBuffer(port, buffer);
}

void CodecImpl::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  FXL_DCHECK(port == kInputPort && thrd_current() == stream_control_thread_ ||
             port == kOutputPort && thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecConfigureBuffers(port, packets);
}

void CodecImpl::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  FXL_DCHECK(port == kInputPort && thrd_current() == stream_control_thread_ ||
             port == kOutputPort && (thrd_current() == fidl_thread() ||
                                     thrd_current() == stream_control_thread_));
  codec_adapter_->CoreCodecEnsureBuffersNotConfigured(port);
}

void CodecImpl::CoreCodecStartStream() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecStartStream();
}

void CodecImpl::CoreCodecQueueInputFormatDetails(
    const fuchsia::mediacodec::CodecFormatDetails&
        per_stream_override_format_details) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecQueueInputFormatDetails(
      per_stream_override_format_details);
}

void CodecImpl::CoreCodecQueueInputPacket(const CodecPacket* packet) {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecQueueInputPacket(packet);
}

void CodecImpl::CoreCodecQueueInputEndOfStream() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecQueueInputEndOfStream();
}

void CodecImpl::CoreCodecStopStream() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecStopStream();
}

bool CodecImpl::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  FXL_DCHECK(thrd_current() == fidl_thread() ||
             thrd_current() == stream_control_thread_);
  return codec_adapter_->IsCoreCodecRequiringOutputConfigForFormatDetection();
}

// Caller must ensure that this is called only on one thread at a time, only
// during setup, during a core codec initiated mid-stream format change, or
// during stream start before any input data has been delivered for the new
// stream.
std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
CodecImpl::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  FXL_DCHECK(IsPotentiallyCoreCodecThread() ||
             thrd_current() == stream_control_thread_);
  return codec_adapter_->CoreCodecBuildNewOutputConfig(
      stream_lifetime_ordinal, new_output_buffer_constraints_version_ordinal,
      new_output_format_details_version_ordinal,
      buffer_constraints_action_required);
}

void CodecImpl::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecMidStreamOutputBufferReConfigPrepare();
}

void CodecImpl::CoreCodecMidStreamOutputBufferReConfigFinish() {
  FXL_DCHECK(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecMidStreamOutputBufferReConfigFinish();
}

void CodecImpl::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  FXL_DCHECK(thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecRecycleOutputPacket(packet);
}
