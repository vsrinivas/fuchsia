// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/defer.h>
#include <lib/fit/optional.h>
#include <lib/media/codec_impl/codec_impl.h>
#include <lib/media/codec_impl/codec_vmo_range.h>
#include <lib/media/codec_impl/log.h>
#include <lib/syslog/cpp/macros.h>
#include <threads.h>

#include <fbl/macros.h>

// "is_bound_checks" - In several lambdas that just send a message, we check
// is_bound() first, only because of ZX_POL_BAD_HANDLE ZX_POL_ACTION_EXCEPTION.
// If it weren't for that, we really wouldn't care about passing
// ZX_HANDLE_INVALID to zx_channel_write(), since the channel error handling is
// async (we Unbind(), sweep the in-proc send queue, and only then delete the
// Binding).

namespace {

constexpr bool kLogTimestampDelay = false;

// The protocol does not permit an unbounded number of in-flight streams, as
// that would potentially result in unbounded data queued in the incoming
// channel with no valid circuit-breaker value for the incoming channel data.
constexpr size_t kMaxInFlightStreams = 10;

class ScopedUnlock {
 public:
  explicit ScopedUnlock(std::unique_lock<std::mutex>& unique_lock) : unique_lock_(unique_lock) {
    unique_lock_.unlock();
  }
  ~ScopedUnlock() { unique_lock_.lock(); }

 private:
  std::unique_lock<std::mutex>& unique_lock_;
  ScopedUnlock() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedUnlock);
};

// Used within ScopedUnlock only.  Normally we'd just leave a std::unique_lock
// locked until it's destructed.
class ScopedRelock {
 public:
  explicit ScopedRelock(std::unique_lock<std::mutex>& unique_lock) : unique_lock_(unique_lock) {
    unique_lock_.lock();
  }
  ~ScopedRelock() { unique_lock_.unlock(); }

 private:
  std::unique_lock<std::mutex>& unique_lock_;
  ScopedRelock() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedRelock);
};

bool IsStreamErrorRecoverable(fuchsia::media::StreamError e) {
  using StreamError = fuchsia::media::StreamError;
  switch (e) {
    case StreamError::DECRYPTOR_NO_KEY:
      return true;
    default:
      return false;
  }
}

const char* ToString(fuchsia::media::StreamError e) {
  using StreamError = fuchsia::media::StreamError;
  switch (e) {
    case StreamError::UNKNOWN:
      return "UNKNOWN";
    case StreamError::INVALID_INPUT_FORMAT_DETAILS:
      return "INVALID_INPUT_FORMAT_DETAILS";
    case StreamError::INCOMPATIBLE_BUFFERS_PROVIDED:
      return "INCOMPATIBLE_BUFFERS_PROVIDED";
    case StreamError::EOS_PROCESSING:
      return "EOS_PROCESSING";
    case StreamError::DECODER_UNKNOWN:
      return "DECODER_UNKNOWN";
    case StreamError::DECODER_DATA_PARSING:
      return "DECODER_DATA_PARSING";
    case StreamError::ENCODER_UNKNOWN:
      return "ENCODER_UNKNOWN";
    case StreamError::DECRYPTOR_UNKNOWN:
      return "DECRYPTOR_UNKNOWN";
    case StreamError::DECRYPTOR_NO_KEY:
      return "DECRYPTOR_NO_KEY";
  }
}

const char* GetStreamErrorAdditionalHelpText(fuchsia::media::StreamError e) {
  using StreamError = fuchsia::media::StreamError;
  switch (e) {
    case StreamError::DECRYPTOR_NO_KEY:
      return "Retry after keys arrive.";
    default:
      return "";
  }
}

}  // namespace

CodecImpl::CodecImpl(fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem,
                     std::unique_ptr<CodecAdmission> codec_admission,
                     async_dispatcher_t* shared_fidl_dispatcher, thrd_t shared_fidl_thread,
                     StreamProcessorParams params,
                     fidl::InterfaceRequest<fuchsia::media::StreamProcessor> request)
    // The parameters to CodecAdapter constructor here aren't important.
    : CodecAdapter(lock_, this),
      codec_admission_(std::move(codec_admission)),
      shared_fidl_dispatcher_(shared_fidl_dispatcher),
      shared_fidl_thread_(shared_fidl_thread),
      shared_fidl_queue_(shared_fidl_dispatcher, shared_fidl_thread),
      params_(std::move(params)),
      tmp_sysmem_(std::move(sysmem)),
      tmp_interface_request_(std::move(request)),
      binding_(this),
      stream_control_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  ZX_DEBUG_ASSERT(tmp_sysmem_);
  ZX_DEBUG_ASSERT(tmp_interface_request_);

  if (codec_admission_)
    codec_admission_->SetChannelToWaitOn(tmp_interface_request_.channel());

  // If the fuchsia::sysmem::Allocator connection dies, so does this CodecImpl.
  sysmem_.set_error_handler([this](zx_status_t status) {
    // This handler can't run until after sysmem_ is bound.
    ZX_DEBUG_ASSERT(was_logically_bound_);
    this->Fail("CodecImpl sysmem_ channel failed");
  });

  // This is the binding_'s error handler, not the owner_error_handler_ which
  // is related but separate.
  binding_.set_error_handler([this](zx_status_t status) {
    // This handler can't run until after binding_ is bound.
    ZX_DEBUG_ASSERT(was_logically_bound_);
    Unbind();
  });

  initial_input_format_details_ = IsDecoder()   ? &decoder_params().input_details()
                                  : IsEncoder() ? &encoder_params().input_details()
                                                : &decryptor_params().input_details();
}

CodecImpl::~CodecImpl() {
  // We need ~binding_ to run on fidl_thread() else it's not safe to
  // un-bind unilaterally.  We could potentially relax this if BindAsync() was
  // never called, but for now we just require this always.
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());

  if (was_logically_bound_) {
    // Ensure that StreamControl is told to stop, which also stops InputData by
    // calling EnsureStreamClosed() as needed.
    Unbind();

    // Wait for StreamControl to be done.
    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);
      // Normally the fidl_thread() waiting for the StreamControl thread to do anything would be
      // bad, because the fidl_thread() is non-blocking and the StreamControl thread can block on
      // stuff, but StreamControl thread behavior after was_unbind_started_ = true and
      // wake_stream_control_condition_.notify_all() does not block and does not wait on
      // fidl_thread().  So in this case it's ok to wait here.
      while (!is_stream_control_done_) {
        stream_control_done_condition_.wait(lock);
      }
    }  // ~lock

    EnsureUnbindCompleted();
  }

  // Ensure the CodecAdmission is deleted entirely after ~this, including after any relevant base
  // class destructors have run.  This posted work may only get deleted, not run, since some
  // environments will Quit() their async::Loop shortly after ~CodecImpl.  So to avoid depending on
  // the destruction order of captures of a lambda, we use a fit::defer which will run it's lambda
  // when deleted.  In this lambda we can force ~CodecAdmission before ~zx::channel, and we know
  // this lambda will run, whether the lambda further down runs or is just deleted.
  auto run_when_deleted = fit::defer([codec_admission = std::move(codec_admission_),
                                      codec_to_close = std::move(codec_to_close_)]() mutable {
    // Ensure codec_to_close is destructed only after the codec_admission is destructed.  We have
    // to be fairly explicit about this since the order of lambda members is explicitly
    // unspecified in C++, so their destruction order is also unspecified.
    //
    // We care about the order because a client is fairly likely to immediately retry on seeing
    // the channel close, and we don't want that to ever bounce off the CodecAdmission for the
    // instance associated with that same channel.
    codec_admission = nullptr;

    // ~codec_to_close (after ~CodecAdmission above).
  });
  // We intentionally don't use shared_fidl_queue_ here.
  PostSerial(shared_fidl_dispatcher_, [run_when_deleted = std::move(run_when_deleted)] {
    // ~run_when_deleted will run the lambda above, whether run at the end of this
    // lambda, or when this lambda is deleted without ever having run during ~async::Loop
    // or async::Loop::Shutdown().
  });

  // Before destruction, we know that EnsureBuffersNotConfigured() got called for both input and
  // output, so we can assert that these are already not set during destruction.
  ZX_DEBUG_ASSERT(!fake_map_range_[kInputPort]);
  ZX_DEBUG_ASSERT(!fake_map_range_[kOutputPort]);
}

std::mutex& CodecImpl::lock() { return lock_; }

void CodecImpl::SetCoreCodecAdapter(std::unique_ptr<CodecAdapter> codec_adapter) {
  ZX_DEBUG_ASSERT(!codec_adapter_);
  codec_adapter_ = std::move(codec_adapter);
}

void CodecImpl::BindAsync(fit::closure error_handler) {
  // While it would potentially be safe to call Bind() from a thread other than
  // fidl_thread(), we have no reason to permit that.
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  // Up to once only.  No re-use.
  ZX_DEBUG_ASSERT(!was_bind_async_called_);
  ZX_DEBUG_ASSERT(!binding_.is_bound());
  ZX_DEBUG_ASSERT(tmp_interface_request_);
  was_bind_async_called_ = true;

  zx_status_t start_thread_result =
      stream_control_loop_.StartThread("StreamControl_loop", &stream_control_thread_);
  if (start_thread_result != ZX_OK) {
    // Handle the error async, to be consistent with later errors that must
    // occur async anyway.  Inability to start StreamControl is the only case
    // where we just allow the owner to "delete this" without using
    // UnbindLocked(), since UnbindLocked() relies on StreamControl.
    PostToSharedFidl(std::move(error_handler));
    return;
  }
  stream_control_queue_.SetDispatcher(stream_control_loop_.dispatcher(), stream_control_thread_);

  // From here on, we'll only fail the CodecImpl via UnbindLocked(), or by
  // just calling ~CodecImpl on the FIDL thread.
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

    CoreCodecSetSecureMemoryMode(kOutputPort, PortSecureMemoryMode(kOutputPort));
    CoreCodecSetSecureMemoryMode(kInputPort, PortSecureMemoryMode(kInputPort));

    if (IsCoreCodecHwBased(kInputPort) || IsCoreCodecHwBased(kOutputPort)) {
      core_codec_bti_ = CoreCodecBti();
    }

    // We touch FIDL stuff only from the fidl_thread().  While it would
    // be more efficient to post once to bind and send up to two messages below,
    // by posting individually we can share more code and have simpler rules for
    // calling that code.

    // Once this is posted, we can be dispatching incoming FIDL messages,
    // concurrent with the rest of the current lambda.  Aside from Sync(), most
    // of that dispatching would tend to land in FailLocked().  The concurrency
    // is just worth keeping in mind for the rest of the current lambda is all.
    PostToSharedFidl([this] {
      zx_status_t status = sysmem_.Bind(std::move(tmp_sysmem_), shared_fidl_dispatcher_);
      if (status != ZX_OK) {
        Fail("sysmem_.Bind() failed");
        return;
      }
      ZX_DEBUG_ASSERT(!tmp_sysmem_);

      status = binding_.Bind(std::move(tmp_interface_request_), shared_fidl_dispatcher_);
      if (status != ZX_OK) {
        Fail("binding_.Bind() failed");
        return;
      }
      ZX_DEBUG_ASSERT(!tmp_interface_request_);
    });

    input_constraints_ = CoreCodecBuildNewInputConstraints();

    ZX_DEBUG_ASSERT(input_constraints_);

    sent_buffer_constraints_version_ordinal_[kInputPort] =
        input_constraints_->buffer_constraints_version_ordinal();
    PostToSharedFidl([this] {
      // See "is_bound_checks" comment up top.
      if (binding_.is_bound()) {
        binding_.events().OnInputConstraints(fidl::Clone(*input_constraints_));
      }
    });
  });
}

void CodecImpl::EnableOnStreamFailed() {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  is_on_stream_failed_enabled_ = true;
}

void CodecImpl::AddInputBuffer_StreamControl(CodecBuffer::Info buffer_info,
                                             CodecVmoRange vmo_range) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  if (IsStopping()) {
    return;
  }
  // We must check, because __WARN_UNUSED_RESULT, and it's worth it for the
  // enforcement and consistency.
  if (!AddBufferCommon(std::move(buffer_info), std::move(vmo_range))) {
    return;
  }
}

void CodecImpl::SetInputBufferPartialSettings(
    fuchsia::media::StreamBufferPartialSettings input_settings) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  PostToStreamControl([this, input_settings = std::move(input_settings)]() mutable {
    SetInputBufferPartialSettings_StreamControl(std::move(input_settings));
  });
}

void CodecImpl::SetInputBufferPartialSettings_StreamControl(
    fuchsia::media::StreamBufferPartialSettings input_partial_settings) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!sysmem_) {
      FailLocked(
          "client sent SetInputBufferPartialSettings() to a CodecImpl that "
          "lacks sysmem_");
      return;
    }
    SetInputBufferSettingsCommon(lock, &input_partial_settings);
  }  // ~lock
}

void CodecImpl::SetInputBufferSettingsCommon(
    std::unique_lock<std::mutex>& lock,
    fuchsia::media::StreamBufferPartialSettings* input_partial_settings) {
  if (IsStoppingLocked()) {
    return;
  }
  if (IsStreamActiveLocked()) {
    FailLocked("client sent SetInputBuffer*Settings() with stream active");
    return;
  }
  SetBufferSettingsCommon(lock, kInputPort, input_partial_settings, *input_constraints_);
}

void CodecImpl::SetOutputBufferSettingsCommon(
    std::unique_lock<std::mutex>& lock,
    fuchsia::media::StreamBufferPartialSettings* output_partial_settings) {
  if (!output_constraints_) {
    // invalid client behavior
    //
    // client must have received at least the initial OnOutputConstraints()
    // first before sending SetOutputBufferSettings().
    FailLocked(
        "client sent SetOutputBufferSettings()/SetOutputBufferPartialSettings()"
        " when no output_constraints_");
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
  if (IsStreamActiveLocked() && IsOutputConfiguredLocked()) {
    FailLocked(
        "client sent SetOutputBufferSettings()/SetOutputBufferPartialSettings()"
        " with IsStreamActiveLocked() + already-fully-configured output");
    return;
  }

  SetBufferSettingsCommon(lock, kOutputPort, output_partial_settings,
                          output_constraints_->buffer_constraints());
}

void CodecImpl::AddOutputBufferInternal(CodecBuffer::Info buffer_info, CodecVmoRange vmo_range) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());

  bool output_buffers_done_configuring =
      AddBufferCommon(std::move(buffer_info), std::move(vmo_range));
  if (output_buffers_done_configuring) {
    // The StreamControl domain _might_ be waiting for output to be configured.
    wake_stream_control_condition_.notify_all();
  }
}

void CodecImpl::SetOutputBufferPartialSettings(
    fuchsia::media::StreamBufferPartialSettings output_partial_settings) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  VLOGF("CodecImpl::SetOutputBufferPartialSettings");
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!sysmem_) {
      FailLocked(
          "client sent SetOutputBufferPartialSettings() to a CodecImpl "
          "that lacks a sysmem_");
      return;
    }
    SetOutputBufferSettingsCommon(lock, &output_partial_settings);
  }  // ~lock
}

void CodecImpl::CompleteOutputBufferPartialSettings(uint64_t buffer_lifetime_ordinal) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (buffer_lifetime_ordinal % 2 == 0) {
      FailLocked(
          "CompleteOutputBufferPartialSettings client sent even "
          "buffer_lifetime_ordinal, but must be odd");
      return;
    }

    if (buffer_lifetime_ordinal != protocol_buffer_lifetime_ordinal_[kOutputPort]) {
      FailLocked("CompleteOutputBufferPartialSettings bad buffer_lifetime_ordinal");
      return;
    }

    // If the server is not interested in the client's buffer_lifetime_ordinal,
    // the client's buffer_lifetime_ordinal won't match the server's
    // buffer_lifetime_ordinal_.  The client will probably later catch up.
    if (buffer_lifetime_ordinal != buffer_lifetime_ordinal_[kOutputPort]) {
      // The case that ends up here is when a client's output configuration
      // (whole or last part) is being ignored because it's not yet caught up
      // with last_required_buffer_constraints_version_ordinal_.

      // Ignore the client's message.  The client will probably catch up later.
      return;
    }

    if (!IsPortBuffersAtLeastPartiallyConfiguredLocked(kOutputPort)) {
      FailLocked(
          "CompleteOutputBufferPartialSettings seen without prior "
          "SetOutputBufferPartialSettings");
      return;
    }

    if (port_settings_[kOutputPort]->is_complete_seen_output()) {
      FailLocked(
          "CompleteOutputBufferPartialSettings permitted exactly once "
          "after each SetOutputBufferPartialSettings");
      return;
    }

    // This will cause IsOutputConfiguredLocked() to start returning true.
    port_settings_[kOutputPort]->SetCompleteSeenOutput();
  }  // ~lock
  wake_stream_control_condition_.notify_all();
}

void CodecImpl::FlushEndOfStreamAndCloseStream(uint64_t stream_lifetime_ordinal) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
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

