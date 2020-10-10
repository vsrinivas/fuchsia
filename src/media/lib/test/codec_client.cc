// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/test/codec_client.h"

#include <lib/async/cpp/task.h>
#include <lib/media/test/one_shot_event.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iostream>
#include <random>

namespace {

// The client would like there to be at least this many input buffers.  Despite
// the client filling input buffers quickly, it's still non-zero duration, so
// using 1 here can help avoid short stalls while an input buffer is being
// filled.
constexpr uint32_t kMinInputBufferCountForCamping = 1;
// The client intends to hold onto this many output buffers for a non-transient
// duration.
constexpr uint32_t kMinOutputBufferCountForCamping = 1;

// For input, this example doesn't re-configure input buffers, so there's only
// one buffer_lifetime_ordinal.
constexpr uint64_t kInputBufferLifetimeOrdinal = 1;

// It's fine to increase this threshold if we add a new usage of CodecClient
// with new StreamProcessor server that should/must have more buffers.  This is
// here to check that we're not allocating more output buffers than expected.
// If the various cases get further apart, it'd probably be worthwhile to plumb
// per-case from code that's using CodecClient.  For now this is based on what
// use_h264_decoder_test allocates (max across astro and QEMU).
//
// This is basically 16 max DPB for h264, 1 to decode into (assumed separate
// from DPB for now), and 1 for the client.
constexpr uint32_t kMaxExpectedBufferCount = 18;

}  // namespace

CodecClient::CodecClient(async::Loop* loop, thrd_t loop_thread,
                         fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem)
    : loop_(loop), dispatcher_(loop_->dispatcher()), loop_thread_(loop_thread) {
  // Only one request is ever created, so we create it in the constructor to
  // avoid needing any manual enforcement that we only do this once.
  temp_codec_request_ = codec_.NewRequest(loop_->dispatcher());
  // We want the error handler set up before any error can possibly be generated
  // by the channel so there's no chance of missing an error.  The async::Loop
  // that we'll use is already running separately from the current thread.
  codec_.set_error_handler([this](zx_status_t status) {
    // Obviously a non-example client that continues to have a purpose even if
    // one of its codecs dies would want to handle errors in a more contained
    // way.
    //
    // TODO(dustingreen): get and print epitaph once that's possible.
    if (!in_lax_mode_) {
      ZX_PANIC("codec_ failed - !in_lax_mode_\n");
    } else {
      FX_PLOGS(WARNING, status) << "codec_ failed - in_lax_mode_";
      connection_lost_ = true;
      output_pending_condition_.notify_all();
      is_sync_complete_condition_.notify_all();
      input_free_packet_list_not_empty_.notify_all();
    }
  });

  // We treat event setup as much as possible like a hidden part of creating the
  // CodecPtr.  If NewBinding() has !is_valid(), we rely on the Codec server to
  // close the Codec channel async.
  codec_.events().OnStreamFailed = fit::bind_member(this, &CodecClient::OnStreamFailed);
  codec_.events().OnInputConstraints = fit::bind_member(this, &CodecClient::OnInputConstraints);
  codec_.events().OnFreeInputPacket = fit::bind_member(this, &CodecClient::OnFreeInputPacket);
  codec_.events().OnOutputConstraints = fit::bind_member(this, &CodecClient::OnOutputConstraints);
  codec_.events().OnOutputFormat = fit::bind_member(this, &CodecClient::OnOutputFormat);
  codec_.events().OnOutputPacket = fit::bind_member(this, &CodecClient::OnOutputPacket);
  codec_.events().OnOutputEndOfStream = fit::bind_member(this, &CodecClient::OnOutputEndOfStream);

  // Bind sysmem_ using FIDL thread.  This is ok because all communication with
  // sysmem also happens via FIDL thread so will queue after this posted lambda.
  PostToFidlThread([this, sysmem = std::move(sysmem)]() mutable {
    zx_status_t bind_status = sysmem_.Bind(std::move(sysmem), dispatcher_);
    ZX_ASSERT(bind_status == ZX_OK);
  });
}

CodecClient::~CodecClient() { Stop(); }

fidl::InterfaceRequest<fuchsia::media::StreamProcessor> CodecClient::GetTheRequestOnce() {
  ZX_DEBUG_ASSERT(!is_start_called_);
  return std::move(temp_codec_request_);
}

// Can optionally be called before Start(), to set the min buffer size that'll
// be requested via sysmem.
void CodecClient::SetMinOutputBufferSize(uint64_t min_output_buffer_size) {
  ZX_DEBUG_ASSERT(!is_start_called_);
  min_output_buffer_size_ = min_output_buffer_size;
}

void CodecClient::SetMinOutputBufferCount(uint32_t min_output_buffer_count) {
  ZX_DEBUG_ASSERT(!is_start_called_);
  min_output_buffer_count_ = min_output_buffer_count;
}

