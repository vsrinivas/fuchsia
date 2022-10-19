// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-audio-stream.h"

#include <lib/ddk/device.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmar.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <usb/audio.h>
#include <usb/usb-request.h>

#include "src/lib/digest/digest.h"
#include "usb-audio-device.h"
#include "usb-audio-stream-interface.h"
#include "usb-audio.h"

namespace audio {
namespace usb {

namespace audio_fidl = fuchsia_hardware_audio;

static constexpr uint32_t MAX_OUTSTANDING_REQ = 6;

UsbAudioStream::UsbAudioStream(UsbAudioDevice* parent, std::unique_ptr<UsbAudioStreamInterface> ifc)
    : UsbAudioStreamBase(parent->zxdev()),
      AudioStreamProtocol(ifc->direction() == Direction::Input),
      parent_(*parent),
      ifc_(std::move(ifc)),
      create_time_(zx::clock::get_monotonic().get()),
      loop_(&kAsyncLoopConfigNeverAttachToThread) {
  snprintf(log_prefix_, sizeof(log_prefix_), "UsbAud %04hx:%04hx %s-%03d", parent_.vid(),
           parent_.pid(), is_input() ? "input" : "output", ifc_->term_link());
  loop_.StartThread("usb-audio-stream-loop");

  root_ = inspect_.GetRoot().CreateChild("usb_audio_stream");
  state_ = root_.CreateString("state", "created");
  number_of_stream_channels_ = root_.CreateUint("number_of_stream_channels", 0);
  start_time_ = root_.CreateInt("start_time", 0);
  position_request_time_ = root_.CreateInt("position_request_time", 0);
  position_reply_time_ = root_.CreateInt("position_reply_time", 0);
  frames_requested_ = root_.CreateUint("frames_requested", 0);
  ring_buffer_size2_ = root_.CreateUint("ring_buffer_size", 0);
  usb_requests_sent_ = root_.CreateUint("usb_requests_sent", 0);
  usb_requests_outstanding_ = root_.CreateInt("usb_requests_outstanding", 0);

  frame_rate_ = root_.CreateUint("frame_rate", 0);
  bits_per_slot_ = root_.CreateUint("bits_per_slot", 0);
  bits_per_sample_ = root_.CreateUint("bits_per_sample", 0);
  sample_format_ = root_.CreateString("sample_format", "not_set");

  size_t number_of_formats = ifc_->formats().size();
  supported_min_number_of_channels_ =
      root_.CreateUintArray("supported_min_number_of_channels", number_of_formats);
  supported_max_number_of_channels_ =
      root_.CreateUintArray("supported_max_number_of_channels", number_of_formats);
  supported_min_frame_rates_ =
      root_.CreateUintArray("supported_min_frame_rates", number_of_formats);
  supported_max_frame_rates_ =
      root_.CreateUintArray("supported_max_frame_rates", number_of_formats);
  supported_bits_per_slot_ = root_.CreateUintArray("supported_bits_per_slot", number_of_formats);
  supported_bits_per_sample_ =
      root_.CreateUintArray("supported_bits_per_sample", number_of_formats);
  supported_sample_formats_ =
      root_.CreateStringArray("supported_sample_formats", number_of_formats);

  size_t count = 0;
  for (auto i : ifc_->formats()) {
    supported_min_number_of_channels_.Set(count, i.range_.min_channels);
    supported_max_number_of_channels_.Set(count, i.range_.max_channels);
    supported_min_frame_rates_.Set(count, i.range_.min_frames_per_second);
    supported_max_frame_rates_.Set(count, i.range_.max_frames_per_second);
    std::vector<utils::Format> formats = utils::GetAllFormats(i.range_.sample_formats);
    // Each UsbAudioStreamInterface formats() entry only reports one format.
    ZX_ASSERT(formats.size() == 1);
    utils::Format format = formats[0];
    supported_bits_per_slot_.Set(count, format.bytes_per_sample * 8);
    supported_bits_per_sample_.Set(count, format.valid_bits_per_sample);
    switch (format.format) {
      case audio_fidl::wire::SampleFormat::kPcmSigned:
        supported_sample_formats_.Set(count, "PCM_signed");
        break;
      case audio_fidl::wire::SampleFormat::kPcmUnsigned:
        supported_sample_formats_.Set(count, "PCM_unsigned");
        break;
      case audio_fidl::wire::SampleFormat::kPcmFloat:
        supported_sample_formats_.Set(count, "PCM_float");
        break;
    }
    count++;
  }
}

UsbAudioStream::~UsbAudioStream() {
  // We destructing.  All of our requests should be sitting in the free list.
  ZX_DEBUG_ASSERT(allocated_req_cnt_ == free_req_cnt_);

  while (!list_is_empty(&free_req_)) {
    usb_request_release(usb_req_list_remove_head(&free_req_, parent_.parent_req_size()));
  }
}

fbl::RefPtr<UsbAudioStream> UsbAudioStream::Create(UsbAudioDevice* parent,
                                                   std::unique_ptr<UsbAudioStreamInterface> ifc) {
  ZX_DEBUG_ASSERT(parent != nullptr);
  ZX_DEBUG_ASSERT(ifc != nullptr);

  fbl::AllocChecker ac;
  auto stream = fbl::AdoptRef(new (&ac) UsbAudioStream(parent, std::move(ifc)));
  if (!ac.check()) {
    LOG_EX(ERROR, *parent, "Out of memory while attempting to allocate UsbAudioStream");
    return nullptr;
  }

  stream->ComputePersistentUniqueId();

  return stream;
}

zx_status_t UsbAudioStream::Bind() {
  // TODO(johngro): Do this differently when we have the ability to queue io
  // transactions to a USB isochronous endpoint and can have the bus driver
  // DMA directly from the ring buffer we have set up with our user.
  {
    fbl::AutoLock req_lock(&req_lock_);

    list_initialize(&free_req_);
    free_req_cnt_ = 0;
    allocated_req_cnt_ = 0;

    uint64_t req_size = parent_.parent_req_size() + sizeof(usb_req_internal_t);
    for (uint32_t i = 0; i < MAX_OUTSTANDING_REQ; ++i) {
      usb_request_t* req;
      zx_status_t status = usb_request_alloc(&req, ifc_->max_req_size(), ifc_->ep_addr(), req_size);
      if (status != ZX_OK) {
        LOG(ERROR, "Failed to allocate usb request %u/%u (size %u): %d", i + 1, MAX_OUTSTANDING_REQ,
            ifc_->max_req_size(), status);
        return status;
      }

      status = usb_req_list_add_head(&free_req_, req, parent_.parent_req_size());
      ZX_DEBUG_ASSERT(status == ZX_OK);
      ++free_req_cnt_;
      ++allocated_req_cnt_;
    }
  }

  char name[64];
  snprintf(name, sizeof(name), "usb-audio-%s-%03d", is_input() ? "input" : "output",
           ifc_->term_link());

  zx_status_t status =
      UsbAudioStreamBase::DdkAdd(ddk::DeviceAddArgs(name).set_inspect_vmo(inspect_.DuplicateVmo()));
  if (status == ZX_OK) {
    // If bind/setup has succeeded, then the devmgr now holds a reference to us.
    // Manually increase our reference count to account for this.
    this->AddRef();
  } else {
    LOG(ERROR, "Failed to publish UsbAudioStream device node (name \"%s\", status %d)", name,
        status);
  }

  if (status != ZX_OK) {
    LOG(ERROR, "Failed to retrieve profile, status %d", status);
    return status;
  }

  return status;
}

void UsbAudioStream::RequestCompleteCallback(void* ctx, usb_request_t* request) {
  ZX_DEBUG_ASSERT(ctx != nullptr);
  reinterpret_cast<UsbAudioStream*>(ctx)->RequestComplete(request);
}

void UsbAudioStream::ComputePersistentUniqueId() {
  // Do the best that we can to generate a persistent ID unique to this audio
  // stream by blending information from a number of sources.  In particular,
  // consume...
  //
  // 1) This USB device's top level device descriptor (this contains the
  //    VID/PID of the device, among other things)
  // 2) The contents of the descriptor list used to describe the control and
  //    streaming interfaces present in the device.
  // 3) The manufacturer, product, and serial number string descriptors (if
  //    present)
  // 4) The stream interface ID.
  //
  // The goal here is to produce something like a UUID which is as unique to a
  // specific instance of a specific device as we can make it, but which
  // should persist across boots even in the presence of driver updates an
  // such.  Even so, upper levels of code will still need to deal with the sad
  // reality that some types of devices may end up looking the same between
  // two different instances.  If/when this becomes an issue, we may need to
  // pursue other options.  One choice might be to change the way devices are
  // enumerated in the USB section of the device tree so that their path has
  // only to do with physical topology, and has no runtime enumeration order
  // dependencies.  At that point in time, adding the topology into the hash
  // should do the job, but would imply that the same device plugged into two
  // different ports will have a different unique ID for the purposes of
  // saving and restoring driver settings (as it does in some operating
  // systems today).
  //
  uint16_t vid = parent_.desc().id_vendor;
  uint16_t pid = parent_.desc().id_product;
  audio_stream_unique_id_t fallback_id{
      .data = {'U', 'S', 'B', ' ', static_cast<uint8_t>(vid >> 8), static_cast<uint8_t>(vid),
               static_cast<uint8_t>(pid >> 8), static_cast<uint8_t>(pid), ifc_->iid()}};
  persistent_unique_id_ = fallback_id;

  digest::Digest sha;
  sha.Init();

  // #1: Top level descriptor.
  sha.Update(&parent_.desc(), sizeof(parent_.desc()));

  // #2: The descriptor list
  const auto& desc_list = parent_.desc_list();
  ZX_DEBUG_ASSERT((desc_list != nullptr) && (desc_list->size() > 0));
  sha.Update(desc_list->data(), desc_list->size());

  // #3: The various descriptor strings which may exist.
  const fbl::Array<uint8_t>* desc_strings[] = {&parent_.mfr_name(), &parent_.prod_name(),
                                               &parent_.serial_num()};
  for (const auto str : desc_strings) {
    if (str->size()) {
      sha.Update(str->data(), str->size());
    }
  }

  // #4: The stream interface's ID.
  auto iid = ifc_->iid();
  sha.Update(&iid, sizeof(iid));

  // Finish the SHA and attempt to copy as much of the results to our internal
  // cached representation as we can.
  sha.Final();
  sha.CopyTruncatedTo(persistent_unique_id_.data, sizeof(persistent_unique_id_.data));
}

void UsbAudioStream::ReleaseRingBufferLocked() {
  if (ring_buffer_virt_ != nullptr) {
    ZX_DEBUG_ASSERT(ring_buffer_size_ != 0);
    zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(ring_buffer_virt_), ring_buffer_size_);
    ring_buffer_virt_ = nullptr;
    ring_buffer_size_ = 0;
  }
  ring_buffer_vmo_.reset();
}

void UsbAudioStream::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (shutting_down_) {
    return completer.Close(ZX_ERR_BAD_STATE);
  }