void CodecImpl::FlushEndOfStreamAndCloseStream_StreamControl(uint64_t stream_lifetime_ordinal) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
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
    ZX_DEBUG_ASSERT(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
    if (!IsStreamActiveLocked() || stream_lifetime_ordinal != stream_lifetime_ordinal_) {
      // TODO(dustingreen): epitaph
      FailLocked(
          "FlushEndOfStreamAndCloseStream() only valid on an active current "
          "stream (flush does not auto-create a new stream)");
      return;
    }
    // At this point we know that the stream is not discarded, and not already
    // flushed previously (because flush will discard the stream as there's
    // nothing more that the stream is permitted to do).
    ZX_DEBUG_ASSERT(IsStreamActiveLocked());
    ZX_DEBUG_ASSERT(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal);
    if (!stream_->input_end_of_stream()) {
      FailLocked(
          "FlushEndOfStreamAndCloseStream() is only permitted after "
          "QueueInputEndOfStream()");
      return;
    }
    while (!stream_->output_end_of_stream()) {
      if (stream_->failure_seen()) {
        return;
      }
      // While waiting, we'll continue to send OnOutputPacket(),
      // OnOutputConstraints(), and continue to process RecycleOutputPacket(),
      // until the client catches up to the latest config (as needed) and we've
      // started the send of output end_of_stream packet to the client.
      //
      // There is no way for the client to cancel a
      // FlushEndOfStreamAndCloseStream() short of closing the Codec channel.
      // Before long, the server will either send the OnOutputEndOfStream(), or
      // will send OnOmxStreamFailed(), or will close the Codec channel.  The
      // server must do one of those things before long (not allowed to get
      // stuck while flushing).
      //
      // Some core codecs have no way to report mid-stream input data corruption
      // errors or similar without it being a stream failure, so if there's any
      // stream error it turns into OnStreamFailed(). It's also permitted for a
      // server to set error_detected_ bool(s) on output packets and send
      // OnOutputEndOfStream() despite detected errors, but this is only a
      // reasonable behavior for the server if the server normally would detect
      // and report mid-stream input corruption errors without an
      // OnStreamFailed().
      // TODO(fxbug.dev/43490): Cancel wait immediately on failure without waiting for
      // timeout.
      if (std::cv_status::timeout ==
          output_end_of_stream_seen_.wait_until(
              lock, std::chrono::system_clock::now() + std::chrono::seconds(5))) {
        FailLocked("Timeout waiting for end of stream");
        break;
      }
    }

    // Now that flush is done, we close the current stream because there is not
    // any subsequent message for the current stream that's valid.
    EnsureStreamClosed(lock);
  }  // ~lock
}

void CodecImpl::CloseCurrentStream(uint64_t stream_lifetime_ordinal, bool release_input_buffers,
                                   bool release_output_buffers) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!EnsureFutureStreamCloseSeenLocked(stream_lifetime_ordinal)) {
      return;
    }
  }  // ~lock
  PostToStreamControl(
      [this, stream_lifetime_ordinal, release_input_buffers, release_output_buffers] {
        CloseCurrentStream_StreamControl(stream_lifetime_ordinal, release_input_buffers,
                                         release_output_buffers);
      });
}

void CodecImpl::CloseCurrentStream_StreamControl(uint64_t stream_lifetime_ordinal,
                                                 bool release_input_buffers,
                                                 bool release_output_buffers) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
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
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  // By posting to StreamControl ordering domain, we sync both Output ordering
  // domain (on fidl_thread()) and the StreamControl ordering domain.
  //
  // If the posted task doesn't run because stream_control_queue_.StopAndClear()
  // happened/happens, it doesn't matter because the whole channel will be
  // closing before long.
  //
  // The callback has affinity with fidl_thread(), including the destructor.
  // This is problematic with respect to the
  // stream_control_queue_.StopAndClear() called on StreamControl domain during
  // unbind. Without special handling, that StopAndClear() would try to delete
  // callback on the StreamControl domain instead of on the fidl_thread().  To
  // prevent that, we ensure that deletion of the lambda without running the
  // lambda will still post destruction of callback to fidl_thread(), and this
  // posting will queue before the lamda that runs
  // shared_fidl_queue_.StopAndClear().
  PostToStreamControl([this, callback_holder = ThreadSafeDeleter<SyncCallback>(
                                 &shared_fidl_queue_, std::move(callback))]() mutable {
    Sync_StreamControl(std::move(callback_holder));
  });
}

void CodecImpl::Sync_StreamControl(ThreadSafeDeleter<SyncCallback> callback_holder) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  if (IsStopping()) {
    // In this case, we rely on ThreadSafeDeleter to delete callback on fidl_thread().
    //
    // The response won't be sent, which is appropriate - the channel is getting closed soon
    // instead, and the client has to tolerate that.
    //
    // ~callback_holder
    return;
  }
  // We post back to FIDL thread to respond to ensure we're not racing with
  // channel close which could lead to attempting to send to handle value 0
  // which can cause process termination.  Also, because this fences
  // BufferAllocation clean close which itself is done async from StreamControl
  // to FIDL in some cases.
  PostToSharedFidl([this, callback_holder = std::move(callback_holder)]() mutable {
    ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
    // call the held callback
    callback_holder.held()();
  });
}

void CodecImpl::RecycleOutputPacket(fuchsia::media::PacketHeader available_output_packet) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  if (kLogTimestampDelay) {
    LOG(INFO, "RecycleOutputPacket");
  }
  CodecPacket* packet = nullptr;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!available_output_packet.has_buffer_lifetime_ordinal()) {
      FailLocked("output packet is missing buffer lifetime ordinal");
      return;
    }

    if (!CheckOldBufferLifetimeOrdinalLocked(kOutputPort,
                                             available_output_packet.buffer_lifetime_ordinal())) {
      return;
    }
    if (available_output_packet.buffer_lifetime_ordinal() < buffer_lifetime_ordinal_[kOutputPort]) {
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
    ZX_DEBUG_ASSERT(available_output_packet.buffer_lifetime_ordinal() ==
                    buffer_lifetime_ordinal_[kOutputPort]);
    if (!IsOutputConfiguredLocked()) {
      FailLocked(
          "client sent RecycleOutputPacket() for buffer_lifetime_ordinal that "
          "isn't fully configured yet - bad client behavior");
      return;
    }
    if (!available_output_packet.has_packet_index()) {
      FailLocked("output packet is missing packet index");
      return;
    }
    ZX_DEBUG_ASSERT(IsOutputConfiguredLocked());
    if (available_output_packet.packet_index() >= all_packets_[kOutputPort].size()) {
      FailLocked("out of range packet_index from client in RecycleOutputPacket()");
      return;
    }
    uint32_t packet_index = available_output_packet.packet_index();
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
    // timestamp_ish).  In addition to these parameters, a core codec can emit
    // output config changes via onCoreCodecMidStreamOutputConstraintsChange().
    packet = all_packets_[kOutputPort][packet_index].get();
    packet->ClearStartOffset();
    packet->ClearValidLengthBytes();
    packet->ClearTimestampIsh();
  }

  // Recycle to core codec.
  CoreCodecRecycleOutputPacket(packet);
}

void CodecImpl::QueueInputFormatDetails(uint64_t stream_lifetime_ordinal,
                                        fuchsia::media::FormatDetails format_details) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!EnsureFutureStreamSeenLocked(stream_lifetime_ordinal)) {
      return;
    }
  }  // ~lock

  if (!format_details.has_format_details_version_ordinal()) {
    Fail(
        "client QueueInputFormatDetails(): Format details have no version "
        "ordinal.");
    return;
  }

  PostToStreamControl(
      [this, stream_lifetime_ordinal, format_details = std::move(format_details)]() mutable {
        QueueInputFormatDetails_StreamControl(stream_lifetime_ordinal, std::move(format_details));
      });
}

// TODO(dustingreen): Need test coverage for this method, to cover at least
// the same format including OOB bytes as were specified during codec creation,
// and codec creation with no OOB bytes then this method setting OOB bytes (not
// the ideal client usage pattern in the long run since the CreateDecoder()
// might decline to provide a optimized but partial Codec implementation, but
// should be allowed nonetheless).
void CodecImpl::QueueInputFormatDetails_StreamControl(
    uint64_t stream_lifetime_ordinal, fuchsia::media::FormatDetails format_details) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);

  std::unique_lock<std::mutex> lock(lock_);
  if (IsStoppingLocked()) {
    return;
  }
  if (!CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal)) {
    return;
  }

  if (!CheckWaitEnsureInputConfigured(lock)) {
    ZX_DEBUG_ASSERT(IsStoppingLocked() || !stream_ || stream_->future_discarded());
    return;
  }

  ZX_DEBUG_ASSERT(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
  if (stream_lifetime_ordinal > stream_lifetime_ordinal_) {
    if (!StartNewStream(lock, stream_lifetime_ordinal)) {
      return;
    }
  }
  ZX_DEBUG_ASSERT(stream_lifetime_ordinal == stream_lifetime_ordinal_);
  if (stream_->input_end_of_stream()) {
    FailLocked("QueueInputFormatDetails() after QueueInputEndOfStream() unexpected");
    return;
  }
  if (stream_->future_discarded()) {
    // No reason to handle since the stream is future-discarded.
    return;
  }
  stream_->SetInputFormatDetails(
      std::make_unique<fuchsia::media::FormatDetails>(std::move(format_details)));
  // SetOobConfigPending(true) to ensure oob_config_pending() is true.
  //
  // This call is needed only to properly handle a call to
  // QueueInputFormatDetails() mid-stream.  For new streams that lack any calls
  // to QueueInputFormatDetails() before an input packet arrives, the
  // oob_config_pending() will already be true because it starts true for a new
  // stream.  For QueueInputFormatDetails() at the start of a stream before any
  // packets, oob_config_pending() will already be true.
  //
  // For decoders this is basically a pending oob_bytes.  For encoders
  // this pending config change can potentially include uncompressed format
  // details, if mid-stream format change is supported by the encoder.
  stream_->SetOobConfigPending(true);
}

void CodecImpl::QueueInputPacket(fuchsia::media::Packet packet) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }
    if (!packet.has_stream_lifetime_ordinal()) {
      FailLocked(
          "client QueueInputPacket() with packet that has no stream lifetime "
          "ordinal");
      return;
    }
    if (!EnsureFutureStreamSeenLocked(packet.stream_lifetime_ordinal())) {
      return;
    }
  }  // ~lock
  if (kLogTimestampDelay) {
    LOG(INFO, "input timestamp: has: %d value: 0x%" PRIx64, packet.has_timestamp_ish(),
        packet.has_timestamp_ish() ? packet.timestamp_ish() : 0);
  }
  PostToStreamControl([this, packet = std::move(packet)]() mutable {
    QueueInputPacket_StreamControl(std::move(packet));
  });
}

void CodecImpl::QueueInputPacket_StreamControl(fuchsia::media::Packet packet) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  ZX_DEBUG_ASSERT(packet.has_stream_lifetime_ordinal());

  if (!packet.has_header()) {
    Fail("client QueueInputPacket() with packet has no header");
    return;
  }
  fuchsia::media::PacketHeader temp_header_copy = fidl::Clone(packet.header());

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }

    // Unless we cancel this cleanup, we'll free the input packet back to the
    // client.
    auto send_free_input_packet_locked =
        fit::defer([this, header = std::move(temp_header_copy)]() mutable {
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

    if (!packet.header().has_buffer_lifetime_ordinal()) {
      FailLocked(
          "client QueueInputPacket() with header that has no buffer lifetime "
          "ordinal");
      return;
    }
    if (!CheckOldBufferLifetimeOrdinalLocked(kInputPort,
                                             packet.header().buffer_lifetime_ordinal())) {
      return;
    }

    if (!packet.has_stream_lifetime_ordinal()) {
      FailLocked("client QueueInputPacket() without packet stream_lifetime_ordinal.");
      return;
    }
    if (!CheckStreamLifetimeOrdinalLocked(packet.stream_lifetime_ordinal())) {
      return;
    }

    if (!CheckWaitEnsureInputConfigured(lock)) {
      ZX_DEBUG_ASSERT(IsStoppingLocked() || (stream_ && stream_->future_discarded()));
      return;
    }

    // For input, mid-stream config changes are not a thing and input buffers
    // are never unilaterally de-configured by the Codec server.
    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[kInputPort] ==
                    port_settings_[kInputPort]->buffer_lifetime_ordinal());

    // For this message we're strict re. buffer_lifetime_ordinal.
    //
    // In contrast to output, the server doesn't use even values to track config
    // changes that the client doesn't know about yet, since the server can't
    // unilaterally demand any changes to the input settings after initially
    // specifying the input constraints.
    //
    // One could somewhat-convincingly argue that this field in this particular
    // message is a bit pointless, but it might serve to detect client-side
    // bugs faster thanks to this check.
    if (packet.header().buffer_lifetime_ordinal() !=
        port_settings_[kInputPort]->buffer_lifetime_ordinal()) {
      FailLocked("client QueueInputPacket() with invalid buffer_lifetime_ordinal.");
      return;
    }

    ZX_DEBUG_ASSERT(packet.stream_lifetime_ordinal() >= stream_lifetime_ordinal_);

    if (packet.stream_lifetime_ordinal() > stream_lifetime_ordinal_) {
      // This case implicitly starts a new stream.  If the client wanted to
      // ensure that the old stream would be fully processed, the client would
      // have sent FlushEndOfStreamAndCloseStream() previously, whose
      // processing (previous to reaching here) takes care of the flush.
      //
      // Start a new stream, synchronously.
      if (!StartNewStream(lock, packet.stream_lifetime_ordinal())) {
        return;
      }
    }
    ZX_DEBUG_ASSERT(packet.stream_lifetime_ordinal() == stream_lifetime_ordinal_);

    if (!packet.header().has_packet_index()) {
      FailLocked("client QueueInputPacket() with packet has no packet index");
      return;
    }
    if (packet.header().packet_index() >= all_packets_[kInputPort].size()) {
      FailLocked(
          "client QueueInputPacket() with packet_index out of range - "
          "packet_index: %u size: %u",
          packet.header().packet_index(), all_packets_[kInputPort].size());
      return;
    }
    if (!packet.has_buffer_index()) {
      FailLocked("client QueueInputPacket() with packet has no buffer index");
      return;
    }
    if (packet.buffer_index() >= all_buffers_[kInputPort].size()) {
      FailLocked("client QueueInputPacket() with buffer_index out of range");
      return;
    }

    // Protocol check re. free/busy coherency.  This applies to packets only,
    // not buffers.
    if (!all_packets_[kInputPort][packet.header().packet_index()]->is_free()) {
      FailLocked("client QueueInputPacket() with packet_index !free");
      return;
    }

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

    all_packets_[kInputPort][packet.header().packet_index()]->SetFree(false);

    // Sending OnFreeInputPacket() will happen later instead, when the core
    // codec gives back the packet.
    send_free_input_packet_locked.cancel();
  }  // ~lock

  if (stream_->oob_config_pending()) {
    HandlePendingInputFormatDetails();
    stream_->SetOobConfigPending(false);
  }

  CodecPacket* core_codec_packet = all_packets_[kInputPort][packet.header().packet_index()].get();
  core_codec_packet->SetBuffer(all_buffers_[kInputPort][packet.buffer_index()].get());
  if (!packet.has_start_offset()) {
    Fail("client QueueInputPacket() with packet has no start offset");
    return;
  }
  core_codec_packet->SetStartOffset(packet.start_offset());
  if (!packet.has_valid_length_bytes()) {
    Fail("client QueueInputPacket() with packet has no valid length bytes");
    return;
  }
  core_codec_packet->SetValidLengthBytes(packet.valid_length_bytes());
  if (packet.has_timestamp_ish()) {
    core_codec_packet->SetTimstampIsh(packet.timestamp_ish());
  } else {
    core_codec_packet->ClearTimestampIsh();
  }

  if (core_codec_packet->valid_length_bytes() <= 0) {
    Fail("client QueueInputPacket() with valid_length_bytes 0 - not allowed");
    return;
  }
  if (core_codec_packet->start_offset() + core_codec_packet->valid_length_bytes() <
      core_codec_packet->start_offset()) {
    Fail("client QueueInputPacket() start_offset + valid_length_bytes overflow");
    return;
  }
  if (core_codec_packet->start_offset() + core_codec_packet->valid_length_bytes() >
      core_codec_packet->buffer()->size()) {
    Fail("client QueueInputPacket() with packet end > buffer size");
    return;
  }

  // Flush the data out to RAM if needed.
  if (IsCoreCodecHwBased(kInputPort) &&
      port_settings_[kInputPort]->coherency_domain() == fuchsia::sysmem::CoherencyDomain::CPU) {
    // This flushes only the portion of the buffer that the packet is
    // referencing.
    core_codec_packet->CacheFlush();
  }

  // We don't need to be under lock for this, because the fact that we're on the
  // StreamControl domain is enough to guarantee that any other control of the
  // core codec will occur after this.
  CoreCodecQueueInputPacket(core_codec_packet);
}

void CodecImpl::QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
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