void CodecClient::Start() {
  ZX_DEBUG_ASSERT(!is_start_called_);
  is_start_called_ = true;
  // The caller is responsible for calling this method only once, using the main
  // thread.  This method only holds the lock for short periods, and has to
  // release the lock many times during this method, which is reasonable given
  // the nature of this method as an overall state progression sequencer.

  // Call Sync() and wait for it's response _only_ to force the Codec server to
  // reach the point of being able response to messages, just for easier
  // debugging if just starting the Codec server fails instead.  Actual clients
  // don't need to use Sync() here.
  CallSyncAndWaitForResponse();
  FX_VLOGS(3) << "Sync() completed, which means the Codec server exists.";
  if (connection_lost_)
    return;

  FX_VLOGS(3) << "Waiting for OnInputConstraints() from the Codec server...";
  // The Codec client can rely on an OnInputConstraints() arriving shortly,
  // without any message required from the client first.  The
  // OnInputConstraints() may in future actually be sent by the CodecFactory,
  // but it'll still be sent to the client on the Codec channel in any case.
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    // In this example we're not paying attention to channel failure here
    // because channel failure calls exit().
    while (!input_constraints_) {
      input_constraints_exist_condition_.wait(lock);
    }
  }  // ~lock
  ZX_ASSERT(input_constraints_);
  FX_VLOGS(3) << "Got OnInputConstraints() from the Codec server.";

  // We know input_constraints_ won't change outside the lock because we prevent
  // that in OnInputConstraints() by only accepting input constraints if there
  // aren't already input constraints.

  // Now that we have input constraints, we can create all the input buffers and
  // tell the Codec server about them.  We tell the Codec server by using
  // SetInputSettings() followed by one or num_buffers calls to
  // AddInputBuffer().  These are necessary before it becomes permissible to
  // call CreateStream().
  //
  // We're not on the FIDL thread, so we need to async::PostTask() over to the
  // FIDL thread to send any FIDL message.

  uint32_t input_packet_count;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  if (!ConfigurePortBufferCollection(false, kInputBufferLifetimeOrdinal,
                                     input_constraints_->buffer_constraints_version_ordinal(),
                                     &input_packet_count, &input_buffer_collection_,
                                     &buffer_collection_info)) {
    FX_LOGS(FATAL) << "ConfigurePortBufferCollection failed (input)";
  }

  ZX_ASSERT(input_free_packet_bits_.empty());
  input_free_packet_bits_.resize(input_packet_count, true);
  all_input_buffers_.reserve(buffer_collection_info.buffer_count);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; i++) {
    std::unique_ptr<CodecBuffer> local_buffer = CodecBuffer::CreateFromVmo(
        i, std::move(buffer_collection_info.buffers[i].vmo),
        buffer_collection_info.buffers[i].vmo_usable_start,
        buffer_collection_info.settings.buffer_settings.size_bytes, true,
        buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
    if (!local_buffer) {
      FX_LOGS(FATAL) << "CodecBuffer::CreateFromVmo() failed";
    }
    ZX_ASSERT(all_input_buffers_.size() == i);
    all_input_buffers_.push_back(std::move(local_buffer));
  }

  // Now that we've SetInputBufferPartialSettings(), the codec will get the
  // input buffers from sysmem.  The input packets all start as free with the
  // Codec client, per protocol.  Same goes for input buffers - this client
  // happens to track in terms of packets and have buffer_index == packet_index.
  //
  // TODO(dustingreen): Have CodecClient scramble the order of packets vs.
  // buffers to check that CodecImpl is handling that correctly for input
  // packets.
  input_free_packet_list_.reserve(input_packet_count);
  for (uint32_t i = 0; i < input_packet_count; i++) {
    input_free_packet_list_.push_back(i);
  }
  input_packet_index_to_buffer_index_.resize(input_packet_count);
  input_free_buffer_list_.reserve(buffer_collection_info.buffer_count);
  for (uint32_t i = 0; i < buffer_collection_info.buffer_count; ++i) {
    input_free_buffer_list_.push_back(i);
  }

  // Shuffle both free lists, so that we'll notice if a StreamProcessor server has inappropriate
  // dependency on ordering of either list or any particular association of packet_index with
  // buffer_index.
  std::random_device random_device;
  std::mt19937 prng(random_device());
  std::shuffle(input_free_packet_list_.begin(), input_free_packet_list_.end(), prng);
  std::shuffle(input_free_buffer_list_.begin(), input_free_buffer_list_.end(), prng);
}

bool CodecClient::CreateAndSyncBufferCollection(
    fuchsia::sysmem::BufferCollectionSyncPtr* out_buffer_collection,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>* out_codec_sysmem_token) {
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> codec_sysmem_token;

  // Create client_token which will get converted into out_buffer_collection.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr client_token;
  fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> client_token_request =
      client_token.NewRequest();

  // Create codec_sysmem_token that'll get returned via out_codec_sysmem_token.
  client_token->Duplicate(std::numeric_limits<uint32_t>::max(), codec_sysmem_token.NewRequest());

  // client_token gets converted into a buffer_collection.
  //
  // Start client_token connection and start converting it into a
  // BufferCollection, so we can Sync() the previous Duplicate().
  PostToFidlThread([this, client_token_request = std::move(client_token_request),
                    client_token = client_token.Unbind(),
                    buffer_collection_request = buffer_collection.NewRequest()]() mutable {
    if (!sysmem_) {
      return;
    }
    sysmem_->AllocateSharedCollection(std::move(client_token_request));
    // codec_sysmem_token will be known to sysmem by the time client_token
    // closure is seen by sysmem, which in turn is before
    // buffer_collection_request will be hooked up, which is why
    // buffer_collection->Sync() completion below is enough to prove that
    // sysmem knows about codec_sysmem_token before codec_sysmem_token is
    // sent to the codec.
    sysmem_->BindSharedCollection(std::move(client_token), std::move(buffer_collection_request));
  });

  // After Sync() completes its round trip, we know that sysmem knows about
  // codec_sysmem_token (causally), which is important because we'll shortly
  // send codec_sysmem_token to the codec which will use codec_sysmem_token via
  // a different sysmem channel.
  zx_status_t sync_status = buffer_collection->Sync();
  if (sync_status != ZX_OK) {
    FX_PLOGS(FATAL, sync_status) << "buffer_collection->Sync() failed";
  }

  *out_buffer_collection = std::move(buffer_collection);
  *out_codec_sysmem_token = std::move(codec_sysmem_token);
  return true;
}

bool CodecClient::WaitForSysmemBuffersAllocated(
    bool is_output, fuchsia::sysmem::BufferCollectionSyncPtr* buffer_collection_param,
    fuchsia::sysmem::BufferCollectionInfo_2* out_buffer_collection_info) {
  // The style guide doesn't like non-const &, but the code in this method is
  // easier to read with a non-const &, so treat it that way within this method.
  fuchsia::sysmem::BufferCollectionSyncPtr& buffer_collection = *buffer_collection_param;
  fuchsia::sysmem::BufferCollectionInfo_2 result_buffer_collection_info;

  // It's not permitted to send input data until the client knows that sysmem
  // is done allocating.  It's not required that the client know that the
  // codec knows that sysmem is done allocating though - the server will
  // verify that sysmem is done by communicating with sysmem directly as
  // needed.
  zx_status_t allocate_status;
  zx_status_t call_status =
      buffer_collection->WaitForBuffersAllocated(&allocate_status, &result_buffer_collection_info);
  if (call_status != ZX_OK) {
    FX_PLOGS(ERROR, call_status) << "WaitForBuffersAllocated returned failure";
    return false;
  }
  if (allocate_status != ZX_OK) {
    FX_PLOGS(ERROR, allocate_status) << "WaitForBuffersAllocated allocation failed";
    return false;
  }

  // This is a little noisy, but it can be useful to see how many buffers are being used.  Make sure
  // it doesn't show up on stderr though.
  std::cout << "WaitForSysmemBuffersAllocated() done - is_output: " << is_output
            << " buffer_count: " << result_buffer_collection_info.buffer_count << std::endl;

  *out_buffer_collection_info = std::move(result_buffer_collection_info);
  return true;
}