  // Attempt to allocate a new driver channel and bind it to us.  If we don't
  // already have an stream_channel_, flag this channel is the privileged
  // connection (The connection which is allowed to do things like change
  // formats).
  bool privileged = (stream_channel_ == nullptr);

  auto stream_channel = StreamChannel::Create<StreamChannel>(this);
  if (stream_channel == nullptr) {
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  stream_channels_.push_back(stream_channel);
  number_of_stream_channels_.Add(1);
  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::StreamConfig>> on_unbound =
      [this, stream_channel](fidl::WireServer<audio_fidl::StreamConfig>*, fidl::UnbindInfo,
                             fidl::ServerEnd<fuchsia_hardware_audio::StreamConfig>) {
        fbl::AutoLock channel_lock(&lock_);
        this->DeactivateStreamChannelLocked(stream_channel.get());
      };

  stream_channel->BindServer(fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfig>>(
      loop_.dispatcher(), std::move(request->protocol), stream_channel.get(),
      std::move(on_unbound)));

  if (privileged) {
    ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
    stream_channel_ = stream_channel;
  }
}

void UsbAudioStream::DdkUnbind(ddk::UnbindTxn txn) {
  {
    fbl::AutoLock lock(&lock_);
    shutting_down_ = true;
    rb_vmo_fetched_ = false;
  }
  // We stop the loop so we can safely deactivate channels via RAII via DdkRelease.
  loop_.Shutdown();

  // Unpublish our device node.
  txn.Reply();
}

void UsbAudioStream::DdkRelease() {
  // Reclaim our reference from the driver framework and let it go out of
  // scope.  If this is our last reference (it should be), we will destruct
  // immediately afterwards.
  auto stream = fbl::ImportFromRawPtr(this);

  // Make sure that our parent is no longer holding a reference to us.
  parent_.RemoveAudioStream(stream);
}

void UsbAudioStream::GetSupportedFormats(
    StreamChannel::GetSupportedFormatsCompleter::Sync& completer) {
  const fbl::Vector<UsbAudioStreamInterface::FormatMapEntry>& formats = ifc_->formats();
  if (formats.size() > std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR, "Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!",
        formats.size());
    return;
  }