void CodecImpl::QueueInputEndOfStream_StreamControl(uint64_t stream_lifetime_ordinal) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }
    if (!CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal)) {
      return;
    }

    if (!CheckWaitEnsureInputConfigured(lock)) {
      ZX_DEBUG_ASSERT(IsStoppingLocked() || (stream_ && stream_->future_discarded()));
      return;
    }

    ZX_DEBUG_ASSERT(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
    if (stream_lifetime_ordinal > stream_lifetime_ordinal_) {
      // We start a new stream given an end-of-stream for a stream we've not
      // seen before, since allowing empty streams to not be errors may be nicer
      // to use.
      if (!StartNewStream(lock, stream_lifetime_ordinal)) {
        return;
      }
    }

    if (stream_->input_end_of_stream()) {
      FailLocked("client already sent QueueInputEndOfStream() for this stream");
      return;
    }
    stream_->SetInputEndOfStream();

    if (stream_->future_discarded()) {
      // Don't queue to core codec.  The stream_ may have never fully started,
      // or may have been future-discarded since. Either way, skip queueing to
      // core codec. We only really must do this because the stream may not have
      // ever fully started, in the case where the client moves on to a new
      // stream before catching up to latest output config.
      return;
    }
  }  // ~lock

  CoreCodecQueueInputEndOfStream();
}

zx_status_t CodecImpl::Pin(uint32_t options, const zx::vmo& vmo, uint64_t offset, uint64_t size,
                           zx_paddr_t* addrs, size_t addrs_count, zx::pmt* pmt) {
  ZX_DEBUG_ASSERT(*core_codec_bti_);
  return core_codec_bti_->pin(options, vmo, offset, size, addrs, addrs_count, pmt);
}

bool CodecImpl::CheckWaitEnsureInputConfigured(std::unique_lock<std::mutex>& lock) {
  // Ensure/finish input configuration.
  if (!IsPortBuffersAtLeastPartiallyConfiguredLocked(kInputPort)) {
    FailLocked(
        "client QueueInput*() with input buffers not at least partially "
        "configured");
    return false;
  }
  ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[kInputPort] % 2 == 1);
  // The client is required to know that sysmem is in fact done allocating the
  // BufferCollection successfully before the client sends
  // QueueInput...StreamControl.  We can't trust a client to necessarily get
  // that right however, so rather than just getting stuck indefinitely in that
  // case, we detect by asking sysmem to verify that it has allocated the
  // BufferCollection successfully.  This verification happens async, but will
  // shortly cause WaitEnsureSysmemReadyOnInput() to return and
  // IsStoppingLocked() to return true if verification fails.
  if (!IsInputConfiguredLocked()) {
    PostToSharedFidl([this, buffer_lifetime_ordinal = buffer_lifetime_ordinal_[kInputPort]] {
      std::unique_lock<std::mutex> lock(lock_);
      if (IsStoppingLocked()) {
        return;
      }
      if (buffer_lifetime_ordinal != buffer_lifetime_ordinal_[kInputPort]) {
        // stale; no problem; old buffers were allocated fine and client already
        // moved on after that.
        return;
      }
      // Else previous buffer_lifetime_ordinal check would have noticed.
      ZX_DEBUG_ASSERT(port_settings_[kInputPort]);
      // paranoid check - assert above believed to be valid
      if (!port_settings_[kInputPort]) {
        return;
      }
      // Else IsStoppingLocked() check above would have returned.
      ZX_DEBUG_ASSERT(port_settings_[kInputPort]->buffer_collection().is_bound());
      // paranoid check - assert above believed to be valid
      if (!port_settings_[kInputPort]->buffer_collection().is_bound()) {
        return;
      }
      port_settings_[kInputPort]->buffer_collection()->CheckBuffersAllocated(
          [this, buffer_lifetime_ordinal](zx_status_t status) {
            std::unique_lock<std::mutex> lock(lock_);
            if (IsStoppingLocked()) {
              return;
            }
            if (buffer_lifetime_ordinal != buffer_lifetime_ordinal_[kInputPort]) {
              // stale; no problem; old buffers were allocated fine and client
              // already moved on after that.
              return;
            }
            if (status != ZX_OK) {
              // This will cause any in-progress WaitEnsureSysmemReadyOnInput()
              // to return shortly and IsStoppingLocked() will be true.
              FailLocked(
                  "Probably client did QueueInput* before the client "
                  "determined that sysmem was done successfully allocating "
                  "buffers after most recent SetInputBufferPartialSettings()");
              return;
            }
          });
    });
    if (!WaitEnsureSysmemReadyOnInput(lock)) {
      ZX_DEBUG_ASSERT(IsStoppingLocked());
      return false;
    }
  }
  if (!IsInputConfiguredLocked()) {
    FailLocked("client QueueInput*() with input buffers not configured");
    return false;
  }
  return true;
}

void CodecImpl::UnbindLocked() {
  // We must have first gotten far enough through BindAsync() before calling
  // UnbindLocked().
  ZX_DEBUG_ASSERT(was_logically_bound_);

  if (was_unbind_started_) {
    // Ignore the second trigger if we have a near-simultaneous failure from
    // StreamControl thread (for example) and from fidl_thread() (for
    // example).  The first will start unbinding, and the second will be
    // ignored.  Since completion of the Unbind() call doesn't imply anything
    // about how done the unbind is, there's no need for the second caller to
    // be blocked waiting for the first caller's unbind to be done.
    return;
  }

  if (codec_admission_)
    codec_admission_->SetCodecIsClosing();

  // Tell StreamControl to not start any more work.
  was_unbind_started_ = true;
  wake_stream_control_condition_.notify_all();

  // Unbind() / UnbindLocked() can be called from any thread.
  //
  // Regardless of what thread UnbindLocked() is called on, "this" will remain
  // allocated at least until the caller of UnbindLocked() releases lock_.
  //
  // In all cases, this posted lambda runs after BindAsync()'s work that's
  // posted to StreamControl, because any/all calls to UnbindLocked() happen
  // after BindAsync() has posted to StreamControl.
  //
  // We know the stream_control_queue_ isn't stopped yet, because the present
  // method is idempotent and the lambda being posted just below has the only
  // call to stream_control_queue_.StopAndClear().
  ZX_DEBUG_ASSERT(!stream_control_queue_.is_stopped());
  PostToStreamControl([this] {
    // At this point we know that no more streams will be started by
    // StreamControl ordering domain (thanks to was_unbind_started_ /
    // IsStoppingLocked() checks), but lambdas posted to the StreamControl
    // ordering domain (by the fidl_thread() or by core codec) may still
    // be creating other activity such as posting lambdas to StreamControl or
    // fidl_thread().
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
        EnsureBuffersNotConfigured(lock, kInputPort);
      }

      // Because the current path is the only path that sets this bool to true,
      // and the current path is run-once.
      ZX_DEBUG_ASSERT(!is_stream_control_done_);
      // Because stream_control_done_ is false, and ~CodecImpl waits for
      // is_stream_control_done_ true before shared_fidl_queue_.StopAndClear().
      ZX_DEBUG_ASSERT(!shared_fidl_queue_.is_stopped());

      // We do this from here so we know that this thread won't run any more
      // tasks after the currently-running task.
      //
      // The currently-running StreamControl task (this method) still gets to
      // run to completion.
      //
      // TODO(dustingreen): We probably could lean more heavily on this Quit()
      // and do less checking of IsStoppingLocked() in StreamControl tasks.
      // This TODO is not meant to imply that all current checking of
      // IsStoppingLocked() is ok to remove (less, not none).
      stream_control_loop_.Quit();

      // This deletes any further tasks already queued to StreamControl, and will immediately
      // delete any additional tasks that try to queue to StreamControl.  We also need to ensure the
      // first time stream_control_queue_.StopAndClear() runs is on stream_control_thread_, per
      // ClosureQueue's usage rules.
      stream_control_queue_.StopAndClear();

      // We're ready to let EnsureUnbindCompleted() and ~CodecImpl do the rest.
      //
      // The core codec has been stopped, so it has no current stream.  The core codec is required
      // to be delete-able when it has no current stream, and required not to asynchronously post
      // more work to the CodecImpl (because calling onCoreCodec... methods is not allowed when
      // there is no current stream).
      //
      // The binding_.Unbind() will run during EnsureUnbindCompleted() on the FIDL thread, so no
      // more FIDL dispatching to this CodecImpl after that.
      //
      // The stream_control_loop_.JoinThreads() will run during ~CodecImpl, so no more activity from
      // stream_control_thread_ after that.
      //
      // Anything posted using PostToSharedFidl() can be deleted instead of run since the whole
      // CodecImpl is going away, and shared_fidl_queue_ makes it safe for ~CodecImpl to complete
      // without needing to wait/fence past previously-posted labmdas to FIDL thread.
      is_stream_control_done_ = true;
      // Must notify_all() under lock_ in this case since ~CodecImpl can run as soon as
      // stream_control_done_ = true just above.
      stream_control_done_condition_.notify_all();

      // If we're not running from ~CodecImpl, we need to run the owner_error_handler_ on the FIDL
      // thread, which will in turn call ~CodecImpl.  If we are running from ~CodecImpl, then we're
      // already on the FIDL thread, and this posted work won't run thanks to shared_fidl_queue_
      // just deleting the posted task instead, in which case the owner_error_handler_ just gets
      // deleted instead of running (the usual semantics in response to unsolicited destruction).
      //
      // Must post under lock_ in this case else ~CodecImpl can have already finished as soon as
      // stream_control_done_ = true above.
      PostToSharedFidl([this, client_error_handler = std::move(owner_error_handler_)] {
        ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
        // We go ahead and finish up the un-binding aspects (because we can free up
        // resources prior to the client code potentially running ~CodecImpl async
        // later).
        //
        // However, this doesn't finish up aspects related to ordering release of
        // resources before acquisition of new resources.  In particular, this call
        // unbinds the channel, but intentionally doesn't close the channel itself until
        // after ~CodecImpl and after ~CodecAdmission.  The intent is to prevent the
        // possibility that overly-agressive client retries on channel closure by the
        // server could build up many CodecImpl instances, even if different instances
        // happen to use different FIDL threads (also potentially different than FIDL
        // thread on which a new CodecAdmission is created). By only closing the channel
        // itself as the last thing after all other cleanup is fully done, we don't
        // trigger the client to create a new CodecImpl while the old one still exists.
        EnsureUnbindCompleted();
        // This call is expected to run ~CodecImpl, either synchronously during this
        // call or shortly later async.
        client_error_handler();
      });
    }  // ~lock

    // "this" will be deleted shortly async when lambda posted just above runs, or we're returning
    // back to rest of ~CodecImpl, or ~CodecImpl is racing/running separately and completing
    // immediately after ~lock just above.  Regardless, done here.
    return;
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

void CodecImpl::EnsureUnbindCompleted() {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  ZX_DEBUG_ASSERT(was_logically_bound_);
  if (was_unbind_completed_) {
    return;
  }
  // Or will be, before this method returns.
  was_unbind_completed_ = true;

  // Unbind from the channel so we won't see any more incoming FIDL messages. This binding doesn't
  // own "this".
  //
  // The Unbind() stops any additional FIDL dispatching re. this CodecImpl.
  if (binding_.is_bound()) {
    codec_to_close_ = binding_.Unbind().TakeChannel();
  }

  // This isn't strictly necessary, but since we can potentially delete a queued
  // task here (before a client-called ~CodecImpl), we go ahead and do that now.
  //
  // This is partly a very minor potential resource deletion, and partly so we
  // get a nicer stack if anything should go wrong during that deletion; partly
  // so we get a nicer stack if somehow the JoinThreads() gets stuck (it
  // shouldn't since Quit() already happened).
  stream_control_loop_.JoinThreads();
  stream_control_loop_.Shutdown();

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    EnsureBuffersNotConfigured(lock, kOutputPort);

    // By this point both PortSettings should have already been deleted.
    ZX_DEBUG_ASSERT(!port_settings_[kInputPort]);
    ZX_DEBUG_ASSERT(!port_settings_[kOutputPort]);

    // Unbind the sysmem_ fuchsia::sysmem::Allocator connection - this also
    // ensures that any in-flight requests' completions will not run.
    sysmem_.Unbind();
  }  // ~lock

  // Any previously-posted tasks via shared_fidl_queue_ are deleted here without running.
  //
  // If we're shutting down because UnbindLocked() was run first upon discovery of an
  // internally-noticed error, then previously-queued sending of FIDL messages on the FIDL thread
  // already ran before the EnsureUnbindCompleted(), which was posted after the sends.
  //
  // If we're running ~CodecImpl because the client code is just deleting CodecImpl for whatever
  // client-initiated reason, then previously queueud sending of FIDL messages can be just deleted
  // here without the sends actually occurring, which is fine since in that case the client code
  // has no particular expectation that any particular messages were sent before deletion vs. not
  // getting sent due to deletion.
  shared_fidl_queue_.StopAndClear();
}

fuchsia::mediacodec::SecureMemoryMode CodecImpl::OutputSecureMemoryMode() {
  if (!IsDecoder() && !IsDecryptor()) {
    return fuchsia::mediacodec::SecureMemoryMode::OFF;
  }
  if (IsDecoder()) {
    if (!decoder_params().has_secure_output_mode()) {
      return fuchsia::mediacodec::SecureMemoryMode::OFF;
    }
    return decoder_params().secure_output_mode();
  } else {
    ZX_DEBUG_ASSERT(IsDecryptor());
    if (!decryptor_params().has_require_secure_mode()) {
      return fuchsia::mediacodec::SecureMemoryMode::OFF;
    }
    return decryptor_params().require_secure_mode() ? fuchsia::mediacodec::SecureMemoryMode::ON
                                                    : fuchsia::mediacodec::SecureMemoryMode::OFF;
  }
}

fuchsia::mediacodec::SecureMemoryMode CodecImpl::InputSecureMemoryMode() {
  if (!IsDecoder()) {
    return fuchsia::mediacodec::SecureMemoryMode::OFF;
  }
  if (!decoder_params().has_secure_input_mode()) {
    return fuchsia::mediacodec::SecureMemoryMode::OFF;
  }
  return decoder_params().secure_input_mode();
}

fuchsia::mediacodec::SecureMemoryMode CodecImpl::PortSecureMemoryMode(CodecPort port) {
  if (port == kOutputPort) {
    return OutputSecureMemoryMode();
  } else {
    ZX_DEBUG_ASSERT(port == kInputPort);
    return InputSecureMemoryMode();
  }
}

bool CodecImpl::IsPortSecureRequired(CodecPort port) {
  // Return false for DYNAMIC, if/when we add that.
  return PortSecureMemoryMode(port) == fuchsia::mediacodec::SecureMemoryMode::ON;
}

bool CodecImpl::IsPortSecurePermitted(CodecPort port) {
  // Return true for DYNAMIC, if/when we add that.
  return PortSecureMemoryMode(port) != fuchsia::mediacodec::SecureMemoryMode::OFF;
}

bool CodecImpl::IsStreamActiveLocked() {
  ZX_DEBUG_ASSERT(!!stream_ == (stream_lifetime_ordinal_ % 2 == 1));
  return !!stream_;
}