void CodecClient::Stop() {
  ZX_DEBUG_ASSERT(thrd_current() != loop_thread_);
  OneShotEvent unbind_and_loop_lambdas_done;
  PostToFidlThread([this, &unbind_and_loop_lambdas_done] {
    if (codec_.is_bound()) {
      codec_.Unbind();
    }
    if (sysmem_.is_bound()) {
      sysmem_.Unbind();
    }
    if (input_buffer_collection_.is_bound()) {
      input_buffer_collection_.Unbind();
    }
    if (output_buffer_collection_.is_bound()) {
      output_buffer_collection_.Unbind();
    }
    // Any lambdas previously queued (by any handlers for the bindings we're
    // unbinding just above) need to be done also, so fence those by re-posting.
    //
    // This relies on lambdas on fidl thread (other than this one) to not
    // re-post to the fidl thread, which we enforce for all calls to
    // PostToFidlThread() except this one.
    PostToFidlThread([&unbind_and_loop_lambdas_done] { unbind_and_loop_lambdas_done.Signal(); },
                     false);
  });
  unbind_and_loop_lambdas_done.Wait();
}

void CodecClient::DoNotQueueInputPacketAfterAll(std::unique_ptr<fuchsia::media::Packet> packet) {
  ZX_ASSERT(packet->has_header());
  ZX_ASSERT(packet->header().has_buffer_lifetime_ordinal());
  ZX_ASSERT(packet->header().has_packet_index());
  ZX_ASSERT(packet->has_buffer_index());
  ZX_ASSERT(packet->has_stream_lifetime_ordinal());
  ZX_ASSERT(packet->has_start_offset());
  ZX_ASSERT(packet->has_valid_length_bytes());
  // timestamp_ish field is optional start_access_unit field is optional
  // known_end_access_unit is optional
  {  // scope lock
    // This packet is already not on the free list, but is still considered free
    // from a protocol point of view, so update that part.
    std::unique_lock<std::mutex> lock(lock_);
    ZX_ASSERT(input_free_packet_bits_[packet->header().packet_index()]);
    input_free_packet_bits_[packet->header().packet_index()] = false;
    // From here it's as if this packet is already in flight with the server.
  }  // ~lock
  async::PostTask(dispatcher_, [this, packet = std::move(packet)]() mutable {
    // Instead of codec_->QueueInputPacket().
    OnFreeInputPacket(std::move(*packet->mutable_header()));
    // ~packet
  });
}

void CodecClient::PostToFidlThread(fit::closure to_run, bool enforce_no_re_posting) {
  ZX_DEBUG_ASSERT(thrd_current() != loop_thread_ || !enforce_no_re_posting);
  zx_status_t post_status = async::PostTask(dispatcher_, std::move(to_run));
  ZX_ASSERT(post_status == ZX_OK);
}

void CodecClient::CallSyncAndWaitForResponse() {
  // |is_sync_complete_condition_| may also be signaled on connection lost, so it
  // needs to be an instance variable.
  bool is_sync_complete = false;
  // Capturing stuff with just "&" is sometimes frowned upon, but in this case
  // there's no chance of any lambda outliving anything, so it's fine.  The
  // outer lambda is because ProxyController isn't thread-safe and the present
  // method is called from the main thread not the FIDL thread, so we have to
  // switch threads to send a FIDL message.  The inner lambda is the completion
  // callback.
  FX_VLOGS(3) << "before calling Sync() (main thread)...";
  async::PostTask(dispatcher_, [&] {
    FX_VLOGS(3) << "before calling Sync() (fidl thread)...";
    codec_->Sync([&]() {
      {  // scope lock
        std::unique_lock<std::mutex> lock(is_sync_complete_lock_);
        is_sync_complete = true;
      }  // ~lock
      is_sync_complete_condition_.notify_all();
    });
  });
  FX_VLOGS(3) << "after calling Sync() - waiting...\n";
  {  // scope lock
    std::unique_lock<std::mutex> lock(is_sync_complete_lock_);
    // We rely on the channel error handler to be doing an exit() for this loop
    // to be reasonable without checking for channel failure here.
    while (!is_sync_complete && !connection_lost_) {
      is_sync_complete_condition_.wait(lock);
    }
  }
  FX_VLOGS(3) << "after calling Sync() - done waiting\n";
  ZX_ASSERT(is_sync_complete || connection_lost_);
}

void CodecClient::TrackOutputStreamLifetimeOrdinal(uint64_t output_stream_lifetime_ordinal) {
  // must be odd
  ZX_ASSERT(output_stream_lifetime_ordinal % 2 == 1);
  ZX_ASSERT(output_stream_lifetime_ordinal >= output_stream_lifetime_ordinal_);
  if (output_stream_lifetime_ordinal > output_stream_lifetime_ordinal_) {
    // We're allowed to forget format any time there's a stream change, so we
    // do.  This isn't critical for this test code, but it's closer to how a
    // real client will likely track the output format on a per-stream basis.
    ZX_ASSERT(!last_output_format_ ||
              last_output_format_->stream_lifetime_ordinal() == output_stream_lifetime_ordinal_);
    output_stream_lifetime_ordinal_ = output_stream_lifetime_ordinal;
    last_output_format_ = nullptr;
    // We intentionally don't reset is_packet_since_last_format_.
  }
}