  // Build formats compatible with FIDL from a vector of audio_stream_format_range_t.
  // Needs to be alive until the reply is sent.
  struct FidlCompatibleFormats {
    fbl::Vector<uint8_t> number_of_channels;
    fbl::Vector<audio_fidl::wire::SampleFormat> sample_formats;
    fbl::Vector<uint32_t> frame_rates;
    fbl::Vector<uint8_t> valid_bits_per_sample;
    fbl::Vector<uint8_t> bytes_per_sample;
  };
  fbl::Vector<FidlCompatibleFormats> fidl_compatible_formats;
  for (UsbAudioStreamInterface::FormatMapEntry& i : formats) {
    std::vector<utils::Format> formats = audio::utils::GetAllFormats(i.range_.sample_formats);
    ZX_ASSERT(formats.size() >= 1);
    for (utils::Format& j : formats) {
      fbl::Vector<uint32_t> rates;
      // Ignore flags if min and max are equal.
      if (i.range_.min_frames_per_second == i.range_.max_frames_per_second) {
        rates.push_back(i.range_.min_frames_per_second);
      } else {
        ZX_DEBUG_ASSERT(!(i.range_.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS));
        audio::utils::FrameRateEnumerator enumerator(i.range_);
        for (uint32_t rate : enumerator) {
          rates.push_back(rate);
        }
      }

      fbl::Vector<uint8_t> number_of_channels;
      for (uint8_t j = i.range_.min_channels; j <= i.range_.max_channels; ++j) {
        number_of_channels.push_back(j);
      }

      fidl_compatible_formats.push_back({
          .number_of_channels = std::move(number_of_channels),
          .sample_formats = {j.format},
          .frame_rates = std::move(rates),
          .valid_bits_per_sample = {j.valid_bits_per_sample},
          .bytes_per_sample = {j.bytes_per_sample},
      });
    }
  }

  fidl::Arena allocator;
  fidl::VectorView<audio_fidl::wire::SupportedFormats> fidl_formats(allocator,
                                                                    fidl_compatible_formats.size());
  // Build formats compatible with FIDL for all the formats.
  // Needs to be alive until the reply is sent.
  for (size_t i = 0; i < fidl_compatible_formats.size(); ++i) {
    FidlCompatibleFormats& src = fidl_compatible_formats[i];
    audio_fidl::wire::SupportedFormats& dst = fidl_formats[i];

    audio_fidl::wire::PcmSupportedFormats formats;
    formats.Allocate(allocator);
    fidl::VectorView<audio_fidl::wire::ChannelSet> channel_sets(allocator,
                                                                src.number_of_channels.size());

    for (uint8_t j = 0; j < src.number_of_channels.size(); ++j) {
      fidl::VectorView<audio_fidl::wire::ChannelAttributes> attributes(allocator,
                                                                       src.number_of_channels[j]);
      channel_sets[j].Allocate(allocator);
      channel_sets[j].set_attributes(allocator, std::move(attributes));
    }
    formats.set_channel_sets(allocator, std::move(channel_sets));
    formats.set_sample_formats(allocator,
                               ::fidl::VectorView<audio_fidl::wire::SampleFormat>::FromExternal(
                                   src.sample_formats.data(), src.sample_formats.size()));
    formats.set_frame_rates(allocator, ::fidl::VectorView<uint32_t>::FromExternal(
                                           src.frame_rates.data(), src.frame_rates.size()));
    formats.set_bytes_per_sample(
        allocator, ::fidl::VectorView<uint8_t>::FromExternal(src.bytes_per_sample.data(),
                                                             src.bytes_per_sample.size()));
    formats.set_valid_bits_per_sample(
        allocator, ::fidl::VectorView<uint8_t>::FromExternal(src.valid_bits_per_sample.data(),
                                                             src.valid_bits_per_sample.size()));

    dst.Allocate(allocator);
    dst.set_pcm_supported_formats(allocator, std::move(formats));
  }

  completer.Reply(std::move(fidl_formats));
}