void CodecImpl::SetBufferSettingsCommon(
    std::unique_lock<std::mutex>& lock, CodecPort port,
    fuchsia::media::StreamBufferPartialSettings* partial_settings,
    const fuchsia::media::StreamBufferConstraints& stream_constraints) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());
  ZX_DEBUG_ASSERT(!IsStoppingLocked());

  if (!partial_settings->has_buffer_lifetime_ordinal()) {
    FailLocked("partial_settings do not have buffer lifetime ordinal");
    return;
  }
  if (!partial_settings->has_buffer_constraints_version_ordinal()) {
    FailLocked("partial_settings do not have buffer constraints version ordinal");
    return;
  }
  if (!partial_settings->has_sysmem_token() || !partial_settings->sysmem_token().is_valid()) {
    FailLocked("partial_settings missing valid sysmem_token");
    return;
  }

  ZX_DEBUG_ASSERT(
      !port_settings_[port] ||
      (buffer_lifetime_ordinal_[port] >= port_settings_[port]->buffer_lifetime_ordinal() &&
       buffer_lifetime_ordinal_[port] <= port_settings_[port]->buffer_lifetime_ordinal() + 1));

  // Extract buffer_lifetime_ordinal and buffer_constraints_version_ordinal from
  // StreamBufferPartialSettings
  // is providing.
  uint64_t buffer_lifetime_ordinal = partial_settings->buffer_lifetime_ordinal();

  uint64_t buffer_constraints_version_ordinal =
      partial_settings->buffer_constraints_version_ordinal();

  if (buffer_lifetime_ordinal <= protocol_buffer_lifetime_ordinal_[port]) {
    FailLocked(
        "buffer_lifetime_ordinal <= "
        "protocol_buffer_lifetime_ordinal_[port] - port: %d",
        port);
    return;
  }
  if (buffer_lifetime_ordinal % 2 == 0) {
    FailLocked(
        "Only odd values for buffer_lifetime_ordinal are permitted - port: %d "
        "value %lu",
        port, buffer_lifetime_ordinal);
    return;
  }
  protocol_buffer_lifetime_ordinal_[port] = buffer_lifetime_ordinal;

  if (buffer_constraints_version_ordinal > sent_buffer_constraints_version_ordinal_[port]) {
    FailLocked("Client sent too-new buffer_constraints_version_ordinal - port: %d", port);
    return;
  }

  if (buffer_constraints_version_ordinal <
      last_required_buffer_constraints_version_ordinal_[port]) {
    // ignore - client will probably catch up later
    return;
  }

  // We've peeled off too new and too old above.
  ZX_DEBUG_ASSERT(buffer_constraints_version_ordinal >=
                      last_required_buffer_constraints_version_ordinal_[port] &&
                  buffer_constraints_version_ordinal <=
                      sent_buffer_constraints_version_ordinal_[port]);

  // We've already checked above that the buffer_lifetime_ordinal is in
  // sequence.
  ZX_DEBUG_ASSERT(!port_settings_[port] ||
                  buffer_lifetime_ordinal > buffer_lifetime_ordinal_[port]);

  if (!ValidatePartialBufferSettingsVsConstraintsLocked(port, *partial_settings,
                                                        stream_constraints)) {
    // This assert is safe only because this thread still holds lock_.  This
    // is asserting that ValidateBufferSettingsVsConstraintsLocked() already
    // called FailLocked().
    ZX_DEBUG_ASSERT(IsStoppingLocked());
    return;
  }

  // Little if any reason to do this outside the lock.
  EnsureBuffersNotConfigured(lock, port);

  // This also starts the new buffer_lifetime_ordinal.
  {  // scope port_settings, to enforce not using it after we've moved it out
    std::unique_ptr<PortSettings> port_settings;
    port_settings = std::make_unique<PortSettings>(this, port, std::move(*partial_settings));
    port_settings_[port] = std::move(port_settings);
  }  // ~port_settings, which has been moved out, so we can't use it anyway
  buffer_lifetime_ordinal_[port] = port_settings_[port]->buffer_lifetime_ordinal();

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token =
      port_settings_[port]->TakeToken();
  // We intentionally don't want to hand the sysmem token directly to the core
  // codec, at least for now (maybe later it'll be necessary).
  ZX_DEBUG_ASSERT(!port_settings_[port]->partial_settings().has_sysmem_token());
  fuchsia::sysmem::BufferCollectionConstraints buffer_collection_constraints =
      [this, port, &lock, &stream_constraints]() {
        // port_settings_[port] can only change on this thread so are safe to
        // read outside the lock.
        ScopedUnlock unlock(lock);
        return CoreCodecGetBufferCollectionConstraints(port, stream_constraints,
                                                       port_settings_[port]->partial_settings());
      }();
  // The core codec doesn't fill out usage directly.  Instead we fill it out
  // here.
  if (!FixupBufferCollectionConstraintsLocked(port, stream_constraints,
                                              port_settings_[port]->partial_settings(),
                                              &buffer_collection_constraints)) {
    // FixupBufferCollectionConstraints() already called Fail().
    ZX_DEBUG_ASSERT(IsStoppingLocked());
    return;
  }
  // For output, the only reason we re-post here is to share the lock
  // acquisition code with input.
  PostToSharedFidl(
      [this, port, buffer_lifetime_ordinal = buffer_lifetime_ordinal_[port],
       token = std::move(token),
       buffer_collection_constraints = std::move(buffer_collection_constraints)]() mutable {
        std::lock_guard<std::mutex> lock(lock_);
        if (buffer_lifetime_ordinal != buffer_lifetime_ordinal_[port]) {
          return;
        }
        if (!sysmem_) {
          return;
        }
        if (IsStoppingLocked()) {
          return;
        }
        sysmem_->BindSharedCollection(
            std::move(token),
            port_settings_[port]->NewBufferCollectionRequest(shared_fidl_dispatcher_));
        port_settings_[port]->buffer_collection().set_error_handler(
            [this, port, buffer_lifetime_ordinal](zx_status_t status) {
              std::lock_guard<std::mutex> lock(lock_);
              if (buffer_lifetime_ordinal != buffer_lifetime_ordinal_[port]) {
                // It's fine if a BufferCollection fails after we're already
                // done using it.
                return;
              }
              // We're intentionally picky about the BufferCollection failing
              // too soon, as all clean closes should use Close(), which will
              // avoid causing this.  If we find a case where a client
              // legitimately needs to try one way then if that fails try
              // another way, we should see if we can avoid the need to do
              // that by expressing in sysmem constraints, or more likely just
              // accept that such a client will need to start with a new codec
              // instance for the 2nd try.
              UnbindLocked();
            });
        std::string buffer_name = codec_adapter_->CoreCodecGetName();
        switch (port) {
          case kInputPort:
            buffer_name += "Input";
            break;
          case kOutputPort:
            buffer_name += "Output";
            break;
          default:
            buffer_name += "Unknown";
            break;
        }
        port_settings_[port]->buffer_collection()->SetName(11, buffer_name);
        port_settings_[port]->buffer_collection()->SetDebugClientInfo(
            codec_adapter_->CoreCodecGetName(), 0);

        port_settings_[port]->buffer_collection()->SetConstraints(
            true, std::move(buffer_collection_constraints));

        port_settings_[port]->buffer_collection()->WaitForBuffersAllocated(
            [this, port, buffer_lifetime_ordinal](
                zx_status_t status,
                fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) mutable {
              OnBufferCollectionInfo(port, buffer_lifetime_ordinal, status,
                                     std::move(buffer_collection_info));
            });
      });
}

void CodecImpl::OnBufferCollectionInfo(
    CodecPort port, uint64_t buffer_lifetime_ordinal, zx_status_t status,
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());

  if (port == kInputPort) {
    PostSysmemCompletion([this, port, buffer_lifetime_ordinal, status,
                          buffer_collection_info = std::move(buffer_collection_info)]() mutable {
      OnBufferCollectionInfoInternal(port, buffer_lifetime_ordinal, status,
                                     std::move(buffer_collection_info));
    });
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    OnBufferCollectionInfoInternal(port, buffer_lifetime_ordinal, status,
                                   std::move(buffer_collection_info));
  }
}

void CodecImpl::OnBufferCollectionInfoInternal(
    CodecPort port, uint64_t buffer_lifetime_ordinal, zx_status_t allocate_status,
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      return;
    }

    // The buffer_lifetime_ordinal_[port] can only change on the current thread.
    if (buffer_lifetime_ordinal != buffer_lifetime_ordinal_[port]) {
      // stale response
      return;
    }
    if (allocate_status != ZX_OK) {
      FailLocked(
          "OnBufferCollectionInfoLocked() sees failure - port: %d "
          "allocate_status: %d",
          port, allocate_status);
      return;
    }
  }  // ~lock

  uint32_t buffer_count = buffer_collection_info.buffer_count;

  // This code trusts sysmem to really be sysmem and to behave correctly, but
  // doesn't hurt to double-check some things in debug build.
  ZX_DEBUG_ASSERT(buffer_count >= 1);
  ZX_DEBUG_ASSERT(buffer_count <= buffer_collection_info.buffers.size());
  // Spot check that the boundary between valid and invalid handles is where it
  // should be.
  ZX_DEBUG_ASSERT(buffer_collection_info.buffers[buffer_count - 1].vmo.is_valid());
  ZX_DEBUG_ASSERT(buffer_count == buffer_collection_info.buffers.size() ||
                  !buffer_collection_info.buffers[buffer_count].vmo.is_valid());

  // Let's move the VMO handles out first, so that the BufferCollectionInfo_2 we
  // send to the core codec doesn't have the VMO handles.  We want the core
  // codec to get its VMO handles via the CodecBuffer*(s) we'll provide shortly
  // below.
  zx::vmo vmos[buffer_collection_info.buffers.size()];
  for (uint32_t i = 0; i < buffer_count; ++i) {
    vmos[i] = std::move(buffer_collection_info.buffers[i].vmo);
    ZX_DEBUG_ASSERT(!buffer_collection_info.buffers[i].vmo.is_valid());
  }

  // Now we can tell the core codec about the collection info.  The core codec
  // can clone the FIDL struct if it wants, or can just copy out any info it
  // wants from specific fields.
  CoreCodecSetBufferCollectionInfo(port, buffer_collection_info);

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);

    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal == buffer_lifetime_ordinal_[port]);

    // The only way port_settings_[port] gets cleared is if
    // buffer_lifetime_ordinal changes.
    ZX_DEBUG_ASSERT(port_settings_[port]);

    // This completes the settings, analogous to having completed
    // SetInputBufferSettings()/SetOutputBufferSettings().
    port_settings_[port]->SetBufferCollectionInfo(std::move(buffer_collection_info));
  }

  if (IsPortSecureRequired(port) && !port_settings_[port]->is_secure()) {
    Fail("IsPortSecureRequired(port) && !port_settings_[port]->is_secure() - port: %d", port);
    return;
  }
  if (!IsPortSecurePermitted(port) && port_settings_[port]->is_secure()) {
    Fail("!IsPortSecurePermitted(port) && port_settings_[port]->is_secure() - port: %d", port);
    return;
  }

  ZX_DEBUG_ASSERT(!fake_map_range_[port]);
  if (port_settings_[port]->is_secure()) {
    if (IsCoreCodecMappedBufferUseful(port)) {
      zx_status_t status =
          FakeMapRange::Create(port_settings_[port]->vmo_usable_size(), &fake_map_range_[port]);
      if (status != ZX_OK) {
        Fail("FakeMapRange::Init() failed");
        return;
      }
    }
  }

  // We convert the buffer_collection_info into AddInputBuffer_StreamControl()
  // and AddOutputBufferInternal() calls, almost as if the client were adding
  // the buffers itself (but without the check that the client isn't adding
  // buffers itself while using sysmem).
  for (uint32_t i = 0; i < buffer_count; i++) {
    // While under the lock we'll move out the stuff we need into locals
    uint64_t vmo_usable_start = 0;
    uint64_t vmo_usable_size = 0;
    bool is_secure = false;
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);

      ZX_DEBUG_ASSERT(buffer_lifetime_ordinal == buffer_lifetime_ordinal_[port]);
      ZX_DEBUG_ASSERT(port_settings_[port]);

      vmo_usable_start = port_settings_[port]->vmo_usable_start(i);
      vmo_usable_size = port_settings_[port]->vmo_usable_size();
      is_secure = port_settings_[port]->is_secure();
    }  // ~lock

    CodecBuffer::Info buffer_info{.port = port,
                                  .lifetime_ordinal = buffer_lifetime_ordinal,
                                  .index = i,
                                  .is_secure = is_secure};
    CodecVmoRange vmo_range(std::move(vmos[i]), vmo_usable_start, vmo_usable_size);
    if (port == kInputPort) {
      AddInputBuffer_StreamControl(std::move(buffer_info), std::move(vmo_range));
    } else {
      ZX_DEBUG_ASSERT(port == kOutputPort);
      AddOutputBufferInternal(std::move(buffer_info), std::move(vmo_range));
    }
  }
}

void CodecImpl::EnsureBuffersNotConfigured(std::unique_lock<std::mutex>& lock, CodecPort port) {
  // This method can be called on input only if there's no current stream.
  //
  // On output, this method can be called if there's no current stream or if
  // we're in the middle of an output config change.
  //
  // On input, this can only be called on stream_control_thread_.
  //
  // On output, this can be called on stream_control_thread_ or fidl_thread().
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && (thrd_current() == stream_control_thread_ ||
                                          thrd_current() == fidl_thread()));

  is_port_buffers_configured_[port] = false;
  if (buffer_lifetime_ordinal_[port] % 2 == 1) {
    buffer_lifetime_ordinal_[port]++;
  }
  if (port_settings_[port]) {
    // This will close the BufferCollection (async as-needed) cleanly, without causing the
    // LogicalBufferCollection to fail.  Mainly we care so we can more easily tell during debugging
    // whether a LogicalBufferCollection was cleanly closed by all participants, vs. potentially
    // getting failed by a participant exiting or non-cleanly closing.  A Sync() by the client is
    // sufficient to ensure this async close is done.
    port_settings_[port] = nullptr;
  }

  // Ensure that buffers aren't with the core codec.
  {  // scope unlock
    ScopedUnlock unlock(lock);
    CoreCodecEnsureBuffersNotConfigured(port);
  }  // ~unlock

  // For mid-stream output config change, the caller is responsible for ensuring
  // that buffers are not with the HW first.
  //
  // TODO(dustingreen): Check anything relevant to buffers not presently being
  // with the HW.
  // ZX_DEBUG_ASSERT(all_packets_[port].empty() ||
  // !all_packets_[port][0]->is_with_hw());

  // This ~FakeMapRange (which calls zx::vmar::destroy()) is among the motivations for calling
  // EnsureBuffersNotConfigured() during the Unbind() sequence / during ~CodecImpl.
  if (fake_map_range_[port]) {
    fake_map_range_[port] = fit::nullopt;
  }

  all_packets_[port].clear();
  all_buffers_[port].clear();
  ZX_DEBUG_ASSERT(all_packets_[port].empty());
  ZX_DEBUG_ASSERT(all_buffers_[port].empty());
}

bool CodecImpl::ValidatePartialBufferSettingsVsConstraintsLocked(
    CodecPort port, const fuchsia::media::StreamBufferPartialSettings& partial_settings,
    const fuchsia::media::StreamBufferConstraints& constraints) {
  // Most of the constraints will be handled by telling sysmem about them, not
  // via the client, so there's not a ton to validate here.
  if ((partial_settings.has_single_buffer_mode() && partial_settings.single_buffer_mode()) &&
      !constraints.single_buffer_mode_allowed()) {
    FailLocked("single_buffer_mode && !single_buffer_mode_allowed");
    return false;
  }
  bool packet_count_needed =
      partial_settings.has_single_buffer_mode() && partial_settings.single_buffer_mode();
  ZX_DEBUG_ASSERT(partial_settings.sysmem_token().is_valid());
  if (packet_count_needed) {
    if (!partial_settings.has_packet_count_for_server()) {
      FailLocked("missing packet_count_for_server with single_buffer_mode true");
      return false;
    }
    if (!partial_settings.has_packet_count_for_client()) {
      FailLocked("missing packet_count_for_client with single_buffer_mode true");
      return false;
    }
  }
  // if needed or provided anyway
  if (partial_settings.has_packet_count_for_server()) {
    if (partial_settings.packet_count_for_server() > constraints.packet_count_for_server_max()) {
      FailLocked("packet_count_for_server > packet_count_for_server_max");
      return false;
    }
  }
  // if needed or provided anyway
  if (partial_settings.has_packet_count_for_client()) {
    if (partial_settings.packet_count_for_client() > constraints.packet_count_for_client_max()) {
      FailLocked("packet_count_for_client > packet_count_for_client_max");
      return false;
    }
  }
  return true;
}