void CodecClient::OnInputConstraints(fuchsia::media::StreamBufferConstraints input_constraints) {
  if (input_constraints_) {
    FX_LOGS(FATAL) << "server sent more than one input constraints";
  }
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    input_constraints_ =
        std::make_unique<fuchsia::media::StreamBufferConstraints>(std::move(input_constraints));
  }  // ~lock
  input_constraints_exist_condition_.notify_all();
}

void CodecClient::OnFreeInputPacket(fuchsia::media::PacketHeader free_input_packet) {
  bool free_buffer_list_was_empty;
  bool free_packet_list_was_empty;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (!free_input_packet.has_packet_index()) {
      FX_LOGS(FATAL) << "OnFreeInputPacket(): Packet has no index.";
    }
    if (input_free_packet_bits_[free_input_packet.packet_index()]) {
      // already free - a normal client wouldn't want to just exit here since
      // this is the server's fault - in this example we just care that we
      // detect it
      FX_LOGS(FATAL) << "OnFreeInputPacket() when already free - server's fault? - "
                        "packet_index: "
                     << free_input_packet.packet_index();
    }
    free_buffer_list_was_empty = input_free_buffer_list_.empty();
    input_free_buffer_list_.push_back(
        input_packet_index_to_buffer_index_[free_input_packet.packet_index()]);
    free_packet_list_was_empty = input_free_packet_list_.empty();
    input_free_packet_list_.push_back(free_input_packet.packet_index());
    input_free_packet_bits_[free_input_packet.packet_index()] = true;
  }  // ~lock
  if (free_buffer_list_was_empty) {
    input_free_buffer_list_not_empty_.notify_all();
  }
  if (free_packet_list_was_empty) {
    input_free_packet_list_not_empty_.notify_all();
  }
}

std::unique_ptr<fuchsia::media::Packet> CodecClient::BlockingGetFreeInputPacket() {
  // This should be significantly longer than kWatchdogTimeoutMs in amlogic_decoder watchdog.cc.
  const uint32_t kBlockingGetFreeInputPacketTimeoutMs = 20000;
  auto now = std::chrono::system_clock::now();
  auto wait_until_time = now + std::chrono::milliseconds(kBlockingGetFreeInputPacketTimeoutMs);
  uint32_t free_packet_index;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    while (input_free_packet_list_.empty()) {
      if (connection_lost_)
        return nullptr;
      auto wait_until_result = input_free_packet_list_not_empty_.wait_until(lock, wait_until_time);
      if (wait_until_result == std::cv_status::timeout) {
        ZX_PANIC(
            "BlockingGetFreeInputPacket() no packet available for too long - "
            "kBlockingGetFreeInputPacketTimeoutMs: %u\n",
            kBlockingGetFreeInputPacketTimeoutMs);
        // not reached
        return nullptr;
      }
      ZX_DEBUG_ASSERT(wait_until_result == std::cv_status::no_timeout);
    }
    free_packet_index = input_free_packet_list_.back();
    input_free_packet_list_.pop_back();
    // We intentionally do not modify input_free_packet_bits_ here, as those bits are
    // tracking the protocol level free-ness, so will get updated when the
    // caller queues the input packet.
    ZX_ASSERT(input_free_packet_bits_[free_packet_index]);
  }  // ~lock
  std::unique_ptr<fuchsia::media::Packet> packet = fuchsia::media::Packet::New();
  packet->mutable_header()->set_buffer_lifetime_ordinal(kInputBufferLifetimeOrdinal);
  packet->mutable_header()->set_packet_index(free_packet_index);
  return packet;
}

const CodecBuffer& CodecClient::BlockingGetFreeInputBufferForPacket(
    fuchsia::media::Packet* packet) {
  uint32_t free_buffer_index;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    while (input_free_buffer_list_.empty()) {
      input_free_buffer_list_not_empty_.wait(lock);
    }
    free_buffer_index = input_free_buffer_list_.back();
    input_free_buffer_list_.pop_back();
  }  // ~lock
  input_packet_index_to_buffer_index_[packet->header().packet_index()] = free_buffer_index;
  packet->set_buffer_index(free_buffer_index);
  return *all_input_buffers_[free_buffer_index].get();
}

const CodecBuffer& CodecClient::GetInputBufferByIndex(uint32_t packet_index) {
  return *all_input_buffers_[packet_index];
}

const CodecBuffer& CodecClient::GetOutputBufferByIndex(uint32_t packet_index) {
  return *all_output_buffers_[packet_index];
}

void CodecClient::QueueInputFormatDetails(uint64_t stream_lifetime_ordinal,
                                          fuchsia::media::FormatDetails input_format_details) {
  async::PostTask(dispatcher_, [this, stream_lifetime_ordinal,
                                input_format_details = std::move(input_format_details)]() mutable {
    codec_->QueueInputFormatDetails(stream_lifetime_ordinal, std::move(input_format_details));
  });
}

void CodecClient::QueueInputPacket(std::unique_ptr<fuchsia::media::Packet> packet) {
  ZX_ASSERT(packet->has_header());
  ZX_ASSERT(packet->header().has_buffer_lifetime_ordinal());
  ZX_ASSERT(packet->header().has_packet_index());
  ZX_ASSERT(packet->has_buffer_index());
  ZX_ASSERT(packet->has_stream_lifetime_ordinal());
  ZX_ASSERT(packet->has_start_offset());
  ZX_ASSERT(packet->has_valid_length_bytes());
  // timestamp_ish field is optional start_access_unit field is optional
  // known_end_access_unit is optional
  {  // scope lock
    // This packet is already not on the free list, but is still considered free
    // from a protocol point of view, so update that part.
    std::unique_lock<std::mutex> lock(lock_);
    ZX_ASSERT(input_free_packet_bits_[packet->header().packet_index()]);
    input_free_packet_bits_[packet->header().packet_index()] = false;
    // From here it's as if this packet is already in flight with the server.
  }  // ~lock
  async::PostTask(dispatcher_, [this, packet = std::move(packet)]() mutable {
    codec_->QueueInputPacket(std::move(*packet));
    // ~packet
  });
}

void CodecClient::QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) {
  async::PostTask(dispatcher_, [this, stream_lifetime_ordinal] {
    codec_->QueueInputEndOfStream(stream_lifetime_ordinal);
  });
}