void UsbAudioStream::CreateRingBuffer(StreamChannel* channel, audio_fidl::wire::Format format,
                                      ::fidl::ServerEnd<audio_fidl::RingBuffer> ring_buffer,
                                      StreamChannel::CreateRingBufferCompleter::Sync& completer) {
  // Only the privileged stream channel is allowed to change the format.
  {
    fbl::AutoLock channel_lock(&lock_);
    if (channel != stream_channel_.get()) {
      LOG(ERROR, "Unprivileged channel cannot set the format");
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  auto req = format.pcm_format();

  audio_sample_format_t sample_format =
      audio::utils::GetSampleFormat(req.valid_bits_per_sample, 8 * req.bytes_per_sample);

  if (sample_format == 0) {
    LOG(ERROR, "Unsupported format: Invalid bits per sample (%u/%u)", req.valid_bits_per_sample,
        8 * req.bytes_per_sample);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (req.sample_format == audio_fidl::wire::SampleFormat::kPcmFloat) {
    sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
    if (req.valid_bits_per_sample != 32 || req.bytes_per_sample != 4) {
      LOG(ERROR, "Unsupported format: Not 32 per sample/channel for float");
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  if (req.sample_format == audio_fidl::wire::SampleFormat::kPcmUnsigned) {
    sample_format |= AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  }

  // Look up the details about the interface and the endpoint which will be
  // used for the requested format.
  size_t format_ndx;
  zx_status_t status =
      ifc_->LookupFormat(req.frame_rate, req.number_of_channels, sample_format, &format_ndx);
  if (status != ZX_OK) {
    LOG(ERROR, "Could not find a suitable format");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Determine the frame size needed for this requested format, then compute
  // the size of our short packets, and the constants used to generate the
  // short/long packet cadence.  For now, assume that we will be operating at
  // a 1mSec isochronous rate.
  //
  // Make sure that we can fit our longest payload length into one of our
  // usb requests.
  //
  // Store the results of all of these calculations in local variables.  Do
  // not commit them to member variables until we are certain that we are
  // going to go ahead with this format change.
  //
  // TODO(johngro) : Unless/until we can find some way to set the USB bus
  // driver to perform direct DMA to/from the Ring Buffer VMO without the need
  // for software intervention, we may want to expose ways to either increase
  // the isochronous interval (to minimize load) or to use USB 2.0 125uSec
  // sub-frame timing (to decrease latency) if possible.
  uint32_t frame_size;
  frame_size =
      audio::utils::ComputeFrameSize(static_cast<uint16_t>(req.number_of_channels), sample_format);
  if (!frame_size) {
    LOG(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)", req.number_of_channels,
        sample_format);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  static constexpr uint32_t iso_packet_rate = 1000;
  uint32_t bytes_per_packet, fractional_bpp_inc, long_payload_len;
  bytes_per_packet = (req.frame_rate / iso_packet_rate) * frame_size;
  fractional_bpp_inc = (req.frame_rate % iso_packet_rate);
  long_payload_len = bytes_per_packet + (fractional_bpp_inc ? frame_size : 0);

  ZX_DEBUG_ASSERT(format_ndx < ifc_->formats().size());
  if (long_payload_len > ifc_->formats()[format_ndx].max_req_size_) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Deny the format change request if the ring buffer is not currently stopped.
  {
    // TODO(johngro) : If the ring buffer is running, should we automatically
    // stop it instead of returning bad state?
    fbl::AutoLock req_lock(&req_lock_);
    if (ring_buffer_state_ != RingBufferState::STOPPED) {
      completer.Close(ZX_ERR_BAD_STATE);
      return;
    }
  }

  fbl::AutoLock req_lock(&lock_);
  if (shutting_down_) {
    return completer.Close(ZX_ERR_BAD_STATE);
  }

  // Looks like we are going ahead with this format change.  Tear down any
  // exiting ring buffer interface before proceeding.
  if (rb_channel_ != nullptr) {
    rb_channel_->UnbindServer();
  }

  // Record the details of our cadence and format selection
  selected_format_ndx_ = format_ndx;
  selected_frame_rate_ = req.frame_rate;
  frame_size_ = frame_size;
  iso_packet_rate_ = iso_packet_rate;
  bytes_per_packet_ = bytes_per_packet;
  fractional_bpp_inc_ = fractional_bpp_inc;

  // Compute the effective fifo depth for this stream.  Right now, we are in a
  // situation where, for an output, we need to memcpy payloads from the mixer
  // ring buffer into the jobs that we send to the USB host controller.  For an
  // input, when the jobs complete, we need to copy the data from the completed
  // job into the ring buffer.
  //
  // This gives us two different "fifo" depths we may need to report.  For an
  // input, if job X just completed, we will be copying the data sometime during
  // job X+1, assuming that we are hitting our callback targets.  Because of
  // this, we should be safe to report our fifo depth as being 2 times the size
  // of a single maximum sized job.
  //
  // For output, we are attempting to stay MAX_OUTSTANDING_REQ ahead, and we are
  // copying the data from the mixer ring buffer as we go.  Because of this, our
  // reported fifo depth is going to be MAX_OUTSTANDING_REQ maximum sized jobs
  // ahead of the nominal read pointer.
  fifo_bytes_ = bytes_per_packet_ * (is_input() ? 2 : MAX_OUTSTANDING_REQ);

  // If we have no fractional portion to accumulate, we always send
  // short packets.  If our fractional portion is <= 1/2 of our
  // isochronous rate, then we will never send two long packets back
  // to back.
  if (fractional_bpp_inc_) {
    fifo_bytes_ += frame_size_;
    if (fractional_bpp_inc_ > (iso_packet_rate_ >> 1)) {
      fifo_bytes_ += frame_size_;
    }
  }
  if (req.frame_rate == 0) {
    LOG(ERROR, "Bad (zero) frame rate");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (frame_size == 0) {
    LOG(ERROR, "Bad (zero) frame size");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint32_t fifo_depth_frames = (fifo_bytes_ + frame_size - 1) / frame_size;
  internal_delay_nsec_ = static_cast<uint64_t>(fifo_depth_frames) * 1'000'000'000 /
                         static_cast<uint64_t>(req.frame_rate);

  // Create a new ring buffer channel which can be used to move bulk data and
  // bind it to us.
  rb_channel_ = Channel::Create<RingBufferChannel>();

  number_of_channels_.Set(req.number_of_channels);
  frame_rate_.Set(req.frame_rate);
  bits_per_slot_.Set(req.bytes_per_sample * 8);
  bits_per_sample_.Set(req.valid_bits_per_sample);
  // clang-format off
  switch (req.sample_format) {
    case audio_fidl::wire::SampleFormat::kPcmSigned:   sample_format_.Set("PCM_signed");   break;
    case audio_fidl::wire::SampleFormat::kPcmUnsigned: sample_format_.Set("PCM_unsigned"); break;
    case audio_fidl::wire::SampleFormat::kPcmFloat:    sample_format_.Set("PCM_float");    break;
  }
  // clang-format on

  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::RingBuffer>> on_unbound =
      [this](fidl::WireServer<audio_fidl::RingBuffer>*, fidl::UnbindInfo,
             fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer>) {
        fbl::AutoLock lock(&lock_);
        this->DeactivateRingBufferChannelLocked(rb_channel_.get());
      };

  rb_channel_->BindServer(fidl::BindServer<fidl::WireServer<audio_fidl::RingBuffer>>(
      loop_.dispatcher(), std::move(ring_buffer), this, std::move(on_unbound)));
}

void UsbAudioStream::WatchGainState(StreamChannel* channel,
                                    StreamChannel::WatchGainStateCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(!channel->gain_completer_);
  channel->gain_completer_ = completer.ToAsync();

  ZX_DEBUG_ASSERT(ifc_->path() != nullptr);
  const auto& path = *(ifc_->path());

  audio_proto::GainState cur_gain_state = {};
  cur_gain_state.cur_mute = path.cur_mute();
  cur_gain_state.cur_agc = path.cur_agc();
  cur_gain_state.cur_gain = path.cur_gain();
  cur_gain_state.can_mute = path.has_mute();
  cur_gain_state.can_agc = path.has_agc();
  cur_gain_state.min_gain = path.min_gain();
  cur_gain_state.max_gain = path.max_gain();
  cur_gain_state.gain_step = path.gain_res();
  // Reply is delayed if there is no change since the last reported gain state.
  if (channel->last_reported_gain_state_ != cur_gain_state) {
    fidl::Arena allocator;
    audio_fidl::wire::GainState gain_state(allocator);
    if (cur_gain_state.can_mute) {
      gain_state.set_muted(cur_gain_state.cur_mute);
    }
    if (cur_gain_state.can_agc) {
      gain_state.set_agc_enabled(cur_gain_state.cur_agc);
    }
    gain_state.set_gain_db(cur_gain_state.cur_gain);
    channel->last_reported_gain_state_ = cur_gain_state;
    channel->gain_completer_->Reply(std::move(gain_state));
    channel->gain_completer_.reset();
  }
}

void UsbAudioStream::WatchClockRecoveryPositionInfo(
    WatchClockRecoveryPositionInfoCompleter::Sync& completer) {
  fbl::AutoLock req_lock(&req_lock_);
  position_completer_ = completer.ToAsync();
  position_request_time_.Set(zx::clock::get_monotonic().get());
}

void UsbAudioStream::WatchDelayInfo(WatchDelayInfoCompleter::Sync& completer) {
  if (!delay_info_updated_) {
    delay_info_updated_ = true;
    fidl::Arena allocator;
    auto delay_info = audio_fidl::wire::DelayInfo::Builder(allocator);
    // No external delay information is provided by this driver.
    delay_info.internal_delay(internal_delay_nsec_);
    completer.Reply(delay_info.Build());
  }
}

void UsbAudioStream::SetGain(audio_fidl::wire::GainState state,
                             StreamChannel::SetGainCompleter::Sync& completer) {
  // TODO(johngro): Actually perform the set operation on our audio path.
  ZX_DEBUG_ASSERT(ifc_->path() != nullptr);
  auto& path = *(ifc_->path());
  bool illegal_mute = state.has_muted() && state.muted() && !path.has_mute();
  bool illegal_agc = state.has_agc_enabled() && state.agc_enabled() && !path.has_agc();
  bool illegal_gain = state.has_gain_db() && (state.gain_db() != 0) && !path.has_gain();

  if (illegal_mute || illegal_agc || illegal_gain) {
    // If this request is illegal, make no changes.
  } else {
    if (state.has_muted()) {
      state.muted() = path.SetMute(parent_.usb_proto(), state.muted());
    }

    if (state.has_agc_enabled()) {
      state.agc_enabled() = path.SetAgc(parent_.usb_proto(), state.agc_enabled());
    }

    if (state.has_gain_db()) {
      state.gain_db() = path.SetGain(parent_.usb_proto(), state.gain_db());
    }

    fbl::AutoLock channel_lock(&lock_);
    for (auto& channel : stream_channels_) {
      if (channel.gain_completer_) {
        channel.gain_completer_->Reply(std::move(state));
        channel.gain_completer_.reset();
      }
    }
  }
}

void UsbAudioStream::WatchPlugState(StreamChannel* channel,
                                    StreamChannel::WatchPlugStateCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(!channel->plug_completer_);
  channel->plug_completer_ = completer.ToAsync();

  // As long as the usb device is present, we are plugged. A second reply is delayed indefinitely
  // since there will be no change from the last reported plugged state.
  if (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kNotReported ||
      (channel->last_reported_plugged_state_ != StreamChannel::Plugged::kPlugged)) {
    fidl::Arena allocator;
    audio_fidl::wire::PlugState plug_state(allocator);
    plug_state.set_plugged(true).set_plug_state_time(allocator, create_time_);
    channel->last_reported_plugged_state_ = StreamChannel::Plugged::kPlugged;
    channel->plug_completer_->Reply(std::move(plug_state));
    channel->plug_completer_.reset();
  }
}

void UsbAudioStream::GetProperties(StreamChannel::GetPropertiesCompleter::Sync& completer) {
  fidl::Arena allocator;
  audio_fidl::wire::StreamProperties stream_properties(allocator);
  stream_properties.set_unique_id(allocator);
  for (size_t i = 0; i < audio_fidl::wire::kUniqueIdSize; ++i) {
    stream_properties.unique_id().data_[i] = persistent_unique_id_.data[i];
  }

  const auto& path = *(ifc_->path());

  auto product = fidl::StringView::FromExternal(
      reinterpret_cast<const char*>(parent_.prod_name().begin()), parent_.prod_name().size());
  auto manufacturer = fidl::StringView::FromExternal(
      reinterpret_cast<const char*>(parent_.mfr_name().begin()), parent_.mfr_name().size());

  stream_properties.set_is_input(is_input())
      .set_can_mute(path.has_mute())
      .set_can_agc(path.has_agc())
      .set_min_gain_db(path.min_gain())
      .set_max_gain_db(path.max_gain())
      .set_gain_step_db(path.gain_res())
      .set_product(allocator, std::move(product))
      .set_manufacturer(allocator, std::move(manufacturer))
      .set_clock_domain(clock_domain_)
      .set_plug_detect_capabilities(audio_fidl::wire::PlugDetectCapabilities::kHardwired);

  completer.Reply(std::move(stream_properties));
}

void UsbAudioStream::GetProperties(GetPropertiesCompleter::Sync& completer) {
  fidl::Arena allocator;
  audio_fidl::wire::RingBufferProperties ring_buffer_properties(allocator);
  ring_buffer_properties.set_fifo_depth(fifo_bytes_);
  // TODO(johngro): Report the actual external delay.
  ring_buffer_properties.set_external_delay(allocator, 0);
  ring_buffer_properties.set_needs_cache_flush_or_invalidate(true);
  completer.Reply(std::move(ring_buffer_properties));
}

void UsbAudioStream::GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) {
  zx::vmo client_rb_handle;
  uint32_t map_flags, client_rights;
  frames_requested_.Set(request->min_frames);

  {
    // We cannot create a new ring buffer if we are not currently stopped.
    fbl::AutoLock req_lock(&req_lock_);
    if (ring_buffer_state_ != RingBufferState::STOPPED) {
      LOG(ERROR, "Tried to get VMO in non-stopped state");
      return;
    }
  }

  // Unmap and release any previous ring buffer.
  {
    fbl::AutoLock req_lock(&lock_);
    ReleaseRingBufferLocked();
  }

  auto cleanup = fit::defer([&completer, this]() {
    {
      fbl::AutoLock req_lock(&this->lock_);
      this->ReleaseRingBufferLocked();
    }
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
  });

  // Compute the ring buffer size.  It needs to be at least as big
  // as the virtual fifo depth.
  ZX_DEBUG_ASSERT(frame_size_ && ((fifo_bytes_ % frame_size_) == 0));
  ZX_DEBUG_ASSERT(fifo_bytes_ && ((fifo_bytes_ % fifo_bytes_) == 0));
  ring_buffer_size_ = request->min_frames;
  ring_buffer_size_ *= frame_size_;
  if (ring_buffer_size_ < fifo_bytes_)
    ring_buffer_size_ = fbl::round_up(fifo_bytes_, frame_size_);

  // Set up our state for generating notifications.
  if (request->clock_recovery_notifications_per_ring) {
    bytes_per_notification_ = ring_buffer_size_ / request->clock_recovery_notifications_per_ring;
  } else {
    bytes_per_notification_ = 0;
  }

  // Create the ring buffer vmo we will use to share memory with the client.
  zx_status_t status = zx::vmo::create(ring_buffer_size_, 0, &ring_buffer_vmo_);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to create ring buffer (size %u, res %d)", ring_buffer_size_, status);
    return;
  }

  // Map the VMO into our address space.
  //
  // TODO(johngro): skip this step when APIs in the USB bus driver exist to
  // DMA directly from the VMO.
  map_flags = ZX_VM_PERM_READ;
  if (is_input())
    map_flags |= ZX_VM_PERM_WRITE;

  status = zx::vmar::root_self()->map(map_flags, 0, ring_buffer_vmo_, 0, ring_buffer_size_,
                                      reinterpret_cast<uintptr_t*>(&ring_buffer_virt_));
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to map ring buffer (size %u, res %d)", ring_buffer_size_, status);
    return;
  }

  // Create the client's handle to the ring buffer vmo and set it back to them.
  client_rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP | ZX_RIGHT_READ;
  if (!is_input())
    client_rights |= ZX_RIGHT_WRITE;

  status = ring_buffer_vmo_.duplicate(client_rights, &client_rb_handle);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to duplicate ring buffer handle (res %d)", status);
    return;
  }

  uint32_t num_ring_buffer_frames = ring_buffer_size_ / frame_size_;

  cleanup.cancel();
  {
    fbl::AutoLock lock(&lock_);
    rb_vmo_fetched_ = true;
  }
  ring_buffer_size2_.Set(ring_buffer_size_);
  completer.ReplySuccess(num_ring_buffer_frames, std::move(client_rb_handle));
}

void UsbAudioStream::Start(StartCompleter::Sync& completer) {
  fbl::AutoLock req_lock(&req_lock_);

  {
    fbl::AutoLock lock(&lock_);
    if (!rb_vmo_fetched_) {
      zxlogf(ERROR, "Did not start, VMO not fetched");
      completer.Close(ZX_ERR_BAD_STATE);
      return;
    }
  }

  if (ring_buffer_state_ != RingBufferState::STOPPED) {
    // The ring buffer is running, do not linger in the lock while we send
    // the error code back to the user.
    LOG(ERROR, "Attempt to start an already started ring buffer");
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  // We are idle, all of our usb requests should be sitting in the free list.
  ZX_DEBUG_ASSERT(allocated_req_cnt_ == free_req_cnt_);

  // Activate the format.
  zx_status_t status = ifc_->ActivateFormat(selected_format_ndx_, selected_frame_rate_);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to activate format %d", status);
    completer.Reply(zx::clock::get_monotonic().get());
    return;
  }

  // Initialize the counters used to...
  // 1) generate the short/long packet cadence.
  // 2) generate notifications.
  // 3) track the position in the ring buffer.
  fractional_bpp_acc_ = 0;
  notification_acc_ = 0;
  ring_buffer_offset_ = 0;
  ring_buffer_pos_ = 0;

  // Schedule the frame number which the first transaction will go out on.
  usb_frame_num_ = usb_get_current_frame(&parent_.usb_proto());

  // Flag ourselves as being in the starting state, then queue up all of our
  // transactions.
  ring_buffer_state_ = RingBufferState::STARTING;
  state_.Set("starting");
  while (!list_is_empty(&free_req_))
    QueueRequestLocked();

  start_completer_.emplace(completer.ToAsync());
}

void UsbAudioStream::Stop(StopCompleter::Sync& completer) {
  fbl::AutoLock req_lock(&req_lock_);

  {
    fbl::AutoLock lock(&lock_);
    if (!rb_vmo_fetched_) {
      zxlogf(ERROR, "Did not stop, VMO not fetched");
      completer.Close(ZX_ERR_BAD_STATE);
      return;
    }
  }

  // TODO(johngro): Fix this to use the cancel transaction capabilities added
  // to the USB bus driver.
  //
  // Also, investigate whether or not the cancel interface is synchronous or
  // whether we will need to maintain an intermediate stopping state.
  if (ring_buffer_state_ != RingBufferState::STARTED) {
    LOG(INFO, "Attempt to stop a not started ring buffer");
    completer.Reply();
    return;
  }

  ring_buffer_state_ = RingBufferState::STOPPING;
  state_.Set("stopping_requested");
  stop_completer_.emplace(completer.ToAsync());
}

void UsbAudioStream::RequestComplete(usb_request_t* req) {
  enum class Action {
    NONE,
    SIGNAL_STARTED,
    SIGNAL_STOPPED,
    NOTIFY_POSITION,
    HANDLE_UNPLUG,
  };

  audio_fidl::wire::RingBufferPositionInfo position_info = {};

  usb_requests_outstanding_.Subtract(1);

  uint64_t complete_time = zx::clock::get_monotonic().get();
  Action when_finished = Action::NONE;

  // TODO(johngro) : See fxbug.dev/30888.  Eliminate this as soon as we have a more
  // official way of meeting real-time latency requirements.  Also, the fact
  // that this boosting gets done after the first transaction completes
  // degrades the quality of the startup time estimate (if the system is under
  // high load when the system starts up).  As a general issue, there are
  // better ways of refining this estimate than bumping the thread prio before
  // the first transaction gets queued.  Therefor, we just have a poor
  // estimate for now and will need to live with the consequences.
  if (!req_complete_prio_bumped_) {
    const char* role_name = "fuchsia.devices.usb.audio";
    const size_t role_name_size = strlen(role_name);
    const zx_status_t status =
        device_set_profile_by_role(this->zxdev(), zx_thread_self(), role_name, role_name_size);
    if (status != ZX_OK) {
      zxlogf(WARNING,
             "Failed to apply role \"%s\" to the USB audio callback thread.  Service will be best "
             "effort.\n",
             role_name);
    }
    req_complete_prio_bumped_ = true;
  }

  {
    fbl::AutoLock req_lock(&req_lock_);

    // Cache the status and length of this usb request.
    zx_status_t req_status = req->response.status;
    uint32_t req_length = static_cast<uint32_t>(req->header.length);

    // Complete the usb request.  This will return the transaction to the free
    // list and (in the case of an input stream) copy the payload to the
    // ring buffer, and update the ring buffer position.
    //
    // TODO(johngro): copying the payload out of the ring buffer is an
    // operation which goes away when we get to the zero copy world.
    CompleteRequestLocked(req);

    // Did the transaction fail because the device was unplugged?  If so,
    // enter the stopping state and close the connections to our clients.
    if (req_status == ZX_ERR_IO_NOT_PRESENT) {
      ring_buffer_state_ = RingBufferState::STOPPING_AFTER_UNPLUG;
      state_.Set("stopping_after_unplug");
    } else {
      // If we are supposed to be delivering notifications, check to see
      // if it is time to do so.
      if (bytes_per_notification_) {
        notification_acc_ += req_length;

        if ((ring_buffer_state_ == RingBufferState::STARTED) &&
            (notification_acc_ >= bytes_per_notification_)) {
          when_finished = Action::NOTIFY_POSITION;
          notification_acc_ = (notification_acc_ % bytes_per_notification_);
          position_info.timestamp = zx::clock::get_monotonic().get();
          position_info.position = ring_buffer_pos_;
        }
      }
    }

    switch (ring_buffer_state_) {
      case RingBufferState::STOPPING:
        if (free_req_cnt_ == allocated_req_cnt_) {
          when_finished = Action::SIGNAL_STOPPED;
        }
        break;

      case RingBufferState::STOPPING_AFTER_UNPLUG:
        if (free_req_cnt_ == allocated_req_cnt_) {
          when_finished = Action::HANDLE_UNPLUG;
        }
        break;

      case RingBufferState::STARTING:
        when_finished = Action::SIGNAL_STARTED;
        [[fallthrough]];

      case RingBufferState::STARTED:
        QueueRequestLocked();
        break;

      case RingBufferState::STOPPED:
      default:
        LOG(ERROR, "Invalid state (%u)", static_cast<uint32_t>(ring_buffer_state_));
        ZX_DEBUG_ASSERT(false);
        break;
    }
  }

  if (when_finished != Action::NONE) {
    fbl::AutoLock lock(&lock_);
    switch (when_finished) {
      case Action::SIGNAL_STARTED:
        if (rb_channel_ != nullptr) {
          // TODO(johngro) : this start time estimate is not as good as it
          // could be.  We really need to have the USB bus driver report
          // the relationship between the USB frame counter and the system
          // tick counter (and track the relationship in the case that the
          // USB oscillator is not derived from the system oscillator).
          // Then we can accurately report the start time as the time of
          // the tick on which we scheduled the first transaction.
          fbl::AutoLock req_lock(&req_lock_);
          start_completer_->Reply(zx_time_sub_duration(complete_time, ZX_MSEC(1)));
        }
        {
          fbl::AutoLock req_lock(&req_lock_);
          ring_buffer_state_ = RingBufferState::STARTED;
          state_.Set("started");
          start_time_.Set(zx::clock::get_monotonic().get());
        }
        break;

      case Action::HANDLE_UNPLUG:
        if (rb_channel_ != nullptr) {
          rb_channel_->UnbindServer();
        }

        if (stream_channel_ != nullptr) {
          stream_channel_->UnbindServer();
        }

        {
          fbl::AutoLock req_lock(&req_lock_);
          ring_buffer_state_ = RingBufferState::STOPPED;
          state_.Set("stopped_handle_unplug");
        }
        break;

      case Action::SIGNAL_STOPPED:
        if (rb_channel_ != nullptr) {
          fbl::AutoLock req_lock(&req_lock_);
          stop_completer_->Reply();
        }
        {
          fbl::AutoLock req_lock(&req_lock_);
          ring_buffer_state_ = RingBufferState::STOPPED;
          state_.Set("stopped_after_signal");
          ifc_->ActivateIdleFormat();
        }
        break;

      case Action::NOTIFY_POSITION: {
        fbl::AutoLock req_lock(&req_lock_);
        if (position_completer_) {
          position_completer_->Reply(position_info);
          position_completer_.reset();
          position_reply_time_.Set(zx::clock::get_monotonic().get());
        }
      } break;

      default:
        ZX_DEBUG_ASSERT(false);
        break;
    }
  }
}

void UsbAudioStream::QueueRequestLocked() {
  ZX_DEBUG_ASSERT((ring_buffer_state_ == RingBufferState::STARTING) ||
                  (ring_buffer_state_ == RingBufferState::STARTED));
  ZX_DEBUG_ASSERT(!list_is_empty(&free_req_));

  // Figure out how much we want to send or receive this time (short or long
  // packet)
  uint32_t todo = bytes_per_packet_;
  fractional_bpp_acc_ += fractional_bpp_inc_;
  if (fractional_bpp_acc_ >= iso_packet_rate_) {
    fractional_bpp_acc_ -= iso_packet_rate_;
    todo += frame_size_;
    ZX_DEBUG_ASSERT(fractional_bpp_acc_ < iso_packet_rate_);
  }

  // Grab a free usb request.
  auto req = usb_req_list_remove_head(&free_req_, parent_.parent_req_size());
  ZX_DEBUG_ASSERT(req != nullptr);
  ZX_DEBUG_ASSERT(free_req_cnt_ > 0);
  --free_req_cnt_;

  // If this is an output stream, copy our data into the usb request.
  // TODO(johngro): eliminate this when we can get to a zero-copy world.
  if (!is_input()) {
    uint32_t avail = ring_buffer_size_ - ring_buffer_offset_;
    ZX_DEBUG_ASSERT(ring_buffer_offset_ < ring_buffer_size_);
    ZX_DEBUG_ASSERT((avail % frame_size_) == 0);
    uint32_t amt = std::min(avail, todo);

    const uint8_t* src = reinterpret_cast<uint8_t*>(ring_buffer_virt_) + ring_buffer_offset_;
    // Not security-critical -- we're copying to a ring buffer that's moving based off of time
    // anyways. If we don't copy enough data we'll just keep playing the same sample in a loop.
    ssize_t copied = usb_request_copy_to(req, src, amt, 0);
    if (amt == avail) {
      ring_buffer_offset_ = todo - amt;
      if (ring_buffer_offset_ > 0) {
        copied = usb_request_copy_to(req, ring_buffer_virt_, ring_buffer_offset_, amt);
      }
    } else {
      ring_buffer_offset_ += amt;
    }
  }

  // Schedule this packet to be sent out on the next frame.
  req->header.frame = ++usb_frame_num_;
  req->header.length = todo;
  usb_request_complete_callback_t complete = {
      .callback = UsbAudioStream::RequestCompleteCallback,
      .ctx = this,
  };
  usb_requests_sent_.Add(1);
  usb_requests_outstanding_.Add(1);
  usb_request_queue(&parent_.usb_proto(), req, &complete);
}

void UsbAudioStream::CompleteRequestLocked(usb_request_t* req) {
  ZX_DEBUG_ASSERT(req);

  // If we are an input stream, copy the payload into the ring buffer.
  if (is_input()) {
    uint32_t todo = static_cast<uint32_t>(req->header.length);

    uint32_t avail = ring_buffer_size_ - ring_buffer_offset_;
    ZX_DEBUG_ASSERT(ring_buffer_offset_ < ring_buffer_size_);
    ZX_DEBUG_ASSERT((avail % frame_size_) == 0);

    uint32_t amt = std::min(avail, todo);
    uint8_t* dst = reinterpret_cast<uint8_t*>(ring_buffer_virt_) + ring_buffer_offset_;

    if (req->response.status == ZX_OK) {
      [[maybe_unused]] ssize_t size = usb_request_copy_from(req, dst, amt, 0);
      if (amt < todo) {
        [[maybe_unused]] ssize_t size =
            usb_request_copy_from(req, ring_buffer_virt_, todo - amt, amt);
      }
    } else {
      // TODO(johngro): filling with zeros is only the proper thing to do
      // for signed formats.  USB does support unsigned 8-bit audio; if
      // that is our format, we should fill with 0x80 instead in order to
      // fill with silence.
      memset(dst, 0, amt);
      if (amt < todo) {
        memset(ring_buffer_virt_, 0, todo - amt);
      }
    }
  }

  // Update the ring buffer position.
  ring_buffer_pos_ += static_cast<uint32_t>(req->header.length);
  if (ring_buffer_pos_ >= ring_buffer_size_) {
    ring_buffer_pos_ -= ring_buffer_size_;
    ZX_DEBUG_ASSERT(ring_buffer_pos_ < ring_buffer_size_);
  }

  // If this is an input stream, the ring buffer offset should always be equal
  // to the stream position.
  if (is_input()) {
    ring_buffer_offset_ = ring_buffer_pos_;
  }

  // Return the transaction to the free list.
  zx_status_t status = usb_req_list_add_head(&free_req_, req, parent_.parent_req_size());
  ZX_DEBUG_ASSERT(status == ZX_OK);
  ++free_req_cnt_;
  ZX_DEBUG_ASSERT(free_req_cnt_ <= allocated_req_cnt_);
}

void UsbAudioStream::DeactivateStreamChannelLocked(StreamChannel* channel) {
  if (stream_channel_.get() == channel) {
    stream_channel_ = nullptr;
  }
  stream_channels_.erase(*channel);
  number_of_stream_channels_.Subtract(1);
}

void UsbAudioStream::DeactivateRingBufferChannelLocked(const Channel* channel) {
  ZX_DEBUG_ASSERT(stream_channel_.get() != channel);
  ZX_DEBUG_ASSERT(rb_channel_.get() == channel);

  {
    fbl::AutoLock req_lock(&req_lock_);
    if (ring_buffer_state_ != RingBufferState::STOPPED) {
      ring_buffer_state_ = RingBufferState::STOPPING;
      state_.Set("stopping_deactivate");
    }
    rb_vmo_fetched_ = false;
    delay_info_updated_ = false;
  }

  rb_channel_.reset();
}

}  // namespace usb
}  // namespace audio