bool CodecImpl::AddBufferCommon(CodecBuffer::Info buffer_info, CodecVmoRange vmo_range) {
  const CodecPort port = buffer_info.port;
  ZX_DEBUG_ASSERT(port == kInputPort && (thrd_current() == stream_control_thread_) ||
                  port == kOutputPort && (thrd_current() == fidl_thread()));
  bool buffers_done_configuring = false;

  std::unique_lock<std::mutex> lock(lock_);

  if (buffer_info.lifetime_ordinal % 2 == 0) {
    FailLocked("Client sent even buffer_lifetime_ordinal, but must be odd - exiting - port: %u\n",
               port);
    return false;
  }

  if (buffer_info.lifetime_ordinal != protocol_buffer_lifetime_ordinal_[port]) {
    FailLocked(
        "Incoherent SetOutputBufferSettings()/SetInputBufferSettings() + "
        "AddOutputBuffer()/AddInputBuffer()s - exiting - port: %d\n",
        port);
    return false;
  }

  // If the server is not interested in the client's buffer_lifetime_ordinal,
  // the client's buffer_lifetime_ordinal won't match the server's
  // buffer_lifetime_ordinal_.  The client will probably later catch up.
  if (buffer_info.lifetime_ordinal != buffer_lifetime_ordinal_[port]) {
    // The case that ends up here is when a client's output configuration
    // (whole or last part) is being ignored because it's not yet caught up
    // with last_required_buffer_constraints_version_ordinal_.

    // This case won't happen for input, at least for now.  This is an assert
    // rather than a client behavior check, because previous client protocol
    // checks have already peeled off any invalid client behavior that might
    // otherwise cause this assert to trigger.
    ZX_DEBUG_ASSERT(port == kOutputPort);

    // Ignore the client's message.  The client will probably catch up later.
    return false;
  }

  if (buffer_info.index != all_buffers_[port].size()) {
    FailLocked(
        "AddOutputBuffer()/AddInputBuffer() had buffer_index out of sequence "
        "- port: %d buffer_index: %u all_buffers_[port].size(): %lu",
        port, buffer_info.index, all_buffers_[port].size());
    return false;
  }

  uint32_t required_buffer_count = port_settings_[port]->buffer_count();
  if (buffer_info.index >= required_buffer_count) {
    FailLocked("AddOutputBuffer()/AddInputBuffer() extra buffer - port: %d", port);
    return false;
  }

  std::unique_ptr<CodecBuffer> local_buffer = std::unique_ptr<CodecBuffer>(
      new CodecBuffer(this, std::move(buffer_info), std::move(vmo_range)));

  if (IsCoreCodecMappedBufferUseful(port)) {
    if (fake_map_range_[port]) {
      // The fake_map_range_[port]->base() is % ZX_PAGE_SIZE == 0, which is the same as a mapping
      // would be.  There are sufficient virtual pages starting at FakeMapRange::base() to permit
      // CodecBuffer to include the low-order vmo_usable_start % ZX_PAGE_SIZE bits in
      // CodecBuffer::base(), for any vmo_usable_start() value (even the worst case of
      // ZX_PAGE_SIZE - 1, and buffer size % ZX_PAGE_SIZE == 2).  By including those low-order
      // intra-page-offset bits, we can treat non-secure and secure cases similarly.
      local_buffer->FakeMap(fake_map_range_[port]->base());
    } else {
      // So far, there's little reason to avoid doing the Map() part under the
      // lock, even if it can be a bit more time consuming, since there's no data
      // processing happening at this point anyway, and there wouldn't be any
      // happening in any other code location where we could potentially move the
      // Map() either.
      if (!local_buffer->Map()) {
        FailLocked("AddOutputBuffer()/AddInputBuffer() couldn't Map() new buffer - port: %d", port);
        return false;
      }
    }
  }

  // We keep the buffers pinned for DMA continuously, since there's not much benefit to un-pinning
  // and re-pinning them (so far).  By pinning, we prevent sysmem from recycling the
  // BufferCollection VMOs until the driver has re-started and un-quarantined pinned pages (via
  // its BTI), after ensuring the HW is no longer doing DMA from/to the pages.
  //
  // TODO(fxbug.dev/38650): All CodecAdapter(s) that start memory access that can continue beyond
  // VMO handle closure during process death/termination should have a BTI.  Resolving this TODO
  // will require updating at least the amlogic-video VP9 decoder to provide a BTI.
  //
  // TODO(fxbug.dev/38651): Currently OEMCrypto's indirect (via FIDL) SMC calls that take physical
  // addresses are not guaranteed to be fully over/done before VMO handles are auto-closed by
  // OEMCrypto assuming OEMCryto's process dies/terminates.
  if (IsCoreCodecHwBased(port) && *core_codec_bti_) {
    zx_status_t status = local_buffer->Pin();
    if (status != ZX_OK) {
      FailLocked("buffer->Pin() failed - status: %d port: %d", status, port);
      return false;
    }
  }

  {
    ScopedUnlock unlock(lock);
    // Inform the core codec up-front about each buffer.
    CoreCodecAddBuffer(port, local_buffer.get());
  }
  all_buffers_[port].push_back(std::move(local_buffer));
  if (all_buffers_[port].size() == required_buffer_count) {
    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[port] ==
                    port_settings_[port]->buffer_lifetime_ordinal());
    // Stash this while we can, before the client de-configures.
    last_provided_buffer_constraints_version_ordinal_[port] =
        port_settings_[port]->buffer_constraints_version_ordinal();
    // Now we allocate all_packets_[port].
    ZX_DEBUG_ASSERT(all_packets_[port].empty());
    uint32_t packet_count = port_settings_[port]->packet_count();
    for (uint32_t i = 0; i < packet_count; i++) {
      // Private constructor to prevent core codec maybe creating its own
      // Packet instances (which isn't the intent) seems worth the hassle of
      // not using make_unique<>() here.
      all_packets_[port].push_back(std::unique_ptr<CodecPacket>(
          new CodecPacket(port_settings_[port]->buffer_lifetime_ordinal(), i)));
    }

    {  // scope unlock
      ScopedUnlock unlock(lock);

      // A core codec can take action here to finish configuring buffers if
      // it's able, or can delay configuring buffers until
      // CoreCodecStartStream() or
      // CoreCodecMidStreamOutputBufferReConfigFinish() if that works better
      // for the core codec.
      //
      // In any case, during a mid-stream output constraints change, the core
      // codec must not call any onCoreCodecOutput* methods until the core
      // codec sees CoreCodecStopStream() (after stopping the stream, in
      // preparation for the next stream), or
      // CoreCodecMidStreamOutputBufferReConfigFinish().
      //
      // In other words, this call does /not/ imply un-pausing output.
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

    is_port_buffers_configured_[port] = true;
    buffers_done_configuring = true;

    // For client-called AddOutputBuffer(), the last buffer being added is
    // analogous to CompleteOutputBufferPartialSettings(); we handle that
    // analogous-ness in IsOutputConfiguredLocked() (not by pretending we got
    // a CompleteOutputBufferPartialSettings() here), so
    // is_port_buffers_configured_[port] = true above is enough to make
    // IsOutputConfiguredLocked() return true if this is a client-driven
    // AddOutputBuffer().
  }
  return buffers_done_configuring;
}

bool CodecImpl::CheckOldBufferLifetimeOrdinalLocked(CodecPort port,
                                                    uint64_t buffer_lifetime_ordinal) {
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

bool CodecImpl::CheckStreamLifetimeOrdinalLocked(uint64_t stream_lifetime_ordinal) {
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
  VLOGF("StartNewStream()");
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  ZX_DEBUG_ASSERT((stream_lifetime_ordinal % 2 == 1) && "new stream_lifetime_ordinal must be odd");

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
  ZX_DEBUG_ASSERT(!IsStreamActiveLocked());

  // Now it's time to start the new stream.  We start the new stream at
  // Codec layer first then core codec layer.

  if (!IsInputConfiguredLocked()) {
    FailLocked("input not configured before start of stream (QueueInputPacket())");
    return false;
  }

  ZX_DEBUG_ASSERT(stream_queue_.size() >= 1);
  ZX_DEBUG_ASSERT(stream_lifetime_ordinal == stream_queue_.front()->stream_lifetime_ordinal());
  stream_ = stream_queue_.front().get();
  // Update the stream_lifetime_ordinal_ to the new stream.  We need to do
  // this before we send new output config, since the output config will be
  // generated using the current stream ordinal.
  ZX_DEBUG_ASSERT(stream_lifetime_ordinal > stream_lifetime_ordinal_);
  stream_lifetime_ordinal_ = stream_lifetime_ordinal;
  ZX_DEBUG_ASSERT(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal_);

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
  // possibility here by forcing a client to catch up with the server, if
  // there's *any possibility* that the client might still be working on
  // catching up with the server.
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
  // Some core codecs may require the output to be configured to _something_ as
  // they don't support giving us the real output config unless the output is
  // configured to at least something at first.
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
  } else if (IsCoreCodecRequiringOutputConfigForFormatDetection() && !IsOutputConfiguredLocked()) {
    // The core codec requires output to be configured before format detection,
    // so we force the client to provide an output config before format
    // detection.
    is_new_config_needed = true;
  } else if (IsOutputConfiguredLocked() &&
             port_settings_[kOutputPort]->buffer_constraints_version_ordinal() <=
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
    StartIgnoringClientOldOutputConfig(lock);
    EnsureBuffersNotConfigured(lock, kOutputPort);
    // This does count as a mid-stream output config change, even when this is
    // at the start of a stream - it's still while a stream is active, and still
    // prevents this stream from outputting any data to the Codec client until
    // the Codec client re-configures output while this stream is active.
    GenerateAndSendNewOutputConstraints(lock, true);

    // Now we can wait for the client to catch up to the current output config
    // or for the client to tell the server to discard the current stream.
    while (!IsStoppingLocked() && !stream_->future_discarded() && !IsOutputConfiguredLocked()) {
      RunAnySysmemCompletionsOrWait(lock);
    }

    if (IsStoppingLocked()) {
      return false;
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
  VLOGF("EnsureStreamClosed()");
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  // Stop the core codec, by using this thread to directly drive the core codec
  // from running to stopped (if not already stopped).  We do this first so the
  // core codec won't try to send us output while we have no stream at the Codec
  // layer.
  if (is_core_codec_stream_started_) {
    {  // scope unlock
      ScopedUnlock unlock(lock);
      VLOGF("CoreCodecStopStream()...");
      CoreCodecStopStream();
      VLOGF("CoreCodecStopStream() done.");
    }
    is_core_codec_stream_started_ = false;
  }

  // Now close the old stream at the Codec layer.
  EnsureCodecStreamClosedLockedInternal();

  ZX_DEBUG_ASSERT(!IsStreamActiveLocked());
}

// The only valid caller of this is EnsureStreamClosed().  We have this in a
// separate method only to make it easier to assert a couple things in the
// caller.
void CodecImpl::EnsureCodecStreamClosedLockedInternal() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  if (stream_lifetime_ordinal_ % 2 == 0) {
    // Already closed.
    return;
  }
  ZX_DEBUG_ASSERT(stream_queue_.front()->stream_lifetime_ordinal() == stream_lifetime_ordinal_);
  stream_ = nullptr;
  stream_queue_.pop_front();
  stream_lifetime_ordinal_++;
  // Even values mean no current stream.
  ZX_DEBUG_ASSERT(stream_lifetime_ordinal_ % 2 == 0);
}

bool CodecImpl::RunAnySysmemCompletions(std::unique_lock<std::mutex>& lock) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  // Typically this loop will run once, but on return we want the queue to be
  // empty even if more showed up while in this method, for condition_variable
  // signalling reasons.
  bool any_ran = false;
  while (!sysmem_completion_queue_.empty()) {
    // We'll run them all, so extract all the items and run them all.
    std::queue<fit::closure> local_batch_to_run;
    local_batch_to_run.swap(sysmem_completion_queue_);
    // The unlock doesn't cause queue re-ordering, though so far none of these
    // items care anyway.
    ScopedUnlock unlock(lock);
    while (!local_batch_to_run.empty()) {
      any_ran = true;
      fit::closure to_run = std::move(local_batch_to_run.front());
      local_batch_to_run.pop();
      to_run();
    }
  }
  return any_ran;
}

void CodecImpl::PostSysmemCompletion(fit::closure to_run) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    sysmem_completion_queue_.emplace(std::move(to_run));
    // In case there is no WaitEnsureSysmemReadyOnInput(), we post to
    // StreamControl to ensure that RunAnySysmemCompletions() runs soon.
    // Don't let them accumulate though.
    if (!is_sysmem_runner_pending_) {
      is_sysmem_runner_pending_ = true;
      PostToStreamControl([this] {
        std::unique_lock<std::mutex> lock(lock_);
        RunAnySysmemCompletions(lock);
        ZX_DEBUG_ASSERT(sysmem_completion_queue_.empty());
        is_sysmem_runner_pending_ = false;
      });
    }
  }  // ~lock

  // In case to_run needs to get run by a QueueInput...StreamControl() method
  // via WaitEnsureSysmemReadyOnInput(), we wake the StreamControl thread.  We
  // must do this even if is_sysmem_runner_pending_, in case that runner won't
  // run for a while due to WaitEnsureSysmemReadyOnInput() blocking
  // StreamControl.
  wake_stream_control_condition_.notify_all();
}

bool CodecImpl::WaitEnsureSysmemReadyOnInput(std::unique_lock<std::mutex>& lock) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  // Input buffer re-config is not permitted unless there's no current stream.
  ZX_DEBUG_ASSERT(!IsStreamActiveLocked());
  while (!IsInputConfiguredLocked()) {
    RunAnySysmemCompletionsOrWait(lock);
    // No need to check for stream switch since it's not permitted for a client
    // to be sending any message that can cause a new stream until after the
    // client is done configuring input buffers (enforced elsewhere).
    if (IsStoppingLocked()) {
      return false;
    }
  }
  return true;
}

void CodecImpl::RunAnySysmemCompletionsOrWait(std::unique_lock<std::mutex>& lock) {
  // If any sysmem completions ran, we immediately return, so that conditions
  // can be checked again in the caller immediately.
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  bool any_completions_ran = RunAnySysmemCompletions(lock);
  ZX_DEBUG_ASSERT(sysmem_completion_queue_.empty());
  if (!any_completions_ran) {
    // We know sysmem_completion_queue_.empty() and the lock is held just before
    // this wait().
    wake_stream_control_condition_.wait(lock);
  }
}

// This is called on Output ordering domain (FIDL thread) any time a message is
// received which would be able to start a new stream.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordinal_.
bool CodecImpl::EnsureFutureStreamSeenLocked(uint64_t stream_lifetime_ordinal) {
  if (future_stream_lifetime_ordinal_ == stream_lifetime_ordinal) {
    return true;
  }
  if (stream_lifetime_ordinal < future_stream_lifetime_ordinal_) {
    FailLocked("stream_lifetime_ordinal went backward - exiting\n");
    return false;
  }
  ZX_DEBUG_ASSERT(stream_lifetime_ordinal > future_stream_lifetime_ordinal_);
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
// stream_queue_ and future_stream_lifetime_ordinal_.
bool CodecImpl::EnsureFutureStreamCloseSeenLocked(uint64_t stream_lifetime_ordinal) {
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
  ZX_DEBUG_ASSERT(stream_lifetime_ordinal == future_stream_lifetime_ordinal_);
  ZX_DEBUG_ASSERT(stream_queue_.size() >= 1);
  Stream* closing_stream = stream_queue_.back().get();
  ZX_DEBUG_ASSERT(closing_stream->stream_lifetime_ordinal() == stream_lifetime_ordinal);
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
  ZX_DEBUG_ASSERT(future_stream_lifetime_ordinal_ % 2 == 0);
  return true;
}

// This is called on Output ordering domain (FIDL thread) any time a flush is
// seen.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordinal_.
bool CodecImpl::EnsureFutureStreamFlushSeenLocked(uint64_t stream_lifetime_ordinal) {
  if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_) {
    FailLocked("FlushCurrentStream() stream_lifetime_ordinal inconsistent");
    return false;
  }
  ZX_DEBUG_ASSERT(stream_queue_.size() >= 1);
  Stream* flushing_stream = stream_queue_.back().get();
  // Thanks to the above future_stream_lifetime_ordinal_ check, we know the
  // future stream is not discarded yet.
  ZX_DEBUG_ASSERT(!flushing_stream->future_discarded());
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
// true in an OnOutputConstraints() message sent shortly after this method call.
//
// Even if the client is switching streams rapidly without configuring output,
// this method and GenerateAndSendNewOutputConstraints() with
// buffer_constraints_action_required true always run in pairs.
//
// If the client is in the middle of configuring output, we'll start ignoring
// the client's messages re. the old buffer_lifetime_ordinal and old
// buffer_constraints_version_ordinal until the client catches up to the new
// last_required_buffer_constraints_version_ordinal_[kOutputPort].
void CodecImpl::StartIgnoringClientOldOutputConfig(std::unique_lock<std::mutex>& lock) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);

  // The buffer_lifetime_ordinal_[kOutputPort] can be even on entry due to at
  // least two cases: 0, and when the client is switching streams repeatedly
  // without setting a new buffer_lifetime_ordinal_[kOutputPort].
  if (buffer_lifetime_ordinal_[kOutputPort] % 2 == 1) {
    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[kOutputPort] % 2 == 1);
    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[kOutputPort] ==
                    port_settings_[kOutputPort]->buffer_lifetime_ordinal());
    buffer_lifetime_ordinal_[kOutputPort]++;
    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[kOutputPort] % 2 == 0);
    ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[kOutputPort] ==
                    port_settings_[kOutputPort]->buffer_lifetime_ordinal() + 1);
  }

  // When buffer_constraints_action_required true, we can assert in
  // GenerateAndSendNewOutputConstraints() that this value is still the
  // next_output_buffer_constraints_version_ordinal_ in that method.
  last_required_buffer_constraints_version_ordinal_[kOutputPort] =
      next_output_buffer_constraints_version_ordinal_;

  // Now that we've stopped any new calls to CoreCodecRecycleOutputPacket(),
  // fence through any previously-started call to CoreCodecRecycleOutputPacket()
  // that maybe have been started previously, before returning from this method.
  //
  // We can't just be holding lock_ during the call to
  // CoreCodecRecycleOutputPacket() because it acquires the video_decoder_lock_
  // and in other paths the video_decoder_lock_ is held while acquiring lock_.
  //
  // It's ok for the StreamControl domain to wait on the Output domain (but not
  // the other way around).
  bool is_output_ordering_domain_done_with_recycle_output_packet = false;
  std::condition_variable condition_changed;
  PostToSharedFidl(
      [this, &is_output_ordering_domain_done_with_recycle_output_packet, &condition_changed] {
        {  // scope lock
          std::lock_guard<std::mutex> lock(lock_);
          is_output_ordering_domain_done_with_recycle_output_packet = true;
        }
        condition_changed.notify_all();
      });
  while (!is_output_ordering_domain_done_with_recycle_output_packet) {
    condition_changed.wait(lock);
  }
  ZX_DEBUG_ASSERT(is_output_ordering_domain_done_with_recycle_output_packet);
}