void CodecClient::FlushEndOfStreamAndCloseStream(uint64_t stream_lifetime_ordinal) {
  async::PostTask(dispatcher_, [this, stream_lifetime_ordinal] {
    codec_->FlushEndOfStreamAndCloseStream(stream_lifetime_ordinal);
  });
}

std::unique_ptr<CodecOutput> CodecClient::BlockingGetEmittedOutput() {
  while (true) {
    // The rule is that a required pending constraints won't be followed by any
    // more output packets until it's no longer pending (in the sense that the
    // output buffers have been suitably re-configured).  We verify the server
    // is following that rule elsewhere, which means we know here that when both
    // packets are pending and constraints is pending, the packets were
    // delivered to the client first. So we drain the packets first.
    std::unique_ptr<CodecOutput> packet;
    std::shared_ptr<const fuchsia::media::StreamOutputConstraints> constraints;
    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);
      while (!output_pending_) {
        if (connection_lost_)
          return nullptr;
        output_pending_condition_.wait(lock);
      }
      if (!emitted_output_.empty()) {
        packet = std::move(emitted_output_.front());
        emitted_output_.pop_front();
        // This only does anything when the last packet is consumed and there is
        // no config pending.
        if (!ComputeOutputPendingLocked()) {
          output_pending_ = false;
        }
      } else {
        ZX_ASSERT(output_constraints_action_pending_);
        ZX_ASSERT(last_required_output_constraints_);
        constraints = last_required_output_constraints_;
      }
    }  // ~lock

    // Now we own a packet or have a required config to deal with, but not both,
    // so it doesn't matter which order we check here, but for clarity we check
    // in the same order as above.
    if (packet) {
      return packet;
    }

    // We have a required output config change to deal with here.

    // The server implicitly has relinquished ownership of all output packets
    // and all output buffers as a semantic of the required config change.  This
    // shouldn't really be thought of the packets being "emitted" - rather from
    // the server's point of view they're deallocated already.  Now it's the
    // client's turn to deallocate the old buffers.  It is not permitted to
    // re-use a previous buffer as a new buffer, per protocol rules, not even
    // for the same Codec instance, and not even for a mid-stream output config
    // change.
    //
    // The main mechanism used to detect that the server isn't sending output
    // too soon is output_config_action_pending_.  In contrast, the client code
    // in this example permits itself to send RecycleOutputPacket() after the
    // client has already seen OnOutputConstraints() with action required true,
    // even though the client could stop itself from doing so as a potential
    // optimization.  The client is allowed to send RecycleOutputPacket() up
    // until the implied ReleaseOutputBuffers() at the start of
    // SetOutputSettings().  Between then and a given packet_index becoming
    // emitted again (!free), a RecycleOutputPacket() for that packet_index is
    // forbidden.
    //
    // Because of the client allowing itself to send RecycleOutputPacket() for a
    // while longer than fundamentally necessary, we delay upkeep on
    // output_free_packet_bits_ until here.  This upkeep isn't really fundamentally
    // necessary between OnOutputConstraints() with action required true and the
    // last AddOutputBuffer() as part of output re-configuration, but ... this
    // explicit delayed upkeep _may_ help illustrate how it's acceptable for a
    // client to let the completion end of output processing send
    // RecycleOutputPacket() as long as all those will be sent before
    // SetOutputSettings().
    std::shared_ptr<const fuchsia::media::StreamOutputConstraints> snapped_constraints;
    uint64_t new_output_buffer_lifetime_ordinal;
    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);

      // We know this because the previous OnOutputConstraints() set this and
      // because we're only here if it's set.
      ZX_ASSERT(output_constraints_action_pending_);
      // We know this because we reject additional output from the server when
      // output_config_action_pending_ is true, and because we've drained all
      // previous output by this point.
      ZX_ASSERT(emitted_output_.empty());
      // We know this because we're only here if we have pending constraints.
      ZX_ASSERT(constraints);

      // Not really critical to do this, as we'll just end up setting these
      // back to true under the same lock hold interval as we set
      // output_config_action_pending_ to false, but see comment above re.
      // how this might help illustrate how late RecycleOutputPacket() can be
      // sent.
      //
      // Think of this assignment as slightly more than a comment in this
      // example, rather than any real need.
      output_free_packet_bits_.resize(0);

      // Free the old output buffers, if any.
      all_output_buffers_.clear();

      // Here is where we snap which exact constraints version we'll actually
      // use.
      //
      // For a client that's doing output buffer re-config on the FIDL thread
      // during OnOutputConstraints with action required true, this will always
      // just be the constraints being presently received.  But this example
      // shows how to drive the codec in a protocol-valid way without being
      // forced to perform buffer re-configuration on the FIDL thread.
      ZX_ASSERT(output_constraints_action_pending_);
      ZX_ASSERT(last_required_output_constraints_);
      ZX_ASSERT(last_output_constraints_);
      // We'll snap the last_output_constraints_, which is always at least as
      // recent as the last_required_output_constraints_.
      snapped_constraints = last_output_constraints_;
      ZX_ASSERT(snapped_constraints);
      new_output_buffer_lifetime_ordinal = next_output_buffer_lifetime_ordinal_;
      next_output_buffer_lifetime_ordinal_ += 2;
    }  // ~lock

    //
    // Tell the server about output settings.
    //

    ZX_ASSERT(snapped_constraints->has_buffer_constraints());
    const fuchsia::media::StreamBufferConstraints& buffer_constraints =
        snapped_constraints->buffer_constraints();

    uint32_t packet_count;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
    if (!ConfigurePortBufferCollection(true, new_output_buffer_lifetime_ordinal,
                                       buffer_constraints.buffer_constraints_version_ordinal(),
                                       &packet_count, &output_buffer_collection_,
                                       &buffer_collection_info)) {
      FX_LOGS(FATAL) << "ConfigurePortBufferCollection failed (output)";
    }

    // Configure tracking for output buffers.
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);

      all_output_buffers_.reserve(packet_count);
      for (uint32_t i = 0; i < packet_count; i++) {
        std::unique_ptr<CodecBuffer> buffer = CodecBuffer::CreateFromVmo(
            i, std::move(buffer_collection_info.buffers[i].vmo),
            buffer_collection_info.buffers[i].vmo_usable_start,
            buffer_collection_info.settings.buffer_settings.size_bytes, true,
            buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
        if (!buffer) {
          FX_LOGS(FATAL) << "CodecBuffer::Allocate() failed (output)";
        }
        ZX_ASSERT(all_output_buffers_.size() == i);
        all_output_buffers_.push_back(std::move(buffer));

        if (i == packet_count - 1) {
          output_free_packet_bits_.resize(packet_count, true);
        }
      }

      current_output_buffer_lifetime_ordinal_ = new_output_buffer_lifetime_ordinal;
    }  // ~lock

    // We're ready to receive output.
    PostToFidlThread([this, output_buffer_lifetime_ordinal = new_output_buffer_lifetime_ordinal] {
      if (!codec_) {
        return;
      }
      if (output_buffer_lifetime_ordinal != current_output_buffer_lifetime_ordinal_) {
        return;
      }
      codec_->CompleteOutputBufferPartialSettings(output_buffer_lifetime_ordinal);
    });

    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);

      // So, now that we're done with that output re-config, it's time to see if
      // that re-config was the last one we need to do, or if there's a newer
      // config that's action-required.

      if (snapped_constraints->buffer_constraints().buffer_constraints_version_ordinal() >=
          last_required_output_constraints_->buffer_constraints()
              .buffer_constraints_version_ordinal()) {
        // Good.  The client is caught up.  The output_config_action_pending_
        // can become false here, but may very shortly become true again if
        // another OnOutputConstraints() is received after we release the lock
        // (roughly speaking; see code).
        //
        // It's ok that we didn't set output_config_action_pending_ to false
        // before sending the last AddOutputBuffer() above, because
        // OnOutputConstraints() was still able to update
        // last_required_output_config_ as needed, which it's been able to do
        // all along during most of this whole method.  If we had set to false
        // up there, it would probably be less obvious why it works vs. here,
        // but either can work.
        FX_VLOGS(3) << "output_config_action_pending_ = false, because client "
                       "caught up";
        output_constraints_action_pending_ = false;
        // Because this was true for at least pending config reason which we
        // are only just clearing immediately above.
        ZX_ASSERT(output_pending_);
        // There can be output packets by this point so only clear
        // output_pending_ if there are also no packets.
        if (!ComputeOutputPendingLocked()) {
          output_pending_ = false;
        }
      } else {
        // We've received and even more recent constraints that's
        // action-required, so go around again without clearing
        // output_constraints_action_pending_ or output_pending_. Both remain
        // true until we've caught up to a config that's at least as new as the
        // last_required_output_constraints_.
        FX_VLOGS(3) << "output_constraints_action_pending_ remains true because server "
                       "has sent yet another action-required output constraints";
        ZX_ASSERT(output_constraints_action_pending_);
        ZX_ASSERT(output_pending_);
      }
    }  // ~lock
  }
}