void CodecImpl::GenerateAndSendNewOutputConstraints(std::unique_lock<std::mutex>& lock,
                                                    bool buffer_constraints_action_required) {
  // When client action is required, this can only happen on the StreamControl
  // ordering domain.  When client action is not required, it can happen from
  // the InputData ordering domain.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required && thrd_current() == stream_control_thread_ ||
                  !buffer_constraints_action_required && IsPotentiallyCoreCodecThread());

  uint64_t current_stream_lifetime_ordinal = stream_lifetime_ordinal_;
  uint64_t new_output_buffer_constraints_version_ordinal =
      next_output_buffer_constraints_version_ordinal_++;

  // If buffer_constraints_action_required true, the caller bumped the
  // last_required_buffer_constraints_version_ordinal_[kOutputPort] before
  // calling this method (using StartIgnoringClientOldOutputConfig()), to
  // ensure any output config messages from the client are ignored until the
  // client catches up to at least
  // last_required_buffer_constraints_version_ordinal_.
  ZX_DEBUG_ASSERT(!buffer_constraints_action_required ||
                  (last_required_buffer_constraints_version_ordinal_[kOutputPort] ==
                   new_output_buffer_constraints_version_ordinal));

  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> output_constraints;
  {  // scope unlock
    ScopedUnlock unlock(lock);

    // Don't call the core codec under the lock_, because we can avoid doing so,
    // and to allow the core codec to use this thread to call back into
    // CodecImpl using this stack if needed.  So far we don't have any actual
    // known examples of a core codec using this thread to call back into
    // CodecImpl using this stack.
    output_constraints = CoreCodecBuildNewOutputConstraints(
        current_stream_lifetime_ordinal, new_output_buffer_constraints_version_ordinal,
        buffer_constraints_action_required);
  }  // ~unlock

  // We only call GenerateAndSendNewOutputConstraints() from contexts that won't
  // be changing the stream_lifetime_ordinal_, so the fact that we released the
  // lock above doesn't mean the stream_lifetime_ordinal_ could have changed, so
  // we can assert here that it's still the same as above.
  ZX_DEBUG_ASSERT(current_stream_lifetime_ordinal == stream_lifetime_ordinal_);

  output_constraints_ = std::move(output_constraints);

  // Stay under lock after setting output_constraints_, to get proper ordering
  // of sent messages even if a hostile client deduces the content of this
  // message before we've sent it and manages to get the server to send another
  // subsequent OnOutputConstraints().

  ZX_DEBUG_ASSERT(sent_buffer_constraints_version_ordinal_[kOutputPort] + 1 ==
                  new_output_buffer_constraints_version_ordinal);

  // Setting this within same lock hold interval as we queue the message to be
  // sent in order vs. other OnOutputConstraints() messages.  This way we can
  // verify that the client's incoming messages are not trying to configure with
  // respect to a buffer_constraints_version_ordinal that is newer than we've
  // actually sent the client.
  sent_buffer_constraints_version_ordinal_[kOutputPort] =
      new_output_buffer_constraints_version_ordinal;

  // Intentional copy of fuchsia::media::OutputConfig output_constraints_ here,
  // as we want output_constraints_ to remain valid (at least for debugging
  // reasons for now).
  PostToSharedFidl([this, output_constraints = fidl::Clone(*output_constraints_)]() mutable {
    // See "is_bound_checks" comment up top.
    if (binding_.is_bound()) {
      binding_.events().OnOutputConstraints(std::move(output_constraints));
    }
  });
}

void CodecImpl::MidStreamOutputConstraintsChange(uint64_t stream_lifetime_ordinal) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  VLOGF("CodecImpl::MidStreamOutputConstraintsChange - stream: %lu", stream_lifetime_ordinal);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    VLOGF("lock aquired 1");
    if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
      // ignore; The omx_meh_output_buffer_constraints_version_ordinal_ took
      // care of it.
      VLOGF("CodecImpl::MidStreamOutputConstraintsChange - stale stream");
      return;
    }
    ZX_DEBUG_ASSERT(stream_lifetime_ordinal == stream_lifetime_ordinal_);

    // We can work through the mid-stream output constraints change step by step
    // using this thread.

    // This is what starts the interval during which we'll ignore any
    // in-progress client output config until the client catches up.
    VLOGF("StartIngoringClientOldOutputConfig()...");
    StartIgnoringClientOldOutputConfig(lock);

    {  // scope unlock
      ScopedUnlock unlock(lock);
      VLOGF("CoreCodecMidStreamOutputBufferReConfigPrepare()...");
      CoreCodecMidStreamOutputBufferReConfigPrepare();
    }  // ~unlock

    VLOGF("EnsureBuffersNotConfigured()...");
    EnsureBuffersNotConfigured(lock, kOutputPort);

    VLOGF("GenerateAndSendNewOutputConstraints()...");
    GenerateAndSendNewOutputConstraints(lock, true);

    // Now we can wait for the client to catch up to the current output config
    // or for the client to tell the server to discard the current stream.
    VLOGF("RunAnySysmemCompletionsOrWait()...");
    while (!IsStoppingLocked() && !stream_->future_discarded() && !IsOutputConfiguredLocked()) {
      RunAnySysmemCompletionsOrWait(lock);
    }

    if (IsStoppingLocked()) {
      VLOGF("CodecImpl::MidStreamOutputConstraintsChange IsStoppingLocked()");
      return;
    }

    if (stream_->future_discarded()) {
      // We already know how to handle this case, and
      // core_codec_meh_output_buffer_constraints_version_ordinal_ is still set
      // such that the client will be forced to re-configure output buffers at
      // the start of the new stream.
      VLOGF("CodecImpl::MidStreamOutputConstraintsChange future_discarded()");
      return;
    }

    // For asserts.
    VLOGF("ClearMidStreamOutputConstraintsChangeActive()...");
    stream_->ClearMidStreamOutputConstraintsChangeActive();
  }  // ~lock

  VLOGF("CoreCodecMidStreamOutputBufferReConfigFinish()...");
  CoreCodecMidStreamOutputBufferReConfigFinish();

  VLOGF("Done with mid-stream format change.");
}

// TODO(dustingreen): Consider whether we ever intend to plumb anything coming from the core codec
// from a different proc.  If not (probably this is the case), we can change several of the checks
// in here to ZX_DEBUG_ASSERT() instead.
bool CodecImpl::FixupBufferCollectionConstraintsLocked(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings,
    fuchsia::sysmem::BufferCollectionConstraints* buffer_collection_constraints) {
  fuchsia::sysmem::BufferUsage& usage = buffer_collection_constraints->usage;

  if (IsCoreCodecMappedBufferUseful(port)) {
    // Not surprisingly, both decoders and encoders read from input and write to
    // output.
    if (port == kInputPort) {
      if (usage.cpu & ~(fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften)) {
        FailLocked("Core codec set disallowed CPU usage bits (input port).");
        return false;
      }
      if (!IsPortSecureRequired(kInputPort)) {
        usage.cpu |= fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften;
      } else {
        usage.cpu = 0;
      }
    } else {
      if (usage.cpu & ~(fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften)) {
        FailLocked("Core codec set disallowed CPU usage bit(s) (output port).");
        return false;
      }
      if (!IsPortSecureRequired(kOutputPort)) {
        usage.cpu |= fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
      } else {
        usage.cpu = 0;
      }
    }
  } else {
    if (usage.cpu) {
      FailLocked("Core codec set usage.cpu despite !IsCoreCodecMappedBufferUseful()");
      return false;
    }
    // The CPU won't touch the buffers at all.
    usage.cpu = 0;
  }
  if (usage.vulkan) {
    FailLocked("Core codec set usage.vulkan bits");
    return false;
  }
  ZX_DEBUG_ASSERT(!usage.vulkan);
  if (usage.display) {
    FailLocked("Core codec set usage.display bits");
    return false;
  }
  ZX_DEBUG_ASSERT(!usage.display);
  if (IsDecryptor()) {
    // DecryptorAdapter should not be setting video usage bits.
    if (usage.video) {
      FailLocked("Core codec set disallowed video usage bits for decryptor");
      return false;
    }
    if (port == kOutputPort) {
      usage.video |= fuchsia::sysmem::videoUsageDecryptorOutput;
    }
  } else if (IsCoreCodecHwBased(port)) {
    // Let's see if we can deprecate videoUsageHwProtected, since it's redundant
    // with secure_required.
    if (usage.video & fuchsia::sysmem::videoUsageHwProtected) {
      FailLocked("Core codec set deprecated videoUsageHwProtected - disallow");
      return false;
    }
    uint32_t allowed_video_usage_bits =
        IsDecoder() ? fuchsia::sysmem::videoUsageHwDecoder : fuchsia::sysmem::videoUsageHwEncoder;
    if (usage.video & ~allowed_video_usage_bits) {
      FailLocked(
          "Core codec set disallowed video usage bit(s) - port: %d, usage: "
          "0x%08x, allowed: 0x%08x",
          port, usage.video, allowed_video_usage_bits);
      return false;
    }
    if (IsDecoder()) {
      usage.video |= fuchsia::sysmem::videoUsageHwDecoder;
    } else if (IsEncoder()) {
      usage.video |= fuchsia::sysmem::videoUsageHwEncoder;
    }
  } else {
    // Despite being a video decoder or encoder, a SW decoder or encoder doesn't
    // count as videoUsageHwDecoder or videoUsageHwEncoder.  And definitely not
    // videoUsageHwProtected.
    usage.video = 0;
  }

  bool is_single_buffer_mode =
      partial_settings.has_single_buffer_mode() && partial_settings.single_buffer_mode();

  if (is_single_buffer_mode) {
    if (buffer_collection_constraints->min_buffer_count_for_camping != 0) {
      FailLocked(
          "Core codec set min_buffer_count_for_camping non-zero when single_buffer_mode true -- "
          "min_buffer_count_for_camping: %lu ",
          buffer_collection_constraints->min_buffer_count_for_camping);
      return false;
    }
    if (buffer_collection_constraints->min_buffer_count_for_dedicated_slack != 0 ||
        buffer_collection_constraints->min_buffer_count_for_shared_slack != 0) {
      FailLocked(
          "Core codec set slack with single_buffer_mode - "
          "min_buffer_count_for_dedicated_slack: %lu "
          "min_buffer_count_for_shared_slack: %lu",
          buffer_collection_constraints->min_buffer_count_for_dedicated_slack,
          buffer_collection_constraints->min_buffer_count_for_shared_slack);
      return false;
    }
    if (buffer_collection_constraints->max_buffer_count != 1) {
      FailLocked("Core codec must specify max_buffer_count 1 when single_buffer_mode");
      return false;
    }
  } else {
    if (buffer_collection_constraints->min_buffer_count_for_camping < 1) {
      FailLocked("Core codec set min_buffer_count_for_camping to 0 when !single_buffer_mode.");
      return false;
    }
  }

  if (!buffer_collection_constraints->has_buffer_memory_constraints) {
    // Leaving all fields set to their defaults is fine if that's really true, but this encourages
    // CodecAdapter implementations to set fields in here.
    FailLocked("Core codec must set has_buffer_memory_constraints");
    return false;
  }
  fuchsia::sysmem::BufferMemoryConstraints& buffer_memory_constraints =
      buffer_collection_constraints->buffer_memory_constraints;

  // Sysmem will fail the BufferCollection if the core codec provides constraints that are
  // inconsistent, but we need to check here that the core codec is being consistent with
  // SecureMemoryMode, since sysmem doesn't know about SecureMemoryMode.  Essentially
  // SecureMemoryMode translates into secure_required and secure_permitted in sysmem.  The former
  // is just a bool.  The latter is indicated by listing at least one secure heap.

  // secure_required consistency check
  //
  // CoreCodecSetSecureMemoryMode() informed the core codec of the mode previously.
  if (!!IsPortSecureRequired(port) != !!buffer_memory_constraints.secure_required) {
    FailLocked("Core codec secure_required inconsistent with SecureMemoryMode");
    return false;
  }

  // secure_permitted consistency check
  //
  // If secure is permitted, then the core codec must support at least one non-SYSTEM_RAM heap, as
  // specifying support for a secure heap is how sysmem knows secure_permitted.  We can't directly
  // tell that the non-RAM heap is secure, so this is an approximate check.  In any case
  // secure_required by any sysmem participant will be enforced by sysmem with respect to specific
  // heaps and whether they're secure.  The approximate-ness is ok since this only comes from
  // in-proc, so the check is just for trying to notice if the core codec is filling out
  // inconsistent constraints in a way that sysmem wouldn't otherwise notice.
  bool is_non_ram_heap_found = false;
  for (uint32_t iter = 0; iter < buffer_memory_constraints.heap_permitted_count; ++iter) {
    if (buffer_memory_constraints.heap_permitted[iter] != fuchsia::sysmem::HeapType::SYSTEM_RAM) {
      is_non_ram_heap_found = true;
      break;
    }
  }
  if (IsPortSecurePermitted(port) && !is_non_ram_heap_found) {
    FailLocked("Core codec must specify at least one non-RAM heap when secure_required");
    return false;
  }

  // The rest of the constraints are entirely up to the core codec, and it's up to the core codec
  // to specify self-consistent constraints.  Sysmem will perform additional consistency checks on
  // the constraints.

  return true;
}

thrd_t CodecImpl::fidl_thread() { return shared_fidl_thread_; }

void CodecImpl::SendFreeInputPacketLocked(fuchsia::media::PacketHeader header) {
  // We allow calling this method on StreamControl or InputData ordering domain.
  // Because the InputData ordering domain thread isn't visible to this code,
  // if this isn't the StreamControl then we can only assert that this thread
  // isn't the FIDL thread, because we know the codec's InputData thread isn't
  // the FIDL thread.
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_ || thrd_current() != fidl_thread());
  // We only send using fidl_thread().
  PostToSharedFidl([this, header = std::move(header)]() mutable {
    // See "is_bound_checks" comment up top.
    if (binding_.is_bound()) {
      binding_.events().OnFreeInputPacket(std::move(header));
    }
  });
}

bool CodecImpl::IsInputConfiguredLocked() {
  return IsPortBuffersConfiguredCommonLocked(kInputPort);
}

bool CodecImpl::IsOutputConfiguredLocked() {
  if (!IsPortBuffersConfiguredCommonLocked(kOutputPort)) {
    return false;
  }
  ZX_DEBUG_ASSERT(port_settings_[kOutputPort]);
  if (!port_settings_[kOutputPort]->is_complete_seen_output()) {
    return false;
  }
  return true;
}

bool CodecImpl::IsPortBuffersConfiguredCommonLocked(CodecPort port) {
  // In addition to what we're able to assert here, when
  // is_port_buffers_configured_[port], the core codec also has the port
  // configured.
  ZX_DEBUG_ASSERT(!is_port_buffers_configured_[port] ||
                  port_settings_[port] &&
                      all_buffers_[port].size() == port_settings_[port]->buffer_count());
  return is_port_buffers_configured_[port];
}

bool CodecImpl::IsPortBuffersAtLeastPartiallyConfiguredLocked(CodecPort port) {
  if (IsPortBuffersConfiguredCommonLocked(port)) {
    return true;
  }
  if (!port_settings_[port]) {
    return false;
  }
  ZX_DEBUG_ASSERT(port_settings_[port]);
  ZX_DEBUG_ASSERT(buffer_lifetime_ordinal_[port] % 2 == 1);
  return true;
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
    vFailLocked(is_fatal, format, args);
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

  size_t buffer_bytes_2 = vsnprintf(buffer.get(), buffer_bytes, format, args2) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  ZX_DEBUG_ASSERT(buffer_bytes == buffer_bytes_2);
  va_end(args2);

  // TODO(dustingreen): It might be worth wiring this up to the log in a more
  // official way, especially if doing so would print a timestamp automatically
  // and/or provide filtering goodness etc.
  const char* message = is_fatal ? "devhost will fail" : "Codec channel will close async";

  // TODO(dustingreen): Send string in buffer via epitaph, when possible.  First
  // we should switch to events so we'll only have the Codec channel not the
  // CodecEvents channel. Note to self: The channel failing server-side may race
  // with trying to send.

  if (is_fatal) {
    // Logs to syslog for non-driver clients
    FX_LOGS(ERROR) << buffer.get() << " -- " << message << "\n";
    // Default logging to stderr for both driver and non-driver clients
    LOG(ERROR, "%s -- %s", buffer.get(), message);

    abort();
  } else {
    // Logs to syslog for non-driver clients
    FX_LOGS(WARNING) << buffer.get() << " -- " << message << "\n";
    // Default logging to stderr for both driver and non-driver clients
    LOG(WARNING, "%s -- %s", buffer.get(), message);

    UnbindLocked();
  }

  // At this point we know "this" is still allocated only because we still hold
  // lock_.  As soon as lock_ is released by the caller, "this" can immediately
  // be deallocated by another thread, if this isn't currently the
  // fidl_thread().
}

void CodecImpl::PostSerial(async_dispatcher_t* async, fit::closure to_run) {
  zx_status_t result = async::PostTask(async, std::move(to_run));
  ZX_ASSERT(result == ZX_OK);
}

// The implementation of PostToSharedFidl() permits queuing lambdas that use
// "this", despite the fact that the client can call ~CodecImpl at any time
// using the fidl_thread().  If ~CodecImpl is called before the lambda runs, the
// lambda will be deleted instead of run, and the deletion will occur during
// ~CodecImpl while essentially all of CodecImpl is still valid (in case ~lambda
// itself touches any of CodecImpl).
void CodecImpl::PostToSharedFidl(fit::closure to_run) {
  // If shared_fidl_queue_.is_stopped(), then to_run will just be deleted here.
  shared_fidl_queue_.Enqueue(std::move(to_run));
}

// The implementation of PostToStreamControl() doesn't strongly need to guard
// against ~CodecImpl because ~CodecImpl will do
// stream_control_loop_.Shutdown(), which deletes any tasks that haven't already
// run on StreamControl.  We use a ClosureQueue anyway, for at least a couple
// reasons.
//
// Not very importantly, by using a ClosureQueue here, we eliminate a window
// between is_stream_control_done_ = true and the lambda posted to FIDL thread
// shortly after that, during which hypothetically many FIDL dispatches could
// queue to StreamControl without them being consumed by StreamControl.
//
// More importantly, assuming we add an over-full threshold detection to
// ClosureQueue, that can help avoid the server being overwhelmed by a
// badly-behaving client that queues more messages than make any sense given the
// StreamProcessor protocol (which overall limits the number of concurrent
// messages that are allowed / make any sense, but any given message isn't
// necessarily checked for making sense until we're on StreamControl).
void CodecImpl::PostToStreamControl(fit::closure to_run) {
  // If stream_control_queue_.is_stopped(), then to_run will just be deleted
  // here.
  stream_control_queue_.Enqueue(std::move(to_run));
}

bool CodecImpl::IsStoppingLocked() { return was_unbind_started_; }

bool CodecImpl::IsStopping() {
  std::lock_guard<std::mutex> lock(lock_);
  return IsStoppingLocked();
}

bool CodecImpl::IsDecoder() const { return params_.index() == 0; }

bool CodecImpl::IsEncoder() const { return params_.index() == 1; }

bool CodecImpl::IsDecryptor() const { return params_.index() == 2; }

const fuchsia::mediacodec::CreateDecoder_Params& CodecImpl::decoder_params() const {
  ZX_DEBUG_ASSERT(IsDecoder());
  return fit::get<fuchsia::mediacodec::CreateDecoder_Params>(params_);
}

const fuchsia::mediacodec::CreateEncoder_Params& CodecImpl::encoder_params() const {
  ZX_DEBUG_ASSERT(IsEncoder());
  return fit::get<fuchsia::mediacodec::CreateEncoder_Params>(params_);
}

const fuchsia::media::drm::DecryptorParams& CodecImpl::decryptor_params() const {
  ZX_DEBUG_ASSERT(IsDecryptor());
  return fit::get<fuchsia::media::drm::DecryptorParams>(params_);
}

// true - maybe it's the core codec thread.
// false - it's definitely not the core codec thread.
bool CodecImpl::IsPotentiallyCoreCodecThread() {
  return (thrd_current() != stream_control_thread_) && (thrd_current() != fidl_thread());
}

void CodecImpl::HandlePendingInputFormatDetails() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  const fuchsia::media::FormatDetails* input_details = nullptr;
  if (stream_->input_format_details()) {
    input_details = stream_->input_format_details();
  } else {
    input_details = initial_input_format_details_;
  }
  ZX_DEBUG_ASSERT(input_details);
  CoreCodecQueueInputFormatDetails(*input_details);
}

void CodecImpl::onCoreCodecFailCodec(const char* format, ...) {
  std::string local_format = std::string("onCoreCodecFailCodec() called -- ") + format;
  va_list args;
  va_start(args, format);
  vFail(false, local_format.c_str(), args);
  // "this" can be deallocated by this point (as soon as ~lock above).
  va_end(args);
}

void CodecImpl::onCoreCodecFailStream(fuchsia::media::StreamError error) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (IsStoppingLocked()) {
      // This CodecImpl is already stopping due to a previous FailLocked(),
      // which will result in the Codec channel getting closed soon.  So don't
      // send OnStreamFailed().
      return;
    }

    // We rely on the CodecAdapter and the rest of CodecImpl to only call this method when there's a
    // current stream.
    ZX_DEBUG_ASSERT(stream_ && stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal_);

    if (stream_->output_end_of_stream()) {
      // Tolerate a CodecAdapter failing the stream after output EndOfStream
      // seen, and avoid notifying the client of a stream failure that's too
      // late to matter.
      return;
    }

    if (stream_->failure_seen()) {
      // We already know.  We don't auto-close the stream because the client is
      // in control of stream lifetime, so it's plausible that a CodecAdapter
      // could notify of stream failure more than once.  We can ignore the
      // redundant stream failure to avoid sending OnStreamFailed() again.
      return;
    }
    stream_->SetFailureSeen();
    // avoid hang in FlushEndOfStreamAndCloseStream_StreamControl
    // TODO(fxbug.dev/43490): Clean this up.
    output_end_of_stream_seen_.notify_all();

    if (IsStreamErrorRecoverable(error)) {
      LOG(INFO, "Stream %lu failed: %s. %s", stream_lifetime_ordinal_, ToString(error),
          GetStreamErrorAdditionalHelpText(error));
    } else {
      LOG(ERROR, "Stream %lu failed: %s", stream_lifetime_ordinal_, ToString(error));
    }

    // We're failing the current stream.  We should still queue to the output
    // ordering domain to ensure ordering vs. any previously-sent output on this
    // stream that was sent directly from codec processing thread.
    //
    // This failure is async, in the sense that the client may still be sending
    // input data, and the core codec is expected to just hold onto those
    // packets until the client has moved on from this stream.

    if (stream_->future_discarded()) {
      // No reason to report a stream failure to the client for an obsolete stream.  The client has
      // already moved on from the current stream anyway.  This path won't be taken if the client
      // flushed the stream before moving on.  This permits core codecs to indicate
      // onCoreCodecFailStream() on a stream being cancelled due to a newer stream, without that
      // causing FailLocked() of the whole codec (important), and without sending an extraneous
      // OnStreamFailed() (less important since the client is expected to ignore messages for an
      // obsolete stream).  Ideally a core codec wouldn't trigger onCoreCodecFailStream() during
      // CoreCodecStopStream(), but this path tolerates it.
      return;
    }

    if (!is_on_stream_failed_enabled_) {
      FailLocked(
          "onStreamFailed() with a client that didn't send "
          "EnableOnStreamFailed(), so closing the Codec channel instead.");
      return;
    }
    // There's not actually any need to track that the stream failed anywhere
    // in the CodecImpl.  The client needs to move on from the failed
    // stream to a new stream, or close the Codec channel.
    PostToSharedFidl([this, stream_lifetime_ordinal = stream_lifetime_ordinal_, error] {
      // See "is_bound_checks" comment up top.
      if (binding_.is_bound()) {
        binding_.events().OnStreamFailed(stream_lifetime_ordinal, error);
      }
    });
  }  // ~lock
}

void CodecImpl::onCoreCodecResetStreamAfterCurrentFrame() {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    // Calls to onCoreCodecResetStreamAfterCurrentFrame() must be fenced out (by the core codec)
    // during CoreCodecStopStream(), so we know we still have the current stream here.
    ZX_DEBUG_ASSERT(stream_);
    // By the time we post over to StreamControl however, the current stream may no longer be
    // current.  If we've moved on to another stream, it's fine to just ignore the reset stream
    // request for a stream that's no longer current.
    uint64_t stream_lifetime_ordinal = stream_->stream_lifetime_ordinal();
    PostToStreamControl([this, stream_lifetime_ordinal] {
      ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
      {  // scope lock
        std::unique_lock<std::mutex> lock(lock_);

        // Only StreamControl messes with stream_.
        if (!stream_) {
          return;
        }
        ZX_DEBUG_ASSERT(stream_);
        if (stream_->stream_lifetime_ordinal() != stream_lifetime_ordinal) {
          return;
        }
        ZX_DEBUG_ASSERT(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal);
        if (stream_->future_discarded()) {
          // Ignore since this stream will be gone soon anyway.
          return;
        }
        if (stream_->failure_seen()) {
          // Ignore since this stream has already failed anyway.
          return;
        }
        ZX_DEBUG_ASSERT(is_core_codec_stream_started_);
      }  // ~lock
      CoreCodecResetStreamAfterCurrentFrame();
      return;
    });
  }  // ~lock
}

void CodecImpl::onCoreCodecMidStreamOutputConstraintsChange(bool output_re_config_required) {
  VLOGF("CodecImpl::onCoreCodecMidStreamOutputConstraintsChange(): re-config: %d",
        output_re_config_required);
  // For now, the core codec thread is the only thread this gets called from.
  ZX_DEBUG_ASSERT(IsPotentiallyCoreCodecThread());

  // For a OMX_EventPortSettingsChanged that doesn't demand output buffer
  // re-config before more output data, this translates to an ordered emit
  // of a no-action-required OnOutputConstraints() that just updates to the new
  // format, without demanding output buffer re-config.  HDR info could be
  // conveyed this way, ordered with respect to output frames.
  if (!output_re_config_required) {
    std::unique_lock<std::mutex> lock(lock_);
    GenerateAndSendNewOutputConstraints(lock,
                                        false);  // buffer_constraints_action_required
    return;
  }

  // We have an output constraints change that does demand output buffer
  // re-config before more output data.
  ZX_DEBUG_ASSERT(output_re_config_required);

  // We post over to StreamControl domain because we need to synchronize
  // with any changes to stream state that might be driven by the client.
  // When we get over there to StreamControl, we'll check if we're still
  // talking about the same stream_lifetime_ordinal, and if not, we ignore
  // the event, because a new stream may or may not have the same output
  // settings, and we'll be re-generating an OnOutputConstraints() as needed
  // from current/later core codec output constraints anyway.  Here are the
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
  //     client's next stream to start with a new OnOutputConstraints() that
  //     the client must catch up to before the stream can fully start.
  //     This way we know we're not ignoring a potential change to
  //     nBufferCountMin or anything like that.
  uint64_t local_stream_lifetime_ordinal;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);

    // The core codec is only allowed to call this mehtod while there's an
    // active stream.
    ZX_DEBUG_ASSERT(IsStreamActiveLocked());

    // The client is allowed to essentially forget what the format is on any
    // mid-stream buffer config change, so remember to re-send the format to the
    // client before the next output packet of this stream.
    stream_->SetOutputFormatPending();

    // For asserts.
    stream_->SetMidStreamOutputConstraintsChangeActive();

    // This part is not speculative.  The core codec has indicated that it's at
    // least meh about the current output config, so ensure we do a required
    // OnOutputConstraints() before the next stream starts, even if the client
    // moves on to a new stream such that the speculative part below becomes
    // stale.
    core_codec_meh_output_buffer_constraints_version_ordinal_ =
        port_settings_[kOutputPort]
            ? port_settings_[kOutputPort]->buffer_constraints_version_ordinal()
            : 0;
    // Speculative part - this part is speculative, in that we don't know if
    // this post over to StreamControl will beat any client driving to a new
    // stream.  So we snap the stream_lifetime_ordinal so we know whether to
    // ignore the post once it reaches StreamControl.
    local_stream_lifetime_ordinal = stream_lifetime_ordinal_;
  }  // ~lock
  PostToStreamControl([this, stream_lifetime_ordinal = local_stream_lifetime_ordinal] {
    MidStreamOutputConstraintsChange(stream_lifetime_ordinal);
  });
}

void CodecImpl::onCoreCodecOutputFormatChange() {
  ZX_DEBUG_ASSERT(IsPotentiallyCoreCodecThread());
  std::lock_guard<std::mutex> lock(lock_);
  ZX_DEBUG_ASSERT(IsStreamActiveLocked());
  // In future we could relax this requirement, but for now we don't allow
  // output format changes, output packets, or EOS while mid-stream constraints
  // change is active.
  ZX_DEBUG_ASSERT(!stream_->is_mid_stream_output_constraints_change_active());
  // Next time the core codec asks to output a packet, we'll send the format
  // first.
  stream_->SetOutputFormatPending();
}

void CodecImpl::onCoreCodecInputPacketDone(CodecPacket* packet) {
  // Free/busy coherency from Codec interface to core codec doesn't involve
  // trusting the client, so assert we're doing it right server-side.
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    // The core codec says the buffer-referening in-flight lifetime of this
    // packet is over.  We'll set the buffer again when this packet get's used
    // by the client again to deliver more input data.
    packet->SetBuffer(nullptr);
    // Unfortunately we have to insist that the core codec not call
    // onCoreCodecInputPacketDone() arbitrarily late because we need to know
    // when it's safe to deallocate binding_, and the core codec, etc.  So the
    // rule is the core codec needs to ensure that all calls to stream-related
    // callbacks have completed (to structure-touching degree; not
    // code-unloading degree) before CoreCodecStopStream() returns.
    ZX_DEBUG_ASSERT(is_core_codec_stream_started_);
    ZX_DEBUG_ASSERT(!all_packets_[kInputPort][packet->packet_index()]->is_free());
    all_packets_[kInputPort][packet->packet_index()]->SetFree(true);
    fuchsia::media::PacketHeader header;
    header.set_buffer_lifetime_ordinal(packet->buffer_lifetime_ordinal());
    header.set_packet_index(packet->packet_index());
    SendFreeInputPacketLocked(std::move(header));
  }  // ~lock
}

void CodecImpl::onCoreCodecOutputPacket(CodecPacket* packet, bool error_detected_before,
                                        bool error_detected_during) {
  ZX_DEBUG_ASSERT(IsPotentiallyCoreCodecThread());

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // The core codec shouldn't output a packet until after
    // CoreCodecStartStream() and input data availability in the case that
    // output buffer config was already suitable, or until after
    // CoreCodecMidStreamOutputBufferReConfigFinish() in the case that output
    // buffer config wasn't suitable (not configured or not suitable) or
    // changed mid-stream.  See also comments in codec_adapter.h.
    ZX_DEBUG_ASSERT(IsOutputConfiguredLocked());

    // Before we send the packet, we check whether the stream has output format
    // pending, which means we need to send the output format before the output
    // packet (and clear the pending state).
    ZX_DEBUG_ASSERT(IsStreamActiveLocked());

    ZX_DEBUG_ASSERT(!stream_->is_mid_stream_output_constraints_change_active());

    if (stream_->output_format_pending()) {
      stream_->ClearOutputFormatPending();
      uint64_t stream_lifetime_ordinal = stream_lifetime_ordinal_;
      uint64_t new_output_format_details_version_ordinal =
          next_output_format_details_version_ordinal_++;
      fuchsia::media::StreamOutputFormat output_format;
      {  // scope unlock
        ScopedUnlock unlock(lock);
        output_format = CoreCodecGetOutputFormat(stream_lifetime_ordinal,
                                                 new_output_format_details_version_ordinal);
      }  // ~unlock
      // Stream change while unlocked above won't happen because we're on
      // InputData domain which is fenced as part of stream switch.
      ZX_DEBUG_ASSERT(stream_lifetime_ordinal == stream_lifetime_ordinal_);
      ZX_DEBUG_ASSERT(new_output_format_details_version_ordinal ==
                      next_output_format_details_version_ordinal_ - 1);
      ZX_DEBUG_ASSERT(sent_format_details_version_ordinal_[kOutputPort] + 1 ==
                      new_output_format_details_version_ordinal);
      sent_format_details_version_ordinal_[kOutputPort] = new_output_format_details_version_ordinal;
      PostToSharedFidl([this, output_format = std::move(output_format)]() mutable {
        // See "is_bound_checks" comment up top.
        if (binding_.is_bound()) {
          binding_.events().OnOutputFormat(std::move(output_format));
        }
      });
    }
  }

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    // This helps verify that packet lifetimes are coherent, but we can't do the
    // same for buffer_index because VP9 has show_existing_frame which is
    // allowed to output the same buffer repeatedly.
    //
    // TODO(dustingreen): We could _optionally_ verify that buffer lifetimes are
    // coherent for codecs that don't output the same buffer repeatedly and
    // concurrently.
    all_packets_[kOutputPort][packet->packet_index()]->SetFree(false);
    ZX_DEBUG_ASSERT(packet->has_start_offset());
    ZX_DEBUG_ASSERT(packet->has_valid_length_bytes());
    // packet->has_timestamp_ish() is optional even if
    // promise_separate_access_units_on_input is true.  We do want to enforce
    // that the client gets no set timestamp_ish values if the client didn't
    // promise_separate_access_units_on_input.
    bool has_timestamp_ish =
        (!IsDecoder() || (decoder_params().has_promise_separate_access_units_on_input() &&
                          decoder_params().promise_separate_access_units_on_input())) &&
        packet->has_timestamp_ish();
    fuchsia::media::Packet p;
    p.mutable_header()->set_buffer_lifetime_ordinal(packet->buffer_lifetime_ordinal());
    p.mutable_header()->set_packet_index(packet->packet_index());
    p.set_buffer_index(packet->buffer()->index());
    p.set_stream_lifetime_ordinal(stream_lifetime_ordinal_);
    p.set_start_offset(packet->start_offset());
    p.set_valid_length_bytes(packet->valid_length_bytes());
    if (has_timestamp_ish) {
      p.set_timestamp_ish(packet->timestamp_ish());
    }
    if (packet->has_key_frame()) {
      p.set_key_frame(packet->key_frame());
    }
    p.set_start_access_unit(true);
    p.set_known_end_access_unit(true);
    PostToSharedFidl(
        [this, p = std::move(p), error_detected_before, error_detected_during]() mutable {
          // See "is_bound_checks" comment up top.
          if (binding_.is_bound()) {
            if (kLogTimestampDelay) {
              LOG(INFO, "output timestamp: has: %d value: 0x%" PRIx64, p.has_timestamp_ish(),
                  p.has_timestamp_ish() ? p.timestamp_ish() : 0);
            }
            binding_.events().OnOutputPacket(std::move(p), error_detected_before,
                                             error_detected_during);
          }
        });
  }  // ~lock
}