bool CodecClient::ConfigurePortBufferCollection(
    bool is_output, uint64_t new_buffer_lifetime_ordinal,
    uint64_t buffer_constraints_version_ordinal, uint32_t* out_packet_count,
    fuchsia::sysmem::BufferCollectionPtr* out_buffer_collection,
    fuchsia::sysmem::BufferCollectionInfo_2* out_buffer_collection_info) {
  fuchsia::media::StreamBufferPartialSettings settings;
  settings.set_buffer_lifetime_ordinal(new_buffer_lifetime_ordinal);
  settings.set_buffer_constraints_version_ordinal(buffer_constraints_version_ordinal);

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> codec_sysmem_token;
  if (!CreateAndSyncBufferCollection(&buffer_collection, &codec_sysmem_token)) {
    FX_LOGS(FATAL) << "CreateAndSyncBufferCollection failed (output)";
    return false;
  }

  settings.set_sysmem_token(std::move(codec_sysmem_token));

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  // TODO(fxbug.dev/24581): Hardcoded to read/write to allow direct Vulkan import on
  // UMA platforms.
  if (!is_output && is_input_secure_) {
    constraints.usage.video = fuchsia::sysmem::videoUsageDecryptorOutput;
  } else if (is_output && is_output_secure_) {
    // Only used if kVerifySecureOutput is used in test.
    constraints.usage.cpu =
        fuchsia::sysmem::cpuUsageReadOften | fuchsia::sysmem::cpuUsageWriteOften;
  } else {
    constraints.usage.cpu =
        fuchsia::sysmem::cpuUsageReadOften | fuchsia::sysmem::cpuUsageWriteOften;
  }

  // TODO(dustingreen): Make this more flexible once we're more flexible on
  // frame_count on output of decoder.
  if (is_output) {
    constraints.min_buffer_count_for_camping = kMinInputBufferCountForCamping;
  } else {
    constraints.min_buffer_count_for_camping = kMinOutputBufferCountForCamping;
  }
  ZX_DEBUG_ASSERT(constraints.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(constraints.min_buffer_count_for_shared_slack == 0);

  // 0 is treated as 0xFFFFFFFF.
  ZX_DEBUG_ASSERT(constraints.max_buffer_count == 0);

  constraints.has_buffer_memory_constraints = true;
  // Sysmem has a built-in min_size_bytes of 1, so no need to really constrain
  // min_size_bytes here, unless output, in which case setting a min_size_bytes
  // allows for seamless video frame dimension changes.  If the client code says
  // 0 that's fine.
  //
  // Similar for min_buffer_count, but min of 0 means 0 / no constraint in that case.
  ZX_DEBUG_ASSERT(constraints.buffer_memory_constraints.min_size_bytes == 0);
  ZX_DEBUG_ASSERT(constraints.min_buffer_count == 0);
  if (is_output) {
    constraints.buffer_memory_constraints.min_size_bytes = min_output_buffer_size_;
    constraints.min_buffer_count = min_output_buffer_count_;
  }
  constraints.buffer_memory_constraints.max_size_bytes = std::numeric_limits<uint32_t>::max();
  constraints.buffer_memory_constraints.physically_contiguous_required = false;
  constraints.buffer_memory_constraints.secure_required = false;
  if (is_output && is_output_secure_) {
    constraints.buffer_memory_constraints.inaccessible_domain_supported = true;
  } else if (!is_output && is_input_secure_) {
    constraints.buffer_memory_constraints.cpu_domain_supported = false;
    constraints.buffer_memory_constraints.ram_domain_supported = false;
    constraints.buffer_memory_constraints.inaccessible_domain_supported = true;
    constraints.buffer_memory_constraints.secure_required = true;
    constraints.buffer_memory_constraints.heap_permitted_count = 1;
    constraints.buffer_memory_constraints.heap_permitted[0] =
        fuchsia::sysmem::HeapType::AMLOGIC_SECURE_VDEC;
  } else {
    ZX_DEBUG_ASSERT(!constraints.buffer_memory_constraints.inaccessible_domain_supported);
    ZX_DEBUG_ASSERT(constraints.buffer_memory_constraints.cpu_domain_supported);
  }

  ZX_DEBUG_ASSERT(!constraints.buffer_memory_constraints.ram_domain_supported);

  // Despite being a consumer of output uncompressed video frames (when decoding
  // video and is_output), for now we intentionally don't constrain to the
  // PixelFormatType(s) that we can consume, and instead fail later if we get
  // something unexpected on output. That's just easier than plumbing
  // PixelFormatType(s) to here for now.
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);

  PostToFidlThread([this, is_output, settings = std::move(settings)]() mutable {
    if (!codec_) {
      return;
    }
    if (is_output) {
      codec_->SetOutputBufferPartialSettings(std::move(settings));
    } else {
      codec_->SetInputBufferPartialSettings(std::move(settings));
    }
  });

  buffer_collection->SetConstraints(true, std::move(constraints));

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
  // This borrows buffer_collection during
  // the call.
  if (!WaitForSysmemBuffersAllocated(is_output, &buffer_collection, &buffer_collection_info)) {
    FX_LOGS(FATAL) << "WaitForSysmemBuffer"
                      "sAllocated failed";
    return false;
  }

  if (!in_lax_mode_) {
    // We don't expect normal cases to go above kMaxExpectedBufferCount, but if
    // min_output_buffer_count_ forces higher buffer counts, that's fine.
    ZX_ASSERT(buffer_collection_info.buffer_count <= kMaxExpectedBufferCount ||
              min_output_buffer_count_ > kMaxExpectedBufferCount);
  }

  fuchsia::sysmem::BufferCollectionPtr buffer_collection_ptr;

  // For the Bind() we probably don't strictly need to be on FIDL thread, so do
  // the Bind() here.  This does mean we need to set the error handler before
  // the Bind() however.
  buffer_collection_ptr.set_error_handler([is_output](zx_status_t status) {
    FX_PLOGS(FATAL, status) << "BufferCollection failed is_output: " << is_output;
  });

  // This implicitly converts buffer_collection from
  // fidl::SynchronousInterfacePtr<> to fidl::InterfaceHandle<>, which is what
  // we want; we're moving handling of the BufferCollection from this thread to
  // the FIDL thread.
  zx_status_t bind_status = buffer_collection_ptr.Bind(std::move(buffer_collection), dispatcher_);
  if (bind_status != ZX_OK) {
    FX_PLOGS(FATAL, bind_status) << "buffer_collection_ptr.Bind() failed is_output: " << is_output;
    return false;
  }

  *out_packet_count = buffer_collection_info.buffer_count;
  *out_buffer_collection = std::move(buffer_collection_ptr);
  *out_buffer_collection_info = std::move(buffer_collection_info);
  return true;
}

void CodecClient::RecycleOutputPacket(fuchsia::media::PacketHeader free_packet) {
  ZX_ASSERT(free_packet.has_packet_index());
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    output_free_packet_bits_[free_packet.packet_index()] = true;
  }  // ~lock
  async::PostTask(dispatcher_, [this, free_packet = std::move(free_packet)]() mutable {
    codec_->RecycleOutputPacket(std::move(free_packet));
  });
}

void CodecClient::OnOutputConstraints(fuchsia::media::StreamOutputConstraints output_constraints) {
  bool output_pending_notify_needed = false;
  // Not that the std::move() actaully helps here, but that's what we're doing.
  std::shared_ptr<fuchsia::media::StreamOutputConstraints> shared_constraints =
      std::make_shared<fuchsia::media::StreamOutputConstraints>(std::move(output_constraints));
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (!shared_constraints->has_stream_lifetime_ordinal()) {
      FX_LOGS(FATAL) << "StreamOutputConstraints missing stream_lifetime_ordinal";
    }
    uint64_t stream_lifetime_ordinal = shared_constraints->stream_lifetime_ordinal();
    TrackOutputStreamLifetimeOrdinal(stream_lifetime_ordinal);

    ZX_ASSERT(
        !last_output_constraints_ ||
        (last_output_constraints_->has_buffer_constraints() &&
         last_output_constraints_->buffer_constraints().has_buffer_constraints_version_ordinal()));
    uint64_t previous_buffer_constraints_version_ordinal =
        last_output_constraints_
            ? last_output_constraints_->buffer_constraints().buffer_constraints_version_ordinal()
            : 0;
    ZX_ASSERT(shared_constraints->has_buffer_constraints() &&
              shared_constraints->buffer_constraints().has_buffer_constraints_version_ordinal());
    if (shared_constraints->buffer_constraints().buffer_constraints_version_ordinal() <
        previous_buffer_constraints_version_ordinal) {
      FX_LOGS(FATAL) << "broken server sent badly ordered buffer constraints ordinals";
    }
    if ((shared_constraints->has_buffer_constraints_action_required() &&
         shared_constraints->buffer_constraints_action_required()) &&
        shared_constraints->buffer_constraints().buffer_constraints_version_ordinal() <=
            previous_buffer_constraints_version_ordinal) {
      FX_LOGS(FATAL) << "broken server sent buffer_constraints_action_required without "
                        "increasingbuffer_constraints_version_ordinal";
    }
    last_output_constraints_ = shared_constraints;
    FX_VLOGS(3) << "OnOutputConstraints buffer_constraints_version_ordinal: "
                << shared_constraints->buffer_constraints().buffer_constraints_version_ordinal()
                << "buffer_constraints_action_required: "
                << shared_constraints->buffer_constraints_action_required();
    if (shared_constraints->buffer_constraints_action_required()) {
      last_required_output_constraints_ = shared_constraints;
      // A client is allowed to forget the output format on any action required
      // buffer constraints, so forget here.
      last_output_format_ = nullptr;
      FX_VLOGS(3) << "output_config_action_pending_ = true, because received a "
                     "buffer_constraints_action_required constraints\n";
      output_constraints_action_pending_ = true;
      if (!output_pending_) {
        output_pending_ = true;
        output_pending_notify_needed = true;
      }
    }
  }  // ~lock
  if (output_pending_notify_needed) {
    output_pending_condition_.notify_all();
  }
}