void CodecImpl::onCoreCodecOutputEndOfStream(bool error_detected_before) {
  VLOGF("CodecImpl::onCoreCodecOutputEndOfStream()");
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT(IsStreamActiveLocked());
    ZX_DEBUG_ASSERT(!stream_->is_mid_stream_output_constraints_change_active());
    stream_->SetOutputEndOfStream();
    output_end_of_stream_seen_.notify_all();
    PostToSharedFidl(
        [this, stream_lifetime_ordinal = stream_lifetime_ordinal_, error_detected_before] {
          // See "is_bound_checks" comment up top.
          if (binding_.is_bound()) {
            binding_.events().OnOutputEndOfStream(stream_lifetime_ordinal, error_detected_before);
          }
        });
  }  // ~lock
}

CodecImpl::Stream::Stream(uint64_t stream_lifetime_ordinal)
    : stream_lifetime_ordinal_(stream_lifetime_ordinal) {
  // nothing else to do here
}

uint64_t CodecImpl::Stream::stream_lifetime_ordinal() { return stream_lifetime_ordinal_; }

void CodecImpl::Stream::SetFutureDiscarded() {
  ZX_DEBUG_ASSERT(!future_discarded_);
  future_discarded_ = true;
}

bool CodecImpl::Stream::future_discarded() { return future_discarded_; }

void CodecImpl::Stream::SetFutureFlushEndOfStream() {
  ZX_DEBUG_ASSERT(!future_flush_end_of_stream_);
  future_flush_end_of_stream_ = true;
}

bool CodecImpl::Stream::future_flush_end_of_stream() { return future_flush_end_of_stream_; }

CodecImpl::Stream::~Stream() {
  VLOGF("~Stream() stream_lifetime_ordinal: %lu", stream_lifetime_ordinal_);
}

void CodecImpl::Stream::SetInputFormatDetails(
    std::unique_ptr<fuchsia::media::FormatDetails> input_format_details) {
  // This is allowed to happen multiple times per stream.
  input_format_details_ = std::move(input_format_details);
}

const fuchsia::media::FormatDetails* CodecImpl::Stream::input_format_details() {
  return input_format_details_.get();
}

void CodecImpl::Stream::SetOobConfigPending(bool pending) {
  // SetOobConfigPending(true) is legal regardless of current state, but
  // SetOobConfigPending(false) is only legal if the state is currently true.
  ZX_DEBUG_ASSERT(pending || oob_config_pending_);
  oob_config_pending_ = pending;
}

bool CodecImpl::Stream::oob_config_pending() { return oob_config_pending_; }

void CodecImpl::Stream::SetInputEndOfStream() {
  ZX_DEBUG_ASSERT(!input_end_of_stream_);
  input_end_of_stream_ = true;
}

bool CodecImpl::Stream::input_end_of_stream() { return input_end_of_stream_; }

void CodecImpl::Stream::SetOutputEndOfStream() {
  ZX_DEBUG_ASSERT(!output_end_of_stream_);
  output_end_of_stream_ = true;
}

bool CodecImpl::Stream::output_end_of_stream() { return output_end_of_stream_; }

void CodecImpl::Stream::SetFailureSeen() {
  ZX_DEBUG_ASSERT(!failure_seen_);
  failure_seen_ = true;
}

bool CodecImpl::Stream::failure_seen() { return failure_seen_; }

void CodecImpl::Stream::SetOutputFormatPending() { output_format_pending_ = true; }

void CodecImpl::Stream::ClearOutputFormatPending() { output_format_pending_ = false; }

bool CodecImpl::Stream::output_format_pending() { return output_format_pending_; }

void CodecImpl::Stream::SetMidStreamOutputConstraintsChangeActive() {
  ZX_DEBUG_ASSERT(!is_mid_stream_output_constraints_change_active_);
  is_mid_stream_output_constraints_change_active_ = true;
}

void CodecImpl::Stream::ClearMidStreamOutputConstraintsChangeActive() {
  ZX_DEBUG_ASSERT(is_mid_stream_output_constraints_change_active_);
  is_mid_stream_output_constraints_change_active_ = false;
}

bool CodecImpl::Stream::is_mid_stream_output_constraints_change_active() {
  return is_mid_stream_output_constraints_change_active_;
}

CodecImpl::PortSettings::PortSettings(CodecImpl* parent, CodecPort port,
                                      fuchsia::media::StreamBufferPartialSettings partial_settings)
    : parent_(parent),
      port_(port),
      partial_settings_(std::make_unique<fuchsia::media::StreamBufferPartialSettings>(
          std::move(partial_settings))) {
  // nothing else to do here
}

CodecImpl::PortSettings::~PortSettings() {
  // To be safe, the unbind needs to occur on the FIDL thread.  In addition, we want to send a clean
  // Close() to avoid causing the LogicalBufferCollection to fail.  Since we're not a crashing
  // process, this is a clean close by definition.
  //
  // TODO(fxbug.dev/37257): Consider _not_ sending Close() for unexpected failures initiated by the
  // server. Consider whether to have a Close() on StreamProcessor to disambiguate clean vs.
  // unexpected StreamProcessor channel close.
  if (thrd_current() != parent_->fidl_thread()) {
    parent_->PostToSharedFidl([buffer_collection = std::move(buffer_collection_)] {
      // Sysmem will notice the Close() before the PEER_CLOSED.
      if (buffer_collection) {
        buffer_collection->Close();
      }
      // ~buffer_collection on FIDL thread
    });
  } else {
    if (buffer_collection_) {
      buffer_collection_->Close();
    }
  }
}

void CodecImpl::PortSettings::SetBufferCollectionInfo(
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info) {
  ZX_DEBUG_ASSERT(!buffer_collection_info_);
  buffer_collection_info_ =
      std::make_unique<fuchsia::sysmem::BufferCollectionInfo_2>(std::move(buffer_collection_info));
}

const fuchsia::sysmem::BufferCollectionInfo_2& CodecImpl::PortSettings::buffer_collection_info() {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  return *buffer_collection_info_;
}

uint64_t CodecImpl::PortSettings::buffer_lifetime_ordinal() {
  return partial_settings_->buffer_lifetime_ordinal();
}

uint64_t CodecImpl::PortSettings::buffer_constraints_version_ordinal() {
  return partial_settings_->buffer_constraints_version_ordinal();
}

uint32_t CodecImpl::PortSettings::packet_count() {
  // Asking before we have buffer_collection_info_ would potentially get the
  // wrong answer.
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  uint32_t packet_count_for_server = partial_settings_->has_packet_count_for_server()
                                         ? partial_settings_->packet_count_for_server()
                                         : 0;
  uint32_t packet_count_for_client = partial_settings_->has_packet_count_for_client()
                                         ? partial_settings_->packet_count_for_client()
                                         : 0;
  return std::max(packet_count_for_server + packet_count_for_client,
                  buffer_collection_info_->buffer_count);
}

uint32_t CodecImpl::PortSettings::buffer_count() {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  return buffer_collection_info_->buffer_count;
}

fuchsia::sysmem::CoherencyDomain CodecImpl::PortSettings::coherency_domain() {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  return buffer_collection_info_->settings.buffer_settings.coherency_domain;
}

const fuchsia::media::StreamBufferPartialSettings& CodecImpl::PortSettings::partial_settings() {
  return *partial_settings_;
}

fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> CodecImpl::PortSettings::TakeToken() {
  ZX_DEBUG_ASSERT(partial_settings_->has_sysmem_token());
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token =
      std::move(*partial_settings_->mutable_sysmem_token());
  partial_settings_->clear_sysmem_token();
  return token;
}

zx::vmo CodecImpl::PortSettings::TakeVmo(uint32_t buffer_index) {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  ZX_DEBUG_ASSERT(buffer_index < buffer_collection_info_->buffer_count);
  return std::move(buffer_collection_info_->buffers[buffer_index].vmo);
}

fidl::InterfaceRequest<fuchsia::sysmem::BufferCollection>
CodecImpl::PortSettings::NewBufferCollectionRequest(async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(thrd_current() == parent_->fidl_thread());
  ZX_DEBUG_ASSERT(!buffer_collection_);
  return buffer_collection_.NewRequest(dispatcher);
}

fuchsia::sysmem::BufferCollectionPtr& CodecImpl::PortSettings::buffer_collection() {
  ZX_DEBUG_ASSERT(thrd_current() == parent_->fidl_thread());
  return buffer_collection_;
}

void CodecImpl::PortSettings::UnbindBufferCollection() {
  ZX_DEBUG_ASSERT(thrd_current() == parent_->fidl_thread());
  // return value intentionally ignored and deleted
  buffer_collection_.Unbind();
}

bool CodecImpl::PortSettings::is_complete_seen_output() {
  ZX_DEBUG_ASSERT(port_ == kOutputPort);
  return is_complete_seen_output_;
}

void CodecImpl::PortSettings::SetCompleteSeenOutput() {
  ZX_DEBUG_ASSERT(port_ == kOutputPort);
  ZX_DEBUG_ASSERT(thrd_current() == parent_->fidl_thread());
  ZX_DEBUG_ASSERT(!is_complete_seen_output_);
  is_complete_seen_output_ = true;
}

uint64_t CodecImpl::PortSettings::vmo_usable_start(uint32_t buffer_index) {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  ZX_DEBUG_ASSERT(buffer_index < buffer_collection_info_->buffer_count);
  return buffer_collection_info_->buffers[buffer_index].vmo_usable_start;
}

uint64_t CodecImpl::PortSettings::vmo_usable_size() {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  return buffer_collection_info_->settings.buffer_settings.size_bytes;
}

bool CodecImpl::PortSettings::is_secure() {
  ZX_DEBUG_ASSERT(buffer_collection_info_);
  return buffer_collection_info_->settings.buffer_settings.is_secure;
}

//
// CoreCodec wrappers, for the asserts.  These asserts, and the way we ensure
// at compile time that this class has a method for every method of
// CodecAdapter, are essentially costing a double vtable call instead of a
// single vtable call.  If we don't like that at some point, we can remove the
// private CodecAdapter inheritance from CodecImpl and have these be normal
// methods instead of virtual methods.
//

void CodecImpl::CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecInit(initial_input_format_details);
}

void CodecImpl::CoreCodecSetSecureMemoryMode(
    CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecSetSecureMemoryMode(port, secure_memory_mode);
}

fuchsia::sysmem::BufferCollectionConstraints CodecImpl::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());
  // We don't intend to send the sysmem token to the core codec directly, just
  // because it doesn't really need to participate directly that way, and this
  // lets us keep direct interaction with sysmem in CodecImpl instead of each
  // core codec.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());
  return codec_adapter_->CoreCodecGetBufferCollectionConstraints(port, stream_buffer_constraints,
                                                                 partial_settings);
}

void CodecImpl::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecSetBufferCollectionInfo(port, buffer_collection_info);
}

void CodecImpl::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecAddBuffer(port, buffer);
}

void CodecImpl::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecConfigureBuffers(port, packets);
}

void CodecImpl::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && (thrd_current() == fidl_thread() ||
                                          thrd_current() == stream_control_thread_));
  codec_adapter_->CoreCodecEnsureBuffersNotConfigured(port);
}

void CodecImpl::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecStartStream();
}

void CodecImpl::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecQueueInputFormatDetails(per_stream_override_format_details);
}

void CodecImpl::CoreCodecQueueInputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecQueueInputPacket(packet);
}

void CodecImpl::CoreCodecQueueInputEndOfStream() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecQueueInputEndOfStream();
}

void CodecImpl::CoreCodecStopStream() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecStopStream();
}

void CodecImpl::CoreCodecResetStreamAfterCurrentFrame() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecResetStreamAfterCurrentFrame();
}

bool CodecImpl::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread() || thrd_current() == stream_control_thread_);
  return codec_adapter_->IsCoreCodecRequiringOutputConfigForFormatDetection();
}

bool CodecImpl::IsCoreCodecMappedBufferUseful(CodecPort port) {
  ZX_DEBUG_ASSERT(port == kInputPort && thrd_current() == stream_control_thread_ ||
                  port == kOutputPort && thrd_current() == fidl_thread());
  return codec_adapter_->IsCoreCodecMappedBufferUseful(port);
}

bool CodecImpl::IsCoreCodecHwBased(CodecPort port) {
  return codec_adapter_->IsCoreCodecHwBased(port);
}

zx::unowned_bti CodecImpl::CoreCodecBti() {
  ZX_DEBUG_ASSERT(IsCoreCodecHwBased(kInputPort) || IsCoreCodecHwBased(kOutputPort));
  return codec_adapter_->CoreCodecBti();
}

std::unique_ptr<const fuchsia::media::StreamBufferConstraints>
CodecImpl::CoreCodecBuildNewInputConstraints() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  std::unique_ptr<const fuchsia::media::StreamBufferConstraints> constraints =
      codec_adapter_->CoreCodecBuildNewInputConstraints();
  ZX_DEBUG_ASSERT(constraints);
  ZX_DEBUG_ASSERT(constraints->has_buffer_constraints_version_ordinal());

  // StreamProcessor guarantees that these default settings as-is (except buffer_lifetime_ordinal)
  // will satisfy the constraints indicated by the other fields of StreamBufferConstraints.
  ZX_DEBUG_ASSERT(constraints->has_default_settings());
  ZX_DEBUG_ASSERT(constraints->default_settings().has_buffer_lifetime_ordinal() &&
                  constraints->default_settings().buffer_lifetime_ordinal() == 0);
  ZX_DEBUG_ASSERT(constraints->default_settings().has_buffer_constraints_version_ordinal());
  ZX_DEBUG_ASSERT(constraints->default_settings().has_packet_count_for_server());
  ZX_DEBUG_ASSERT(constraints->default_settings().has_packet_count_for_client());
  ZX_DEBUG_ASSERT(constraints->default_settings().has_per_packet_buffer_bytes());
  ZX_DEBUG_ASSERT(constraints->default_settings().has_single_buffer_mode() &&
                  constraints->default_settings().single_buffer_mode() == false);

  return constraints;
}
// Caller must ensure that this is called only on one thread at a time, only
// during setup, during a core codec initiated mid-stream format change, or
// during stream start before any input data has been delivered for the new
// stream.
std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecImpl::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  ZX_DEBUG_ASSERT(IsPotentiallyCoreCodecThread() || thrd_current() == stream_control_thread_);
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> constraints =
      codec_adapter_->CoreCodecBuildNewOutputConstraints(
          stream_lifetime_ordinal, new_output_buffer_constraints_version_ordinal,
          buffer_constraints_action_required);
  ZX_DEBUG_ASSERT(constraints);
  ZX_DEBUG_ASSERT(constraints->has_stream_lifetime_ordinal());
  ZX_DEBUG_ASSERT(constraints->stream_lifetime_ordinal() == stream_lifetime_ordinal);
  ZX_DEBUG_ASSERT(constraints->has_buffer_constraints());
  ZX_DEBUG_ASSERT(constraints->buffer_constraints().has_buffer_constraints_version_ordinal());
  ZX_DEBUG_ASSERT(constraints->buffer_constraints().buffer_constraints_version_ordinal() ==
                  new_output_buffer_constraints_version_ordinal);
  ZX_DEBUG_ASSERT(constraints->has_buffer_constraints_action_required());
  ZX_DEBUG_ASSERT(constraints->buffer_constraints_action_required() ==
                  buffer_constraints_action_required);
  return constraints;
}

fuchsia::media::StreamOutputFormat CodecImpl::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  ZX_DEBUG_ASSERT(IsPotentiallyCoreCodecThread());
  fuchsia::media::StreamOutputFormat format = codec_adapter_->CoreCodecGetOutputFormat(
      stream_lifetime_ordinal, new_output_format_details_version_ordinal);
  ZX_DEBUG_ASSERT(format.has_stream_lifetime_ordinal());
  ZX_DEBUG_ASSERT(format.stream_lifetime_ordinal() == stream_lifetime_ordinal);
  ZX_DEBUG_ASSERT(format.has_format_details());
  ZX_DEBUG_ASSERT(format.format_details().has_format_details_version_ordinal());
  ZX_DEBUG_ASSERT(format.format_details().format_details_version_ordinal() ==
                  new_output_format_details_version_ordinal);
  return format;
}

void CodecImpl::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecMidStreamOutputBufferReConfigPrepare();
}

void CodecImpl::CoreCodecMidStreamOutputBufferReConfigFinish() {
  ZX_DEBUG_ASSERT(thrd_current() == stream_control_thread_);
  codec_adapter_->CoreCodecMidStreamOutputBufferReConfigFinish();
}

void CodecImpl::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT(thrd_current() == fidl_thread());
  codec_adapter_->CoreCodecRecycleOutputPacket(packet);
}