void CodecClient::OnOutputFormat(fuchsia::media::StreamOutputFormat output_format) {
  // We don't need to notify output_pending_condition_ since in contrast to
  // constraints we don't need to take action, and nobody cares about the format
  // until the next packet arrives.
  std::shared_ptr<fuchsia::media::StreamOutputFormat> shared_format =
      std::make_shared<fuchsia::media::StreamOutputFormat>(std::move(output_format));
  std::unique_lock<std::mutex> lock(lock_);

  if (!shared_format->has_stream_lifetime_ordinal()) {
    FX_LOGS(FATAL) << "StreamOutputFormat missing stream_lifetime_ordinal";
  }
  uint64_t stream_lifetime_ordinal = shared_format->stream_lifetime_ordinal();
  TrackOutputStreamLifetimeOrdinal(stream_lifetime_ordinal);

  if (is_format_since_last_packet_) {
    // A server can easily elide unnecessary OnOutputFormat by not sending
    // OnOutputFormat until immediately before OnOutputPacket.  The format is
    // logically part of each output packet, with the optimization that we only
    // send output format when it changes, in between packets, to avoid needing
    // to send output format with every packet.
    FX_LOGS(FATAL) << "broken server sent two OnOutputFormat() in a row";
  }
  if (!shared_format->has_stream_lifetime_ordinal()) {
    FX_LOGS(FATAL) << "OnOutputFormat !has_stream_lifetime_ordinal()";
  }
  if (!shared_format->has_format_details()) {
    FX_LOGS(FATAL) << "OnOutputFormat !has_format_details()";
  }
  last_output_format_ = shared_format;
  is_format_since_last_packet_ = true;
}

void CodecClient::OnOutputPacket(fuchsia::media::Packet output_packet, bool error_detected_before,
                                 bool error_detected_during) {
  FX_CHECK(output_packet.has_header());
  FX_CHECK(output_packet.has_stream_lifetime_ordinal());
  FX_CHECK(output_packet.header().has_packet_index());
  bool output_pending_notify_needed = false;
  std::unique_ptr<const fuchsia::media::Packet> local_packet =
      std::make_unique<fuchsia::media::Packet>(std::move(output_packet));
  uint32_t packet_index = local_packet->header().packet_index();

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (!local_packet->has_stream_lifetime_ordinal()) {
      FX_LOGS(FATAL) << "Packet missing stream_lifetime_ordinal";
    }
    uint64_t stream_lifetime_ordinal = local_packet->stream_lifetime_ordinal();
    TrackOutputStreamLifetimeOrdinal(stream_lifetime_ordinal);
    if (!last_output_format_ ||
        last_output_format_->stream_lifetime_ordinal() != stream_lifetime_ordinal) {
      FX_LOGS(FATAL) << "OnOutputFormat required before OnOutputPacket, per-stream";
    }

    std::unique_ptr<CodecOutput> output =
        std::make_unique<CodecOutput>(stream_lifetime_ordinal, last_output_constraints_,
                                      last_output_format_, std::move(local_packet), false);
    if (output_constraints_action_pending_) {
      // FWIW, we wouldn't be able to detect this if we were using the
      // async::Loop thread to perform output buffer re-configuration.
      FX_LOGS(FATAL) << "server incorrectly sent output packet while required "
                        "constraints change pending";
    }
    if (!output_free_packet_bits_[packet_index]) {
      // The packet was emitted twice by the server without it becoming free in
      // between, which is broken server behavior.
      FX_LOGS(FATAL) << "server incorrectly emitted an output packet without it becoming "
                        "free in between";
    }
    // Emitted by server, so not free until later when we send it back to server
    // with RecycleOutputPacket(), or until we re-configure output buffers in
    // which case all the output packets start free with the server.
    output_free_packet_bits_[packet_index] = false;
    emitted_output_.push_back(std::move(output));
    is_format_since_last_packet_ = false;
    if (!output_pending_) {
      output_pending_ = true;
      output_pending_notify_needed = true;
    }
  }  // ~lock
  if (output_pending_notify_needed) {
    output_pending_condition_.notify_all();
  }
}

void CodecClient::OnOutputEndOfStream(uint64_t stream_lifetime_ordinal,
                                      bool error_detected_before) {
  bool output_pending_notify_needed = false;
  std::unique_ptr<CodecOutput> output =
      std::make_unique<CodecOutput>(stream_lifetime_ordinal, nullptr, nullptr, nullptr, true);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    if (output_constraints_action_pending_) {
      // FWIW, we wouldn't be able to detect this if we were using the
      // async::Loop thread to perform output buffer re-configuration.
      FX_LOGS(FATAL) << "server incorrectly sent OnOutputEndOfStream() while "
                        "required constraints change pending";
    }
    emitted_output_.push_back(std::move(output));
    if (!output_pending_) {
      output_pending_ = true;
      output_pending_notify_needed = true;
    }
  }  // ~lock
  if (output_pending_notify_needed) {
    output_pending_condition_.notify_all();
  }
}

void CodecClient::OnStreamFailed(uint64_t stream_lifetime_ordinal,
                                 fuchsia::media::StreamError error) {
  FX_LOGS(FATAL) << "OnStreamFailed: stream_lifetime_ordinal: " << stream_lifetime_ordinal
                 << " error: " << std::hex << static_cast<uint32_t>(error);
}
