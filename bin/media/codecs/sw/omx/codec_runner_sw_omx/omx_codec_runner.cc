// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "omx_codec_runner.h"

#include "so_entry_point.h"

#include <dlfcn.h>
#include "OMX_Core.h"

#include <fbl/auto_call.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <algorithm>

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

// The protocol does not permit an unbounded number of in-flight streams, as
// that would potentially result in unbounded data queued in the incoming
// channel with no valid circuit-breaker value for the incoming channel data.
constexpr size_t kMaxInFlightStreams = 10;
// Input constraints always have version ordinal 1 because version 0 isn't a
// valid ordinal (to simplify initial state handling) and there's only ever one
// version.
constexpr uint64_t kInputBufferConstraintsVersionOrdinal = 1;
// This is fairly arbitrary, but avoid recommending buffers that are pointlessly
// large.  This is subject to change.
constexpr uint32_t kOmxRecommendedBufferVsMinBufferFactor = 1;
// This is fairly arbitrary.  This is subject to change.  Note that this places
// a constraint on the max vs. min at Codec layer, not the max at OMX layer,
// because at the OMX layer the nBufferSize is virtualized fairly heavily in
// single-buffer mode, so the OMX layer max nBufferSize value can become much
// larger than this factor (vs the initial value of nBufferSize).
constexpr uint32_t kOmxMaxBufferVsMinBufferFactor = 5;

// This does not auto-add any buffers for client use or for performance, and we
// don't want to have every layer adding more buffer count for such reasons, so
// pass through the nBufferCountMin as the min and the recommended number.
constexpr uint32_t kOmxRecommendedBufferCountVsMinBufferCountFactor = 1;
// More than 3 times the min is probably pointless.  This is fairly arbitrary.
constexpr uint32_t kOmxRecommendedMaxBufferCountVsMinBufferCountFactor = 3;
// This is fairly arbitrary.
constexpr uint32_t kOmxMaxBufferCountVsMinBufferCountFactor = 5;

// These are packet_count based, so 0 means one beyond the last normal packet
// index, 1 means 2 beyond the last normal packet index.  OMX knows about these
// packets (as OMX buffers) but the Codec client does not.
constexpr uint32_t kHiddenInputPacketIndexOffsetOob = 0;
constexpr uint32_t kHiddenInputPacketIndexOffsetEos = 1;
constexpr uint32_t kHiddenInputPacketCount = 2;

// For input, we only send OnInputBufferSettings() once at the very beginning,
// so for now it makes sense (barely) to help the client select the client's
// buffer_lifetime_ordinal.
constexpr uint32_t kBestFirstBufferLifetimeOrdinal = 1;
// For output, don't try to help the client count from the wrong end of the
// channel.  At best this would be of marginal value to simple clients and at
// worst it would lead to an expectation that the server knows what
// buffer_lifetime_ordinal values the client has used so far which the server
// has no way of knowing at any given instant.
constexpr uint32_t kInvalidDefaultBufferLifetimeOrdinal = 0;

constexpr fuchsia::mediacodec::AudioChannelId
    kOmxAudioChannelTypeToAudioChannelId[] = {
        fuchsia::mediacodec::AudioChannelId::SKIP,  // OMX_AUDIO_ChannelNone
        fuchsia::mediacodec::AudioChannelId::LF,    // OMX_AUDIO_ChannelLF
        fuchsia::mediacodec::AudioChannelId::RF,    // OMX_AUDIO_ChannelRF
        fuchsia::mediacodec::AudioChannelId::CF,    // OMX_AUDIO_ChannelCF
        fuchsia::mediacodec::AudioChannelId::LS,    // OMX_AUDIO_ChannelLS
        fuchsia::mediacodec::AudioChannelId::RS,    // OMX_AUDIO_ChannelRS
        fuchsia::mediacodec::AudioChannelId::LFE,   // OMX_AUDIO_ChannelLFE
        fuchsia::mediacodec::AudioChannelId::CS,    // OMX_AUDIO_ChannelCS
        fuchsia::mediacodec::AudioChannelId::LR,    // OMX_AUDIO_ChannelLR
        fuchsia::mediacodec::AudioChannelId::RR,    // OMX_AUDIO_ChannelRR
};
// We do allow translating OMX_AUDIO_ChannelNone ("unused or empty") to Skip.
constexpr uint32_t kOmxAudioChannelTypeSupportedMin = 0;
constexpr uint32_t kOmxAudioChannelTypeSupportedMax = 9;

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

template <typename OMX_STRUCT>
void InitOmxStruct(OMX_STRUCT* omx_struct) {
  memset(omx_struct, 0, sizeof(*omx_struct));
  omx_struct->nSize = sizeof(*omx_struct);
  // Same as in SoftOMXComponent.cpp.
  omx_struct->nVersion.s.nVersionMajor = 1;
  omx_struct->nVersion.s.nVersionMinor = 0;
  omx_struct->nVersion.s.nRevision = 0;
  omx_struct->nVersion.s.nStep = 0;
}

}  // namespace

namespace codec_runner {

OmxCodecRunner::OmxCodecRunner(async_dispatcher_t* fidl_dispatcher,
                               thrd_t fidl_thread, std::string_view mime_type,
                               std::string_view lib_filename)
    : CodecRunner(fidl_dispatcher, fidl_thread),
      mime_type_(mime_type),
      lib_filename_(lib_filename) {
  // nothing else to do here
}

//
// CodecRunner
//

bool OmxCodecRunner::Load() {
  // Load the per-omx-codec .so and find the one entry point.
  createSoftOMXComponent_fn createSoftOMXComponent = nullptr;
  void* dl = dlopen(lib_filename_.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!dl) {
    printf("dl is nullptr\n");
    return false;
  }
  VLOGF("loaded codec .so file.\n");
  createSoftOMXComponent = reinterpret_cast<createSoftOMXComponent_fn>(
      dlsym(dl, "entrypoint_createSoftOMXComponent"));
  if (!createSoftOMXComponent) {
    printf("dlsym() failed.\n");
    return false;
  }
  VLOGF("found entrypoint.\n");

  // This lock hold interval isn't really needed, but it also doesn't hurt, and
  // it might make it easier to get FXL_GUARDED_BY annotations to work.
  std::unique_lock<std::mutex> lock(lock_);

  omx_callbacks_.EventHandler = omx_EventHandler;
  omx_callbacks_.EmptyBufferDone = omx_EmptyBufferDone;
  omx_callbacks_.FillBufferDone = omx_FillBufferDone;
  OMX_PTR app_data = reinterpret_cast<OMX_PTR>(this);
  // The direct_ version bypasses the .so stuff above, because it's an easier
  // workflow if we don't have to replace two files, for now, until we figure
  // out if there's a clean way to load an .so from an arbitrary file - like
  // with dlopen_vmo or something maybe...  Or with a different config that lets
  // dlopen() find our .so in places other than /system/lib, which is currently
  // hard to replace.  Maybe package server stuff will make this easier soon.
  createSoftOMXComponent("OMX.google.aac.decoder", &omx_callbacks_, app_data,
                         &omx_component_);
  if (!omx_component_) {
    printf("failed to create component_\n");
    return false;
  }
  VLOGF("successfully created omx_component_\n");

  OMX_ERRORTYPE omx_result;

  // SetCallbacks() is nullptr, so apparently we don't need to call it, and the
  // callbacks are passed in above, so that should do it.

  OMX_STATETYPE omx_state;
  omx_result = omx_component_->GetState(omx_component_, &omx_state);
  if (omx_result != OMX_ErrorNone) {
    printf("omx_component->GetState() failed: %d\n", omx_result);
    return false;
  }
  if (omx_state != OMX_StateLoaded) {
    printf("unexpected OMX component state: %d\n", omx_state);
    return false;
  }
  assert(omx_state == OMX_StateLoaded);
  VLOGF("omx_component state is: %d\n", omx_state);
  // Nobody is waiting for the state to change yet, so we can just set
  // omx_state_ here without notifying omx_state_changed_.
  omx_state_ = omx_state;
  // This is OMX_StateLoaded.
  omx_state_desired_ = omx_state;

  // OMX_GetComponentVersion entry point is nullptr.
  // OMX_GetConfig and OMX_SetConfig just return OMX_ErrorUndefined.

  // Find input port and output port indexes.
  //
  // Also check that there are the expected number of ports, though this is
  // slightly indirect and approximate given the lack of any direct way that I
  // can find to check this via OMX.
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  for (int i = 0; i < 3; i++) {
    InitOmxStruct(&port_def);
    port_def.nPortIndex = i;
    omx_result = omx_component_->GetParameter(
        omx_component_, OMX_IndexParamPortDefinition, &port_def);
    // check for errors differently depending on whether port index 2 or less
    // than 2
    if (i != 2) {
      if (omx_result != OMX_ErrorNone) {
        printf("component_->GetParameter() failed: %d\n", omx_result);
        return false;
      }
      if (port_def.eDir == OMX_DirInput) {
        omx_port_index_[kInput] = i;
      } else if (port_def.eDir == OMX_DirOutput) {
        omx_port_index_[kOutput] = i;
      } else {
        printf("unexpected port_def.eDir: %d\n", port_def.eDir);
        return false;
      }
    } else {
      assert(i == 2);
      // Avoid caring which specific error is reutrn for port index 2, but it
      // shouldn't succeed.
      if (omx_result == OMX_ErrorNone) {
        // For now, bail out if we don't find exactly two ports.  There might
        // be reasonable ways to deal with exceptions to this, but until we have
        // an example of a codec that has more than two ports, postpone handling
        // it.
        printf("more than two ports found\n");
        return false;
      }
    }
  }
  if (omx_port_index_[kInput] == 0xFFFFFFFF) {
    printf("failed to find input port\n");
    return false;
  }
  if (omx_port_index_[kOutput] == 0xFFFFFFFF) {
    printf("failed to find output port\n");
    return false;
  }

  VLOGF("input_port_index_: %d\n", omx_port_index_[kInput]);
  VLOGF("output_port_index_: %d\n", omx_port_index_[kOutput]);

  // The default behavior is fine, since we don't need this to be the default
  // loop for any thread.
  //
  // Go ahead and get the StreamControl domain's thread created and started, but
  // its first item will be to wait for the Setup ordering domain to be done,
  // which prevents any overlap between Setup items and StreamControl items.
  //
  // The StreamControl thread is allowed to block.
  stream_control_ =
      std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  zx_status_t start_thread_result = stream_control_->StartThread(
      "StreamControl_ordering_domain", &stream_control_thread_);
  if (start_thread_result != ZX_OK) {
    printf("stream_control_->StartThread() failed\n");
    return false;
  }
  stream_control_dispatcher_ = stream_control_->dispatcher();
  PostSerial(stream_control_dispatcher_, [this] {
    {  // scope lock
      std::unique_lock<std::mutex> lock(lock_);
      while (!is_setup_done_) {
        // We don't share this process across Codec instances, so currently we
        // don't need a way to give up here, short of exiting the whole process.
        is_setup_done_condition_.wait(lock);
      }
    }
  });

  return true;
}

// TODO(dustingreen): this method needs to understand how to translate between
// Codec and OMX for every entry in local_codec_factory.cc.  That means this
// method and similar AudioEncoder/VideoDecoder/VideoEncoder methods will likely
// involve more fan-out to deal with all the formats.
//
// For now it's a non-goal to deal with formats outside the set listed in
// local_codec_factory.cc, and certainly a non-goal here to try to anticipate or
// handle any format beyond what OMX can describe.  Any format future-proofing
// belongs in CodecFactory and Codec interfaces (if anywhere), but not here for
// now.
void OmxCodecRunner::SetDecoderParams(
    fuchsia::mediacodec::CreateDecoder_Params audio_decoder_params) {
  struct AudioDecoder {
    std::string_view codec_mime_type;
    std::string_view omx_mime_type;
    OMX_AUDIO_CODINGTYPE omx_coding_type;
    void (OmxCodecRunner::*set_input_method_ptr)();
  };
  static const AudioDecoder known_audio_decoders[] = {
      // TODO(dustingreen): add audio/aac soon.
      {"audio/aac-adts", "audio/aac", OMX_AUDIO_CodingAAC,
       &OmxCodecRunner::SetInputAacAdts},
  };
  const AudioDecoder* dec = nullptr;
  for (size_t i = 0; i < arraysize(known_audio_decoders); i++) {
    if (known_audio_decoders[i].codec_mime_type ==
        audio_decoder_params.input_details.mime_type.get()) {
      dec = &known_audio_decoders[i];
      break;
    }
  }
  // Reject up front any mime types that we don't handle at all yet.
  if (!dec) {
    // TODO(dustingreen): epitaph
    binding_.reset();
    Exit("SetAudioDecoderParams() couldn't find a suitable decoder");
  }

  decoder_params_ = std::make_unique<fuchsia::mediacodec::CreateDecoder_Params>(
      std::move(audio_decoder_params));
  initial_input_format_details_ =
      fuchsia::mediacodec::CodecFormatDetails::New();
  zx_status_t clone_result =
      decoder_params_->input_details.Clone(initial_input_format_details_.get());
  if (clone_result != ZX_OK) {
    Exit("CodecFormatDetails::Clone() failed - exiting");
  }

  // For the moment, let's check that the input is AAC.
  //
  // TODO(dustingreen): Do this generically across all codecs, probably based on
  // fields in a built-in codec table.
  InitOmxStruct(&omx_initial_port_def_[kInput]);
  omx_initial_port_def_[kInput].nPortIndex = omx_port_index_[kInput];
  OMX_ERRORTYPE omx_result =
      omx_component_->GetParameter(omx_component_, OMX_IndexParamPortDefinition,
                                   &omx_initial_port_def_[kInput]);
  if (omx_result != OMX_ErrorNone) {
    Exit("omx_result->GetParameter(port def, input port) failed: %d\n",
         omx_result);
  }
  if (omx_initial_port_def_[kInput].eDomain != OMX_PortDomainAudio) {
    Exit("unexpected input port eDomain: %d\n",
         omx_initial_port_def_[kInput].eDomain);
  }
  if (omx_initial_port_def_[kInput].format.audio.cMIMEType !=
      dec->omx_mime_type) {
    Exit("unexpected input port mime type: %s\n",
         omx_initial_port_def_[kInput].format.audio.cMIMEType);
  }
  if (omx_initial_port_def_[kInput].format.audio.eEncoding !=
      dec->omx_coding_type) {
    Exit("unexpected input port format.audio.eEncoding: %d\n",
         omx_initial_port_def_[kInput].format.audio.eEncoding);
  }
  if (omx_initial_port_def_[kInput].nBufferAlignment != 1) {
    Exit("unexpected input buffer alignment: %d\n",
         omx_initial_port_def_[kInput].nBufferAlignment);
  }

  // For audio decoders, let's check that the output is PCM.
  //
  // TODO(dustingreen): Do this generically across all codecs, probably based on
  // fields in a built-in codec table.
  InitOmxStruct(&omx_initial_port_def_[kOutput]);
  omx_initial_port_def_[kOutput].nPortIndex = omx_port_index_[kOutput];
  omx_result =
      omx_component_->GetParameter(omx_component_, OMX_IndexParamPortDefinition,
                                   &omx_initial_port_def_[kOutput]);
  if (omx_result != OMX_ErrorNone) {
    Exit("omx_component->GetParameter(port def, output port) failed: %d\n",
         omx_result);
  }
  if (omx_initial_port_def_[kOutput].eDomain != OMX_PortDomainAudio) {
    Exit("unexpected output port eDomain: %d\n",
         omx_initial_port_def_[kOutput].eDomain);
  }
  std::string expected_output_mime("audio/raw");
  if (expected_output_mime !=
      omx_initial_port_def_[kOutput].format.audio.cMIMEType) {
    Exit("unexpected output port mime type: %s\n",
         omx_initial_port_def_[kOutput].format.audio.cMIMEType);
  }
  if (omx_initial_port_def_[kOutput].format.audio.eEncoding !=
      OMX_AUDIO_CodingPCM) {
    Exit("unexpected output port format.audio.eEncoding: %d\n",
         omx_initial_port_def_[kOutput].format.audio.eEncoding);
  }
  if (omx_initial_port_def_[kOutput].nBufferAlignment != 2) {
    Exit("unexpected output buffer alignment: %d\n",
         omx_initial_port_def_[kOutput].nBufferAlignment);
  }

  for (Port port = kFirstPort; port < kPortCount; port++) {
    // intentional copy
    omx_port_def_[port] = omx_initial_port_def_[port];
  }

  // Handle per-format parameter setting.  Method call to a method like
  // SetInputAacAdts() or analogous method.
  (this->*(dec->set_input_method_ptr))();

  // next is ComputeInputConstraints()
}

// Set the AAC decoder to ADTS mode.
void OmxCodecRunner::SetInputAacAdts() {
  OMX_AUDIO_PARAM_AACPROFILETYPE aac_profile;
  InitOmxStruct(&aac_profile);
  aac_profile.nPortIndex = omx_port_index_[kInput];
  OMX_ERRORTYPE omx_result = omx_component_->GetParameter(
      omx_component_, OMX_IndexParamAudioAac, &aac_profile);
  if (omx_result != OMX_ErrorNone) {
    Exit("omx_component->GetParameter(input, aac profile) failed: %d",
         omx_result);
  }
  // For now, we won't strip off the ADTS-ness from the input .adts file, so put
  // the AAC decoder in ADTS mode.
  aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;
  omx_result = omx_component_->SetParameter(
      omx_component_, OMX_IndexParamAudioAac, &aac_profile);
  if (omx_result != OMX_ErrorNone) {
    Exit("omx_component->SetParameter(input, ADTS) failed: %d\n", omx_result);
  }
}

// This is called before the Codec channel is bound, so this class is still
// single-threaded during this method.
void OmxCodecRunner::ComputeInputConstraints() {
  // The OMX info we need to generate input_constraints_ is all in
  // omx_initial_port_def_[kInput] + omx_min_nBufferSize_[kInput] and was
  // already queried from the OMX codec previously.
  //
  // The kInputBufferConstraintsVersionOrdinal is required in the sense that it
  // is the only one and also required, vs output which has both required and
  // not-required buffer_constraints_version_ordinal(s).  Input has this only
  // to allow sharing more code with output.
  last_required_buffer_constraints_version_ordinal_[kInput] =
      kInputBufferConstraintsVersionOrdinal;
  sent_buffer_constraints_version_ordinal_[kInput] =
      kInputBufferConstraintsVersionOrdinal;
  OMX_U32 omx_min_buffer_size = omx_initial_port_def_[kInput].nBufferSize;
  uint32_t packet_count_for_codec_recommended =
      kOmxRecommendedBufferCountVsMinBufferCountFactor *
      omx_initial_port_def_[kInput].nBufferCountMin;
  uint32_t per_packet_buffer_bytes_recommended =
      kOmxRecommendedBufferVsMinBufferFactor * omx_min_buffer_size;
  input_constraints_ =
      std::make_unique<fuchsia::mediacodec::CodecBufferConstraints>(
          fuchsia::mediacodec::CodecBufferConstraints{
              .buffer_constraints_version_ordinal =
                  kInputBufferConstraintsVersionOrdinal,
              .per_packet_buffer_bytes_min = omx_min_buffer_size,
              .per_packet_buffer_bytes_recommended =
                  per_packet_buffer_bytes_recommended,
              .per_packet_buffer_bytes_max =
                  kOmxMaxBufferVsMinBufferFactor * omx_min_buffer_size,
              .packet_count_for_codec_min =
                  omx_initial_port_def_[kInput].nBufferCountMin,
              .packet_count_for_codec_recommended =
                  packet_count_for_codec_recommended,
              .packet_count_for_codec_recommended_max =
                  kOmxRecommendedMaxBufferCountVsMinBufferCountFactor *
                  omx_initial_port_def_[kInput].nBufferCountMin,
              .packet_count_for_codec_max =
                  kOmxMaxBufferCountVsMinBufferCountFactor *
                  omx_initial_port_def_[kInput].nBufferCountMin,
              .packet_count_for_client_max =
                  std::numeric_limits<uint32_t>::max(),
              // TODO(dustingreen): verify that this works end to end for the
              // OmxCodecRunner...
              .single_buffer_mode_allowed = true,

              // default_settings
              //
              // Inititial input buffer_lifetime_ordinal of 1 is ok.  It's also
              // ok if it's any larger odd number, but 1 is the best choice.
              .default_settings.buffer_lifetime_ordinal =
                  kBestFirstBufferLifetimeOrdinal,
              // The buffer_constraints_version_ordinal is a pass-through value
              // so clients will have no reason to change this - it's just so
              // the server knows what version of constraints the client was
              // aware of so far.
              .default_settings.buffer_constraints_version_ordinal =
                  kInputBufferConstraintsVersionOrdinal,
              .default_settings.packet_count_for_codec =
                  packet_count_for_codec_recommended,
              .default_settings.packet_count_for_client =
                  ::fuchsia::mediacodec::kDefaultInputPacketCountForClient,
              .default_settings.per_packet_buffer_bytes =
                  per_packet_buffer_bytes_recommended,
              .default_settings.single_buffer_mode =
                  ::fuchsia::mediacodec::kDefaultInputIsSingleBufferMode,
          });

  // We're about to be bound to the Codec channel, which will immediately send
  // the input_constraints_ to the client as the first server to client message.
}

//
// Codec
//

// The base class is about to send input_constraints_ using
// OnInputConstraints().  Since OMX codecs demand to have output buffers
// configured before generating OMX_EventPortSettingsChanged on the output port,
// and because OMX codecs can potentially not generate that event and just
// output into the initial buffers instead, and because this class doesn't
// virtualize that away with a bunch of memcpy + complicated tracking that would
// be required, the OmxCodecRunner will want to send the output constraints
// asap, which is when this method gets called.
//
// We want to send this _before_ the input constraints to encourage the client
// to configure output before queueing any input data for the first stream, else
// we can end up triggering another output re-config.
//
// This is called on the FIDL thread, but we post any sent messages back to the
// FIDL thread to be sent on a clean thread without lock_ held anyway.
void OmxCodecRunner::onInputConstraintsReady() {
  std::unique_lock<std::mutex> lock(lock_);
  StartIgnoringClientOldOutputConfigLocked();
  GenerateAndSendNewOutputConfig(lock, true);

  // Next is the client sending SetInputBufferSettings()+AddInputBuffer() or
  // SetOutputBufferSettings()+AddOutputBuffer().  Preferably the latter first,
  // but either is permitted.
}

void OmxCodecRunner::GenerateAndSendNewOutputConfig(
    std::unique_lock<std::mutex>& lock,
    bool buffer_constraints_action_required) {
  // This method is only called on these ordering domains:
  //   * Setup ordering domain
  //   * StreamControl ordering domain
  //   * InputData domain if buffer_constraints_action_required is false
  //
  // TODO(dustingreen): Create an assert that checks for the above.  This
  // commented-out assert doesn't include possibility of InputData domain:
  // assert(!is_setup_done_ || thrd_current() == stream_control_thread_);

  uint64_t current_stream_lifetime_ordinal = stream_lifetime_ordinal_;
  uint64_t new_output_buffer_constraints_version_ordinal =
      next_output_buffer_constraints_version_ordinal_++;
  uint64_t new_output_format_details_version_ordinal =
      next_output_format_details_version_ordinal_++;

  // If buffer_constraints_action_required true, the caller bumped the
  // last_required_buffer_constraints_version_ordinal_[kOutput] before calling
  // this method (using StartIgnoringClientOldOutputConfigLocked()), to ensure
  // any output config messages from the client are ignored until the client
  // catches up to at least last_required_buffer_constraints_version_ordinal_.
  assert(!buffer_constraints_action_required ||
         (last_required_buffer_constraints_version_ordinal_[kOutput] ==
          new_output_buffer_constraints_version_ordinal));

  // printf("GenerateAndSendNewOutputConfig
  // new_output_buffer_constraints_version_ordinal: %lu
  // buffer_constraints_action_required: %d\n",
  // new_output_buffer_constraints_version_ordinal,
  // buffer_constraints_action_required);

  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig> output_config;
  {  // scope unlock
    ScopedUnlock unlock(lock);
    // Don't call OMX under the lock_, because we can avoid doing so, and
    // because of paranoia that OMX might call EventHandler() at any time using
    // the same stack that we call OMX on - it's only partly paranoia, since OMX
    // _does_ do that sometimes, for some calls into OMX - so assume that's the
    // contract for all calls into OMX.

    // We know we're the only thread calling this currently, because this method
    // is part of the Setup ordering domain and the onSetupDone() method
    // prevents any overlap between Setup and StreamControl.
    output_config =
        BuildNewOutputConfig(current_stream_lifetime_ordinal,
                             new_output_buffer_constraints_version_ordinal,
                             new_output_format_details_version_ordinal,
                             buffer_constraints_action_required);
  }  // ~unlock
  assert(current_stream_lifetime_ordinal == stream_lifetime_ordinal_);

  output_config_ = std::move(output_config);

  // Stay under lock after setting output_config_, to get proper ordering of
  // sent messages even if a hostile client deduces the content of this message
  // before we've sent it and manages to get the server to send another
  // subsequent OnOutputConfig().

  assert(sent_buffer_constraints_version_ordinal_[kOutput] + 1 ==
         new_output_buffer_constraints_version_ordinal);
  assert(sent_format_details_version_ordinal_[kOutput] + 1 ==
         new_output_format_details_version_ordinal);

  // Setting this within same lock hold interval as we queue the message to be
  // sent in order vs. other OnOutputConfig() messages.  This way we can verify
  // that the client's incoming messages are not trying to configure with
  // respect to a buffer_constraints_version_ordinal that is newer than we've
  // actually sent the client.
  sent_buffer_constraints_version_ordinal_[kOutput] =
      new_output_buffer_constraints_version_ordinal;
  sent_format_details_version_ordinal_[kOutput] =
      new_output_format_details_version_ordinal;

  // Intentional copy of fuchsia::mediacodec::OutputConfig output_config_ here,
  // as we want output_config_ to remain valid (at least for debugging reasons
  // for now).
  fuchsia::mediacodec::CodecOutputConfig config_copy;
  zx_status_t clone_status = output_config_->Clone(&config_copy);
  if (clone_status != ZX_OK) {
    Exit("CodecOutputConfig::Clone() failed - exiting - status: %d\n",
         clone_status);
  }
  VLOGF("GenerateAndSendNewOutputConfig() - fidl_dispatcher_: %p\n",
        fidl_dispatcher_);
  PostSerial(fidl_dispatcher_,
             [this, output_config = std::move(config_copy)]() mutable {
               binding_->events().OnOutputConfig(std::move(output_config));
             });
}

void OmxCodecRunner::onSetupDone() {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    is_setup_done_ = true;
  }  // ~lock
  is_setup_done_condition_.notify_all();
}

// The only valid caller of this is EnsureStreamClosed().  We have this in a
// separate method only to make it easier to assert a couple things in the
// caller.
void OmxCodecRunner::EnsureCodecStreamClosedLockedInternal() {
  assert(thrd_current() == stream_control_thread_);
  if (stream_lifetime_ordinal_ % 2 == 0) {
    // Already closed.
    return;
  }
  assert(stream_queue_.front()->stream_lifetime_ordinal() ==
         stream_lifetime_ordinal_);
  stream_ = nullptr;
  stream_queue_.pop_front();
  stream_lifetime_ordinal_++;
  // Even values mean no current stream.
  assert(stream_lifetime_ordinal_ % 2 == 0);
}

// This is called on Output ordering domain (FIDL thread) any time a message is
// received which would be able to start a new stream.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordinal_.
void OmxCodecRunner::EnsureFutureStreamSeenLocked(
    uint64_t stream_lifetime_ordinal) {
  if (future_stream_lifetime_ordinal_ == stream_lifetime_ordinal) {
    return;
  }
  if (stream_lifetime_ordinal < future_stream_lifetime_ordinal_) {
    Exit("stream_lifetime_ordinal went backward - exiting\n");
  }
  assert(stream_lifetime_ordinal > future_stream_lifetime_ordinal_);
  if (future_stream_lifetime_ordinal_ % 2 == 1) {
    EnsureFutureStreamCloseSeenLocked(future_stream_lifetime_ordinal_);
  }
  future_stream_lifetime_ordinal_ = stream_lifetime_ordinal;
  stream_queue_.push_back(std::make_unique<Stream>(stream_lifetime_ordinal));
  if (stream_queue_.size() > kMaxInFlightStreams) {
    Exit(
        "kMaxInFlightStreams reached - clients capable of causing this are "
        "instead supposed to wait/postpone to prevent this from occurring - "
        "exiting\n");
  }
}

// This is called on Output ordering domain (FIDL thread) any time a message is
// received which would close a stream.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordinal_.
void OmxCodecRunner::EnsureFutureStreamCloseSeenLocked(
    uint64_t stream_lifetime_ordinal) {
  if (future_stream_lifetime_ordinal_ % 2 == 0) {
    // Already closed.
    if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_ - 1) {
      Exit(
          "CloseCurrentStream() seen with stream_lifetime_ordinal != "
          "most-recent seen stream - exiting\n");
    }
    return;
  }
  if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_) {
    Exit(
        "attempt to close a stream other than the latest seen stream - "
        "exiting\n");
  }
  assert(stream_lifetime_ordinal == future_stream_lifetime_ordinal_);
  assert(stream_queue_.size() >= 1);
  Stream* closing_stream = stream_queue_.back().get();
  assert(closing_stream->stream_lifetime_ordinal() == stream_lifetime_ordinal);
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
  assert(future_stream_lifetime_ordinal_ % 2 == 0);
}

// This is called on Output ordering domain (FIDL thread) any time a flush is
// seen.
//
// More complete protocol validation happens on StreamControl ordering domain.
// The validation here is just to validate to degree needed to not break our
// stream_queue_ and future_stream_lifetime_ordinal_.
void OmxCodecRunner::EnsureFutureStreamFlushSeenLocked(
    uint64_t stream_lifetime_ordinal) {
  if (stream_lifetime_ordinal != future_stream_lifetime_ordinal_) {
    Exit(
        "FlushCurrentStream() stream_lifetime_ordinal inconsistent - "
        "exiting\n");
  }
  assert(stream_queue_.size() >= 1);
  Stream* flushing_stream = stream_queue_.back().get();
  // Thanks to the above future_stream_lifetime_ordinal_ check, we know the
  // future stream is not discarded yet.
  assert(!flushing_stream->future_discarded());
  if (flushing_stream->future_flush_end_of_stream()) {
    Exit("FlushCurrentStream() used twice on same stream - exiting\n");
  }

  // We don't future-verify that we have a QueueInputEndOfStream(). We'll verify
  // that later when StreamControl catches up to this stream.

  // Remember the flush so we later know that a close doesn't imply discard.
  flushing_stream->SetFutureFlushEndOfStream();

  // A FlushEndOfStreamAndCloseStream() is also a close, after the flush.  This
  // keeps future_stream_lifetime_ordinal_ consistent.
  EnsureFutureStreamCloseSeenLocked(stream_lifetime_ordinal);
}

// Caller must ensure that this is called only on one thread at a time.
std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
OmxCodecRunner::BuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  return CreateNewOutputConfigFromOmxOutputFormat(
      OmxGetOutputFormat(), stream_lifetime_ordinal,
      new_output_buffer_constraints_version_ordinal,
      new_output_format_details_version_ordinal,
      buffer_constraints_action_required);
}

// Caller must ensure that this is called only on one thread at a time.
std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
OmxCodecRunner::CreateNewOutputConfigFromOmxOutputFormat(
    std::unique_ptr<const OmxCodecRunner::OMX_GENERIC_PORT_FORMAT>
        omx_output_format,
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  // Unfortunately OMX only allows nBufferSize to increase, never decrease, so
  // we have to convey that to the output constraints also, since we don't have
  // any per-omx-buffer-lifetime way of reducing how much output data might be
  // generated per output buffer.  So we really are stuck with a min that's
  // whatever OMX's nBufferSize is so far.  For input the situation is
  // different since we can control how many valid bytes per input buffer
  // lifetime.
  OMX_U32 per_packet_buffer_bytes_min =
      omx_output_format->definition.nBufferSize;
  const OMX_PARAM_PORTDEFINITIONTYPE& port = omx_output_format->definition;
  uint32_t per_packet_buffer_bytes_recommended =
      kOmxRecommendedBufferVsMinBufferFactor * per_packet_buffer_bytes_min;
  uint32_t packet_count_for_codec_recommended =
      kOmxRecommendedBufferCountVsMinBufferCountFactor * port.nBufferCountMin;
  std::unique_ptr<fuchsia::mediacodec::CodecOutputConfig> result =
      std::make_unique<fuchsia::mediacodec::CodecOutputConfig>(
          fuchsia::mediacodec::CodecOutputConfig{
              .stream_lifetime_ordinal = stream_lifetime_ordinal_,
              .buffer_constraints_action_required =
                  buffer_constraints_action_required,
              .buffer_constraints.buffer_constraints_version_ordinal =
                  new_output_buffer_constraints_version_ordinal,
              .buffer_constraints.per_packet_buffer_bytes_min =
                  per_packet_buffer_bytes_min,
              .buffer_constraints.per_packet_buffer_bytes_recommended =
                  per_packet_buffer_bytes_recommended,
              .buffer_constraints.per_packet_buffer_bytes_max =
                  kOmxMaxBufferVsMinBufferFactor * per_packet_buffer_bytes_min,
              .buffer_constraints.packet_count_for_codec_min =
                  port.nBufferCountMin,
              .buffer_constraints.packet_count_for_codec_recommended =
                  packet_count_for_codec_recommended,
              .buffer_constraints.packet_count_for_codec_recommended_max =
                  kOmxRecommendedMaxBufferCountVsMinBufferCountFactor *
                  port.nBufferCountMin,
              .buffer_constraints.packet_count_for_codec_max =
                  kOmxMaxBufferCountVsMinBufferCountFactor *
                  port.nBufferCountMin,
              .buffer_constraints.packet_count_for_client_max =
                  std::numeric_limits<uint32_t>::max(),
              .buffer_constraints.single_buffer_mode_allowed = false,

              // default_settings
              //
              // Can't/won't help the client pick the client's
              // buffer_lifetime_ordinal for output.
              .buffer_constraints.default_settings.buffer_lifetime_ordinal =
                  kInvalidDefaultBufferLifetimeOrdinal,
              // The buffer_constraints_version_ordinal is a pass-through value
              // so clients will have no reason to change this - it's just so
              // the server knows what version of constraints the client was
              // aware of so far.
              .buffer_constraints.default_settings
                  .buffer_constraints_version_ordinal =
                  new_output_buffer_constraints_version_ordinal,
              .buffer_constraints.default_settings.packet_count_for_codec =
                  packet_count_for_codec_recommended,
              .buffer_constraints.default_settings.packet_count_for_client =
                  ::fuchsia::mediacodec::kDefaultOutputPacketCountForClient,
              .buffer_constraints.default_settings.per_packet_buffer_bytes =
                  per_packet_buffer_bytes_recommended,
              .buffer_constraints.default_settings.single_buffer_mode =
                  ::fuchsia::mediacodec::kDefaultOutputIsSingleBufferMode,

              .format_details.format_details_version_ordinal =
                  new_output_format_details_version_ordinal,
          });
  switch (omx_output_format->definition.eDomain) {
    case OMX_PortDomainAudio:
      PopulateFormatDetailsFromOmxOutputFormat_Audio(*omx_output_format.get(),
                                                     &result->format_details);
      break;
    case OMX_PortDomainVideo:
      // TODO(dustingreen): handle video format details - it likely makes sense
      // to switch to the common format details FIDL struct/table first though.
      Exit("for now, video OMX eDomain is not handled");
      break;
    default:
      // TODO(dustingreen): epitaph
      Exit("unrecognized OMX eDomain: %d",
           omx_output_format->definition.eDomain);
      break;
  }
  return result;
}

// Fill out everything except format_details_version_ordinal.
//
// TODO(dustingreen): handle audio encoders, which will need to fill out
// codec_oob_config based on the first output data, if available.
void OmxCodecRunner::PopulateFormatDetailsFromOmxOutputFormat_Audio(
    const OmxCodecRunner::OMX_GENERIC_PORT_FORMAT& omx_output_format,
    fuchsia::mediacodec::CodecFormatDetails* format_details) {
  assert(omx_output_format.definition.eDir == OMX_DirOutput);
  assert(omx_output_format.definition.eDomain == OMX_PortDomainAudio);
  const OMX_AUDIO_PORTDEFINITIONTYPE& omx_audio_port_def =
      omx_output_format.definition.format.audio;
  const OMX_AUDIO_PARAM_PORTFORMATTYPE& omx_audio_param_port_format =
      omx_output_format.audio.format;
  format_details->mime_type = omx_audio_port_def.cMIMEType;
  if (omx_audio_port_def.eEncoding != omx_audio_param_port_format.eEncoding) {
    Exit("inconsistent eEncoding from OMX - exiting");
  }
  assert(omx_audio_port_def.eEncoding == omx_audio_param_port_format.eEncoding);
  fuchsia::mediacodec::AudioFormat audio_format{};
  switch (omx_audio_param_port_format.eEncoding) {
    case OMX_AUDIO_CodingPCM: {
      const OMX_AUDIO_PARAM_PCMMODETYPE& omx_pcm = omx_output_format.audio.pcm;
      fuchsia::mediacodec::PcmFormat pcm{};
      switch (omx_pcm.ePCMMode) {
        case OMX_AUDIO_PCMModeLinear:
          pcm.pcm_mode = fuchsia::mediacodec::AudioPcmMode::LINEAR;
          break;
        default:
          Exit("unhandled OMX_AUDIO_PARAM_PCMMODETYPE.ePCMMode value: %d",
               omx_pcm.ePCMMode);
          break;
      }
      pcm.bits_per_sample = omx_pcm.nBitPerSample;
      pcm.frames_per_second = omx_pcm.nSamplingRate;
      std::vector<fuchsia::mediacodec::AudioChannelId> channel_map(
          omx_pcm.nChannels);
      for (uint32_t i = 0; i < omx_pcm.nChannels; i++) {
        channel_map[i] =
            AudioChannelIdFromOmxAudioChannelType(omx_pcm.eChannelMapping[i]);
      }
      pcm.channel_map.reset(channel_map);
      fuchsia::mediacodec::AudioUncompressedFormat uncompressed{};
      uncompressed.set_pcm(std::move(pcm));
      audio_format.set_uncompressed(std::move(uncompressed));
    } break;
    case OMX_AUDIO_CodingAAC:
      // TODO(dustingreen): implement, at least for AAC encode
      // fallthough for now
    default:
      Exit("unhandled OMX output format - value: %d",
           omx_audio_param_port_format.eEncoding);
      break;
  }
  format_details->domain = fuchsia::mediacodec::DomainFormat::New();
  format_details->domain->set_audio(std::move(audio_format));
}

std::unique_ptr<const OmxCodecRunner::OMX_GENERIC_PORT_FORMAT>
OmxCodecRunner::OmxGetOutputFormat() {
  std::unique_ptr<OMX_GENERIC_PORT_FORMAT> result =
      std::make_unique<OMX_GENERIC_PORT_FORMAT>();
  // Grab all the output format info.
  InitOmxStruct(&result->definition);
  result->definition.nPortIndex = omx_port_index_[kOutput];
  OMX_ERRORTYPE omx_result = omx_component_->GetParameter(
      omx_component_, OMX_IndexParamPortDefinition, &result->definition);
  if (omx_result != OMX_ErrorNone) {
    Exit("Couldn't get output port definition from OMX: %d", omx_result);
  }
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    // intentional copy
    //
    // We're stashing this structure from here because this method happens to be
    // the common code path invovled in all OMX updates of the output port
    // definition where constraints might change which we need to pay attention
    // to later.  Mainly we care about nBufferSize.
    omx_port_def_[kOutput] = result->definition;
  }
  switch (result->definition.eDomain) {
    case OMX_PortDomainAudio:
      InitOmxStruct(&result->audio.format);
      result->audio.format.nPortIndex = omx_port_index_[kOutput];
      omx_result = omx_component_->GetParameter(
          omx_component_, OMX_IndexParamAudioPortFormat, &result->audio.format);
      if (omx_result != OMX_ErrorNone) {
        Exit(
            "GetParameter(OMX_IndexParamAudioPortFormat) failed: %d - "
            "exiting\n",
            omx_result);
      }
      switch (result->audio.format.eEncoding) {
        case OMX_AUDIO_CodingPCM:
          InitOmxStruct(&result->audio.pcm);
          result->audio.pcm.nPortIndex = omx_port_index_[kOutput];
          omx_result = omx_component_->GetParameter(
              omx_component_, OMX_IndexParamAudioPcm, &result->audio.pcm);
          if (omx_result != OMX_ErrorNone) {
            Exit("GetParameter(OMX_IndexParamAudioPcm) failed: %d - exiting\n",
                 omx_result);
          }
          break;
        default:
          Exit(
              "un-handled output_port_format_.audio.format.eEncoding: %d - "
              "exiting\n",
              result->audio.format.eEncoding);
      }
      break;
    case OMX_PortDomainVideo:
      Exit("currently un-handled eDomain video: %d - exiting\n",
           result->definition.eDomain);
    default:
      Exit("un-handled eDomain: %d - exiting\n", result->definition.eDomain);
  }
  return result;
}

void OmxCodecRunner::EnableOnStreamFailed() {
  std::unique_lock<std::mutex> lock(lock_);
  enable_on_stream_failed_ = true;
}

void OmxCodecRunner::SetInputBufferSettings(
    fuchsia::mediacodec::CodecPortBufferSettings input_settings) {
  PostSerial(stream_control_dispatcher_,
             [this, input_settings = input_settings] {
               SetInputBufferSettings_StreamControl(input_settings);
             });
}

void OmxCodecRunner::SetInputBufferSettings_StreamControl(
    fuchsia::mediacodec::CodecPortBufferSettings input_settings) {
  assert(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (!input_constraints_sent_) {
      Exit(
          "client sent SetInputBufferSettings() before first "
          "OnInputConstraints()");
    }

    if (IsStreamActiveLocked()) {
      Exit(
          "client sent SetInputBufferSettings() with stream active - "
          "exiting\n");
    }

    SetBufferSettingsCommonLocked(kInput, input_settings, *input_constraints_);
  }  // ~lock
}

void OmxCodecRunner::AddInputBuffer(fuchsia::mediacodec::CodecBuffer buffer) {
  PostSerial(stream_control_dispatcher_,
             [this, buffer = std::move(buffer)]() mutable {
               AddInputBuffer_StreamControl(std::move(buffer));
             });
}

void OmxCodecRunner::AddInputBuffer_StreamControl(
    fuchsia::mediacodec::CodecBuffer buffer) {
  assert(thrd_current() == stream_control_thread_);
  AddBufferCommon(kInput, std::move(buffer));
}

void OmxCodecRunner::SetBufferSettingsCommonLocked(
    Port port, const fuchsia::mediacodec::CodecPortBufferSettings& settings,
    const fuchsia::mediacodec::CodecBufferConstraints& constraints) {
  // Invariant
  assert((!port_settings_[port] && buffer_lifetime_ordinal_[port] == 0) ||
         (buffer_lifetime_ordinal_[port] >=
              port_settings_[port]->buffer_lifetime_ordinal &&
          buffer_lifetime_ordinal_[port] <=
              port_settings_[port]->buffer_lifetime_ordinal + 1));

  if (settings.buffer_lifetime_ordinal <=
      protocol_buffer_lifetime_ordinal_[port]) {
    Exit(
        "settings.buffer_lifetime_ordinal <= "
        "protocol_buffer_lifetime_ordinal_[port] - exiting - port: %d\n",
        port);
  }
  protocol_buffer_lifetime_ordinal_[port] = settings.buffer_lifetime_ordinal;

  if (settings.buffer_lifetime_ordinal % 2 == 0) {
    Exit(
        "only odd values for buffer_lifetime_ordinal are permitted - exiting - "
        "port: %d value: %lu\n",
        port, settings.buffer_lifetime_ordinal);
  }

  if (settings.buffer_constraints_version_ordinal >
      sent_buffer_constraints_version_ordinal_[port]) {
    Exit(
        "client sent too-new buffer_constraints_version_ordinal - exiting - "
        "port: %d\n",
        port);
  }

  if (settings.buffer_constraints_version_ordinal <
      last_required_buffer_constraints_version_ordinal_[port]) {
    // ignore - client will (probably) catch up later
    return;
  }

  // We've peeled off too new and too old above.
  assert(settings.buffer_constraints_version_ordinal >=
             last_required_buffer_constraints_version_ordinal_[port] &&
         settings.buffer_constraints_version_ordinal <=
             sent_buffer_constraints_version_ordinal_[port]);

  // We've already checked above that the buffer_lifetime_ordinal is in
  // sequence.
  assert(!port_settings_[port] ||
         settings.buffer_lifetime_ordinal > buffer_lifetime_ordinal_[port]);

  ValidateBufferSettingsVsConstraints(port, settings, constraints);

  // Regardless of mid-stream output config change or not (only relevant to
  // output), we know that buffers aren't with OMX currently, so we can just
  // de-ref low-layer output buffers without needing to interact with OMX here.

  // Little if any reason to do this outside the lock.
  EnsureBuffersNotConfiguredLocked(port);

  // This also starts the new buffer_lifetime_ordinal.
  port_settings_[port] =
      std::make_unique<fuchsia::mediacodec::CodecPortBufferSettings>(
          std::move(settings));
  buffer_lifetime_ordinal_[port] =
      port_settings_[port]->buffer_lifetime_ordinal;
}

void OmxCodecRunner::SetOutputBufferSettings(
    fuchsia::mediacodec::CodecPortBufferSettings output_settings) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (!output_config_) {
      // invalid client behavior
      //
      // client must have received at least the initial OnOutputConfig() first
      // before sending SetOutputBufferSettings().
      Exit(
          "client sent SetOutputBufferSettings() when no output_config_ - "
          "exiting\n");
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
      Exit(
          "client sent SetOutputBufferSettings() with IsStreamActiveLocked() + "
          "already-configured output");
    }

    SetBufferSettingsCommonLocked(kOutput, output_settings,
                                  output_config_->buffer_constraints);
  }  // ~lock
}

void OmxCodecRunner::AddOutputBuffer(fuchsia::mediacodec::CodecBuffer buffer) {
  bool output_done_configuring = AddBufferCommon(kOutput, std::move(buffer));
  if (output_done_configuring) {
    // The StreamControl domain _might_ be waiting for output to be configured.
    wake_stream_control_.notify_all();
  }
}

bool OmxCodecRunner::AddBufferCommon(Port port,
                                     fuchsia::mediacodec::CodecBuffer buffer) {
  bool done_configuring = false;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    if (buffer.buffer_lifetime_ordinal % 2 == 0) {
      Exit(
          "client sent even buffer_lifetime_ordinal, but must be odd - exiting "
          "- port: %u\n",
          port);
    }

    if (buffer.buffer_lifetime_ordinal !=
        protocol_buffer_lifetime_ordinal_[port]) {
      Exit(
          "incoherent SetOutputBufferSettings()/SetInputBufferSettings() + "
          "AddOutputBuffer()/AddInputBuffer()s - exiting - port: %d\n",
          port);
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
      assert(port == kOutput);

      // Ignore the client's message.  The client will probably catch up later.
      return false;
    }

    if (buffer.buffer_index != all_buffers_[port].size()) {
      Exit(
          "AddOutputBuffer()/AddInputBuffer() had buffer_index out of sequence "
          "- port: %d buffer_index: %u all_buffers_[port].size(): %lu",
          port, buffer.buffer_index, all_buffers_[port].size());
    }

    uint32_t required_buffer_count =
        BufferCountFromPortSettings(*port_settings_[port]);
    if (buffer.buffer_index >= required_buffer_count) {
      Exit("AddOutputBuffer()/AddInputBuffer() extra buffer - port: %d", port);
    }

    // So far, there's little reason to avoid doing the Init() part under the
    // lock, even if it can be a bit more time consuming, since there's no data
    // processing happening at this point anyway, and there wouldn't be any
    // happening in any other code location where we could potentially move the
    // Init() either.

    std::unique_ptr<Buffer> local_buffer =
        std::make_unique<Buffer>(this, port, std::move(buffer));
    if (!local_buffer->Init()) {
      Exit(
          "AddOutputBuffer()/AddInputBuffer() couldn't Init() new buffer - "
          "port: %d",
          port);
    }
    all_buffers_[port].push_back(std::move(local_buffer));
    if (all_buffers_[port].size() == required_buffer_count) {
      // Now we allocate all_packets_[port].
      assert(all_packets_[port].empty());
      uint32_t packet_count =
          PacketCountFromPortSettings(*port_settings_[port]);
      for (uint32_t i = 0; i < packet_count; i++) {
        uint32_t buffer_index = required_buffer_count == 1 ? 0 : i;
        Buffer* buffer = all_buffers_[port][buffer_index].get();
        assert(buffer_lifetime_ordinal_[port] ==
               port_settings_[port]->buffer_lifetime_ordinal);
        all_packets_[port].push_back(std::make_unique<Packet>(
            port_settings_[port]->buffer_lifetime_ordinal, i, buffer));
      }
      // On input, free with client.  On output, free with Codec server.
      // Either way, initially free with the producer of data.
      packet_free_bits_[port].resize(packet_count, true);

      // Now we allocate omx_input_packet_oob_ and omx_input_packet_eos_, if
      // this is input.
      if (port == kInput) {
        // For the oob packet, we do need a real buffer, and it needs to be able
        // to hold real (oob) data, so we have to allocate a buffer for this
        // purpose server-side, since the Codec client won't be providing one.
        //
        // For now, we just allocate kMaxCodecOobBytesSize for this (none of the
        // relevant codecs need larger, and kMaxCodecOobBytesSize is 1 page
        // which is a non-zero-sized VMO's minimum size).
        //
        // We (in general) lie to OMX about the size being at least
        // OMX_PARAM_PORTDEFINITIONTYPE.nBufferSize when allocating an OMX
        // buffer for this packet, then we don't actaully fill beyond
        // kMaxCodecOobBytesSize.
        //
        // We don't really care about OMX_PARAM_PORTDEFINITIONTYPE.nBufferSize
        // aside from properly lying to OMX, since the size of normal buffers
        // was never really directly relevant to how large OOB data can be.  In
        // other words, we don't force ourselves to support up to
        // OMX_PARAM_PORTDEFINITIONTYPE.nBufferSize bytes of OOB config data,
        // because there's no real value in doing so, despite OMX essentially
        // sortof supporting up to that much OOB data in a
        // OMX_BUFFERFLAG_CODECCONFIG buffer.
        //
        // If kMaxCodecOobBytesSize isn't page size aligned, zx_vmo_create()
        // will round up for us, so we don't have to handle that possibility
        // here.
        assert(!omx_input_buffer_oob_);
        assert(!omx_input_packet_oob_);
        static_assert(fuchsia::mediacodec::kMaxCodecOobBytesSize <=
                          ZX_CHANNEL_MAX_MSG_BYTES,
                      "fuchsia::mediacodec::kMaxCodecOobBytesSize must be <= "
                      "ZX_CHANNEL_MAX_MSG_BYTES");
        zx::vmo oob_vmo;
        zx_status_t vmo_create_status = zx::vmo::create(
            fuchsia::mediacodec::kMaxCodecOobBytesSize, 0, &oob_vmo);
        if (vmo_create_status != ZX_OK) {
          Exit("zx::vmo::create() failed for omx_input_buffer_oob_");
        }
        fuchsia::mediacodec::CodecBuffer oob_buffer{
            .buffer_lifetime_ordinal =
                port_settings_[port]->buffer_lifetime_ordinal,
            // We don't really use this for anything, so just set it to one
            // beyond the last Codec protocol buffer_index, to avoid any
            // ambiguity with any real buffer_index.
            .buffer_index = required_buffer_count,
        };
        oob_buffer.data.set_vmo(fuchsia::mediacodec::CodecBufferDataVmo{
            .vmo_handle = std::move(oob_vmo),
            .vmo_usable_start = 0,
            .vmo_usable_size = fuchsia::mediacodec::kMaxCodecOobBytesSize,
        });
        omx_input_buffer_oob_ =
            std::make_unique<Buffer>(this, kInput, std::move(oob_buffer));
        // Unlike most input packets, the server requires the ability to write
        // to this input packet's buffer.
        if (!omx_input_buffer_oob_->Init(true)) {
          Exit("omx_input_buffer_oob_->Init() failed");
        }
        omx_input_packet_oob_ = std::make_unique<Packet>(
            port_settings_[port]->buffer_lifetime_ordinal,
            packet_count + kHiddenInputPacketIndexOffsetOob,
            omx_input_buffer_oob_.get());

        // For the eos packet, we don't really need a real buffer, so we just
        // share buffer 0.
        assert(!omx_input_packet_eos_);
        Buffer* buffer = all_buffers_[port][0].get();
        assert(buffer_lifetime_ordinal_[port] ==
               port_settings_[port]->buffer_lifetime_ordinal);
        omx_input_packet_eos_ = std::make_unique<Packet>(
            port_settings_[port]->buffer_lifetime_ordinal,
            packet_count + kHiddenInputPacketIndexOffsetEos, buffer);
      }

      // We tell OMX about the potentially-new buffer count separately later,
      // just before moving from OMX loaded to OMX idle, or as part of
      // mid-stream output config change.

      // We don't allocate OMX_BUFFERHEADERTYPE yet here by calling OMX
      // UseBuffer() yet, because we can be in OMX_StateLoaded currently, and
      // OMX UseBuffer() isn't valid until we're moving from OMX_StateLoaded
      // to OMX_StateIdle.

      done_configuring = true;
    }
  }
  return done_configuring;
}

void OmxCodecRunner::FlushEndOfStreamAndCloseStream(
    uint64_t stream_lifetime_ordinal) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    EnsureFutureStreamFlushSeenLocked(stream_lifetime_ordinal);
  }
  PostSerial(stream_control_dispatcher_, [this, stream_lifetime_ordinal] {
    FlushEndOfStreamAndCloseStream_StreamControl(stream_lifetime_ordinal);
  });
}

void OmxCodecRunner::FlushEndOfStreamAndCloseStream_StreamControl(
    uint64_t stream_lifetime_ordinal) {
  assert(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // We re-check some things which were already future-verified a different
    // way, to allow for flexibility in the future-tracking stuff to permit less
    // checking in the Output ordering domain (FIDL thread) without breaking
    // overall verification of a flush.  Any checking in the Output ordering
    // domain (FIDL thread) is for the future-tracking's own convenience only.
    // The checking here is the real checking.

    CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal);
    assert(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
    if (!IsStreamActiveLocked() ||
        stream_lifetime_ordinal != stream_lifetime_ordinal_) {
      // TODO(dustingreen): epitaph
      Exit(
          "FlushEndOfStreamAndCloseStream() only valid on an active current "
          "stream (flush does not auto-create a new stream)");
    }
    // At this point we know that the stream is not discarded, and not already
    // flushed previously (because flush will discard the stream as there's
    // nothing more that the stream is permitted to do).
    assert(stream_);
    assert(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal);
    if (!stream_->input_end_of_stream()) {
      Exit(
          "FlushEndOfStreamAndCloseStream() is only permitted after "
          "QueueInputEndOfStream()");
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
      // OMX codecs have no way to report mid-stream input data corruption
      // errors or similar without it being a stream failure, so if there's any
      // stream error it turns into OnStreamFailed().  It's also permitted for a
      // server to set error_detected_ bool(s) on output packets and send
      // OnOutputEndOfStream() despite detected errors, but this is only a
      // reasonable behavior for the server if the server normally would detect
      // and report mid-stream input corruption errors without an
      // OnStreamFailed().
      output_end_of_stream_seen_.wait(lock);
    }

    // Now that flush is done, we close the current stream because there is not
    // any subsequent message for the current stream that's valid.
    EnsureStreamClosed(lock);
  }  // ~lock
}

// This message is required to be idempotent.
void OmxCodecRunner::CloseCurrentStream(uint64_t stream_lifetime_ordinal,
                                        bool release_input_buffers,
                                        bool release_output_buffers) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    EnsureFutureStreamCloseSeenLocked(stream_lifetime_ordinal);
  }  // ~lock
  PostSerial(stream_control_dispatcher_, [this, stream_lifetime_ordinal,
                                          release_input_buffers,
                                          release_output_buffers] {
    CloseCurrentStream_StreamControl(
        stream_lifetime_ordinal, release_input_buffers, release_output_buffers);
  });
}

void OmxCodecRunner::CloseCurrentStream_StreamControl(
    uint64_t stream_lifetime_ordinal, bool release_input_buffers,
    bool release_output_buffers) {
  std::unique_lock<std::mutex> lock(lock_);
  EnsureStreamClosed(lock);
  if (release_input_buffers) {
    EnsureBuffersNotConfiguredLocked(kInput);
  }
  if (release_output_buffers) {
    EnsureBuffersNotConfiguredLocked(kOutput);
  }
}

void OmxCodecRunner::Sync(SyncCallback callback) {
  // By posting to StreamControl ordering domain before calling the callback, we
  // sync the Output ordering domain and the StreamControl ordering domain.
  PostSerial(stream_control_dispatcher_,
             [this, callback = std::move(callback)]() mutable {
               Sync_StreamControl(std::move(callback));
             });
}

void OmxCodecRunner::Sync_StreamControl(SyncCallback callback) { callback(); }

void OmxCodecRunner::RecycleOutputPacket(
    fuchsia::mediacodec::CodecPacketHeader available_output_packet) {
  std::unique_lock<std::mutex> lock(lock_);
  CheckOldBufferLifetimeOrdinalLocked(
      kOutput, available_output_packet.buffer_lifetime_ordinal);
  if (available_output_packet.buffer_lifetime_ordinal <
      buffer_lifetime_ordinal_[kOutput]) {
    // ignore arbitrarily-stale required by protocol
    //
    // Thanks to even values from the client being prohibited, this also covers
    // mid-stream output config change where the server has already
    // de-configured output buffers but the client doesn't know about that yet.
    // We include that case here by setting
    // buffer_lifetime_ordinal_[kOutput] to the next even value
    // when de-configuring output server-side until the client has re-configured
    // output.
    return;
  }
  assert(available_output_packet.buffer_lifetime_ordinal ==
         buffer_lifetime_ordinal_[kOutput]);
  if (!IsOutputConfiguredLocked()) {
    Exit(
        "client sent RecycleOutputPacket() for buffer_lifetime_ordinal that "
        "isn't fully configured yet - bad client behavior");
  }
  assert(IsOutputConfiguredLocked());
  assert(!packet_free_bits_[kOutput].empty());
  assert(all_packets_[kOutput].size() == packet_free_bits_[kOutput].size());
  if (available_output_packet.packet_index >= all_packets_[kOutput].size()) {
    Exit("out of range packet_index from client in RecycleOutputPacket()");
  }
  uint32_t packet_index = available_output_packet.packet_index;
  if (packet_free_bits_[kOutput][packet_index]) {
    Exit(
        "packet_index already free at protocol level - invalid client message");
  }
  // Mark free at protocol level.
  packet_free_bits_[kOutput][packet_index] = true;

  // Recycle to OMX layer, if presently in acceptable OMX state.
  OmxTryRecycleOutputPacketLocked(
      all_packets_[kOutput][packet_index]->omx_header());
}

// TODO(dustingreen): At least for decoders, get the OOB config data if any,
// stash it temporarily, and convert to CODECCONFIG (instead of the codec
// creation format details).
void OmxCodecRunner::QueueInputFormatDetails(
    uint64_t stream_lifetime_ordinal,
    fuchsia::mediacodec::CodecFormatDetails format_details) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    EnsureFutureStreamSeenLocked(stream_lifetime_ordinal);
  }  // ~lock
  PostSerial(stream_control_dispatcher_,
             [this, stream_lifetime_ordinal,
              format_details = std::move(format_details)]() mutable {
               QueueInputFormatDetails_StreamControl(stream_lifetime_ordinal,
                                                     std::move(format_details));
             });
}

void OmxCodecRunner::QueueInputFormatDetails_StreamControl(
    uint64_t stream_lifetime_ordinal,
    fuchsia::mediacodec::CodecFormatDetails format_details) {
  assert(thrd_current() == stream_control_thread_);

  std::unique_lock<std::mutex> lock(lock_);
  CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal);
  assert(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
  if (stream_lifetime_ordinal > stream_lifetime_ordinal_) {
    StartNewStream(lock, stream_lifetime_ordinal);
  }
  assert(stream_lifetime_ordinal == stream_lifetime_ordinal_);
  if (stream_->input_end_of_stream()) {
    Exit("QueueInputFormatDetails() after QueueInputEndOfStream() unexpected");
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
  stream_->SetOobConfigPending(true);
}

void OmxCodecRunner::QueueInputPacket(fuchsia::mediacodec::CodecPacket packet) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    EnsureFutureStreamSeenLocked(packet.stream_lifetime_ordinal);
  }  // ~lock
  PostSerial(stream_control_dispatcher_,
             [this, packet = std::move(packet)]() mutable {
               QueueInputPacket_StreamControl(std::move(packet));
             });
}

void OmxCodecRunner::QueueInputPacket_StreamControl(
    fuchsia::mediacodec::CodecPacket packet) {
  // Unless we cancel this cleanup, we'll free the input packet back to the
  // client.
  //
  // This is an example of where not being able to copy a FIDL struct using
  // language-level copy can be a bit verbose, but overall it's probably worth
  // forcing copy to be explicit.
  fuchsia::mediacodec::CodecPacketHeader temp_header_copy;
  zx_status_t clone_result = packet.header.Clone(&temp_header_copy);
  if (clone_result != ZX_OK) {
    Exit("CodecPacketHeader::Clone() failed");
  }

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    auto send_free_input_packet_locked = fbl::MakeAutoCall(
        [this, header = std::move(temp_header_copy)]() mutable {
          SendFreeInputPacketLocked(std::move(header));
        });

    CheckOldBufferLifetimeOrdinalLocked(kInput,
                                        packet.header.buffer_lifetime_ordinal);

    // For input, mid-stream config changes are not a thing and input buffers
    // are never unilaterally de-configured by the Codec server.
    assert(buffer_lifetime_ordinal_[kInput] ==
           port_settings_[kInput]->buffer_lifetime_ordinal);
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
        port_settings_[kInput]->buffer_lifetime_ordinal) {
      Exit("client QueueInputPacket() with invalid buffer_lifetime_ordinal.");
    }

    CheckStreamLifetimeOrdinalLocked(packet.stream_lifetime_ordinal);
    assert(packet.stream_lifetime_ordinal >= stream_lifetime_ordinal_);

    if (packet.stream_lifetime_ordinal > stream_lifetime_ordinal_) {
      // This case implicitly starts a new stream.  If the client wanted to
      // ensure that the old stream would be fully processed, the client would
      // have sent FlushEndOfStreamAndCloseStream() previously, whose
      // processing (previous to reaching here) takes care of the flush.
      //
      // Start a new stream, synchronously.
      StartNewStream(lock, packet.stream_lifetime_ordinal);
    }
    assert(packet.stream_lifetime_ordinal == stream_lifetime_ordinal_);

    // Protocol check re. free/busy coherency.
    if (!packet_free_bits_[kInput][packet.header.packet_index]) {
      Exit("client QueueInputPacket() with packet_index !free - exiting\n");
    }
    packet_free_bits_[kInput][packet.header.packet_index] = false;

    if (stream_->input_end_of_stream()) {
      Exit("QueueInputPacket() after QueueInputEndOfStream() unexpeted");
    }

    if (stream_->future_discarded()) {
      // Don't queue to OMX.  The stream_ may have never fully started, or may
      // have been future-discarded since.  Either way, skip queueing to OMX.
      //
      // If the stream didn't fully start - as in, the client moved on to
      // another stream before fully configuring output, then OMX is not
      // presently in a state compatible with queueing input, but the Codec
      // interface is.  So in that case, we must avoid queueing to OMX for
      // correctness.
      //
      // If the stream was just future-discarded after fully starting, then this
      // is just an optimization to avoid giving OMX more work to do for a
      // stream the client has already discarded.
      //
      // ~send_free_input_packet_locked
      // ~lock
      return;
    }

    // Sending OnFreeInputPacket() will happen later instead, when OMX gives
    // back the packet.
    send_free_input_packet_locked.cancel();
  }  // ~lock

  if (stream_->oob_config_pending()) {
    OmxQueueInputOOB();
    stream_->SetOobConfigPending(false);
  }

  // We don't need to be under lock for this, because the fact that we're on the
  // StreamControl domain is enough to guarantee that any SendCommand to OMX
  // will start after this.
  OmxQueueInputPacket(packet);
}

void OmxCodecRunner::StartNewStream(std::unique_lock<std::mutex>& lock,
                                    uint64_t stream_lifetime_ordinal) {
  assert(thrd_current() == stream_control_thread_);
  assert((stream_lifetime_ordinal % 2 == 1) &&
         "new stream_lifetime_ordinal must be odd");

  EnsureStreamClosed(lock);
  assert((stream_lifetime_ordinal_ % 2 == 0) && "expecting no current stream");
  assert(!stream_);

  // Now it's time to start the new stream.  We start the new stream at
  // Codec layer first then OMX layer.

  if (!IsInputConfiguredLocked()) {
    Exit("input not configured before start of stream (QueueInputPacket())");
  }

  assert(stream_queue_.size() >= 1);
  assert(stream_lifetime_ordinal ==
         stream_queue_.front()->stream_lifetime_ordinal());
  stream_ = stream_queue_.front().get();
  // Update the stream_lifetime_ordinal_ to the new stream.  We need to do
  // this before we send new output config, since the output config will be
  // generated using the current stream ordinal.
  assert(stream_lifetime_ordinal > stream_lifetime_ordinal_);
  stream_lifetime_ordinal_ = stream_lifetime_ordinal;
  assert(stream_->stream_lifetime_ordinal() == stream_lifetime_ordinal_);

  // At this point, when driving an OMX codec, we need the output to be
  // configured to _something_, as OMX doesn't support giving us the real
  // output config unless the output is configured to at least something at
  // first.  If the client has not yet configured output, we also are
  // required to tell the client about the output config needed by this
  // stream in particular (at least the config needed by this stream in
  // particular at the start of this stream - there's no guarantee that this
  // stream will be able to continue without another output config change
  // before OMX emits any data).
  //
  // At this point we know we've never sent a config that's tagged with the
  // new stream yet.  Send that now, if output isn't already configured.

  if (!IsOutputConfiguredLocked() ||
      port_settings_[kOutput]->buffer_constraints_version_ordinal <=
          omx_meh_output_buffer_constraints_version_ordinal_) {
    StartIgnoringClientOldOutputConfigLocked();
    EnsureBuffersNotConfiguredLocked(kOutput);
    // This does count as a mid-stream output config change, even when this is
    // at the start of a stream - it's still while a stream is active, and still
    // prevents this stream from outputting any data to the Codec client until
    // the Codec client re-configures output while this stream is active.
    GenerateAndSendNewOutputConfig(lock, true);
  }

  // Now we can wait for the client to catch up to the current output config
  // or for the client to tell the server to discard the current stream.
  while (!stream_->future_discarded() && !IsOutputConfiguredLocked()) {
    wake_stream_control_.wait(lock);
  }

  if (stream_->future_discarded()) {
    return;
  }

  // Now we have both input and output configured, so we can move OMX from
  // OMX loaded state to OMX executing state.  This also calls or re-calls
  // FillThisBuffer() on any currently-free output packets.
  EnsureOmxStateExecuting(lock);
}

void OmxCodecRunner::EnsureStreamClosed(std::unique_lock<std::mutex>& lock) {
  // Move OMX codec to OMX loaded (from OMX executing), by using this thread to
  // directly drive the codec from executing down to loaded.  We do this first
  // so OMX won't try to send us output while we have no stream at the Codec
  // layer.
  //
  // We can de-init OMX codec here regardless of whether output buffers are yet
  // ready.  For some codecs we try to encourage the client to have the output
  // buffers be ready before a stream starts, but that's generally not required
  // by all codecs, and the client is not required to configure output before
  // feeding input.
  EnsureOmxStateLoaded(lock);

  // Now close the old stream at the Codec layer.
  EnsureCodecStreamClosedLockedInternal();

  assert((stream_lifetime_ordinal_ % 2 == 0) && "expecting no current stream");
  assert(!stream_);
}

void OmxCodecRunner::EnsureOmxStateLoaded(std::unique_lock<std::mutex>& lock) {
  assert(thrd_current() == stream_control_thread_);
  // We never leave the OMX codec in OMX_StateIdle, because the only way to
  // reset an OMX codec between streams is to drop all the way down to
  // OMX_StateLoaded.
  assert(omx_state_ == OMX_StateLoaded || omx_state_ == OMX_StateExecuting);
  assert(omx_state_desired_ == omx_state_);
  if (omx_state_ == OMX_StateLoaded) {
    // Already done
    return;
  }
  assert(omx_state_ == OMX_StateExecuting);

  is_omx_recycle_enabled_ = false;

  // Drop the codec from executing to idle, then from idle to loaded.

  OmxStartStateSetLocked(OMX_StateIdle);

  // (FillBufferDone() is ignoring the buffers returned thanks to
  // omx_state_desired_ set other than OMX_StateExecuting by
  // OmxStartStateSetLocked() above, and EmptyBufferDone() is sending
  // OnFreeInputPacket() like usual.)

  VLOGF("waiting for idle state...\n");
  OmxWaitForState(lock, OMX_StateExecuting, OMX_StateIdle);
  VLOGF("idle state reached\n");

  // The codec by this point will have "returned" all the buffers by calling
  // FillBufferDone() and/or EmptyBufferDone().  The buffers are still
  // allocated.  Unlike for port disable, we don't have to wait for this count
  // to reach zero ourselves, because OMX essentially has two steps to get from
  // OMX_StateExecuting to OMX_StateLoaded, but only one step to go from port
  // enabled to port disabled.  Only during port disable is this count reaching
  // zero used as a signalling mechanism.  If we wanted, we could pretend to
  // wait for this just above or below waiting to reach OMX_StateIdle, but there
  // would be no point.
  assert(omx_output_buffer_with_omx_count_ == 0);

  OmxStartStateSetLocked(OMX_StateLoaded);

  // We've started the state change from OMX_StateIdle to OMX_StateLoaded, but
  // for that state change to complete, we must call OMX FreeBuffer() on all the
  // OMX buffer headers.  We completely ignore the OMX spec where it says that
  // low-layer buffers need to be deallocated before calling FreeBuffer().
  // Instead we leave our low-layer buffers completely allocated and will
  // (potentially, if not reconfigured) use them again when moving from
  // OMX_StateLoaded to OMX_StateIdle in future.

  // We know input is not happening currently because we're on StreamControl
  // domain.  We know RecycleOutputPacket() is not actually recycling output
  // buffers back to OMX thanks to OmxTryRecycleOutputPacketLocked() checking
  // omx_state_desired_ != OMX_StateExecuting.

  // We don't deallocate Packet(s) here, we only deallocate all the OMX buffer
  // headers.
  OmxFreeAllBufferHeaders(lock);

  VLOGF("waiting for loaded state...\n");
  OmxWaitForState(lock, OMX_StateIdle, OMX_StateLoaded);
  VLOGF("loaded state reached\n");

  // Ensure output port is enabled, to get it back to same state as if we had
  // just loaded the codec.  This is effectively the end of cancelling a
  // mid-stream output config change.
  OMX_PARAM_PORTDEFINITIONTYPE output_port_def;
  InitOmxStruct(&output_port_def);
  output_port_def.nPortIndex = omx_port_index_[kOutput];
  OMX_ERRORTYPE omx_result = omx_component_->GetParameter(
      omx_component_, OMX_IndexParamPortDefinition, &output_port_def);
  if (omx_result != OMX_ErrorNone) {
    Exit(
        "Couldn't get port definition from OMX (during ensure output enable) - "
        "result: %d",
        omx_result);
  }
  if (!output_port_def.bEnabled) {
    OmxOutputStartSetEnabledLocked(true);
    // In this case we can immediately wait because we're in OMX_StateLoaded,
    // so nothing to do before waiting in this case.
    OmxWaitForOutputEnableStateChangeDone(lock);
  }

  // Reset OMX codec state tracking.
  omx_output_enabled_ = true;
  omx_output_enabled_desired_ = true;
  assert(omx_state_ == OMX_StateLoaded &&
         omx_state_desired_ == OMX_StateLoaded);
  assert(omx_output_enabled_ && omx_output_enabled_desired_);

  // The OMX codec, and our associated tracking state, is now reset.
}

void OmxCodecRunner::OmxOutputStartSetEnabledLocked(bool enable) {
  // We post because we always post all FillThisBuffer() and
  // SendCommand(), and because we want to call OMX only outside lock_.
  omx_output_enabled_desired_ = enable;
  PostSerial(fidl_dispatcher_, [this, enable] {
    OMX_ERRORTYPE omx_result = omx_component_->SendCommand(
        omx_component_, enable ? OMX_CommandPortEnable : OMX_CommandPortDisable,
        omx_port_index_[kOutput], nullptr);
    if (omx_result != OMX_ErrorNone) {
      Exit(
          "SendCommand(OMX_CommandPortEnable/OMX_CommandPortDisable) failed - "
          "exiting - enable: %d result: %d\n",
          enable, omx_result);
    }
  });
}

// packet is modified; packet is not stashed
void OmxCodecRunner::OmxFreeBufferHeader(std::unique_lock<std::mutex>& lock,
                                         Port port, Packet* packet) {
  OMX_BUFFERHEADERTYPE* header = packet->omx_header();
  packet->SetOmxHeader(nullptr);
  // We make all ScopedUnlock scopes stand out, even if a scope just goes to
  // the end of a method.
  {  // scope unlock
    ScopedUnlock unlock(lock);
    OMX_ERRORTYPE omx_result = omx_component_->FreeBuffer(
        omx_component_, omx_port_index_[port], header);
    if (omx_result != OMX_ErrorNone) {
      Exit("FreeBuffer() failed - exiting - port: %d\n", port);
    }
  }  // ~unlock
}

void OmxCodecRunner::OmxWaitForState(std::unique_lock<std::mutex>& lock,
                                     OMX_STATETYPE from_state,
                                     OMX_STATETYPE desired_state) {
  while (omx_state_ != omx_state_desired_) {
    if (omx_state_ != from_state && omx_state_ != desired_state) {
      // We went off the expected state transition rails.  This is treated as a
      // fatal error.  We don't expect this to happen.  We don't have any
      // reasonable way to handle this short of starting over with a new codec
      // process.
      Exit(
          "while waiting for state transition, went off expected state rails - "
          "from_state: %d desired_state: %d omx_state_: %d\n",
          from_state, desired_state, omx_state_);
    }
    omx_state_changed_.wait(lock);
  }
}

void OmxCodecRunner::OmxWaitForOutputEnableStateChangeDone(
    std::unique_lock<std::mutex>& lock) {
  while (omx_output_enabled_ != omx_output_enabled_desired_) {
    omx_output_enabled_changed_.wait(lock);
  }
}

void OmxCodecRunner::EnsureOmxStateExecuting(
    std::unique_lock<std::mutex>& lock) {
  assert(stream_control_thread_ == thrd_current());
  for (Port port = kFirstPort; port < kPortCount; port++) {
    // In contrast to Codec interface, OMX doesn't permit the output buffers to
    // be not yet configured when moving to OMX_StateExecuting, so the caller
    // takes care of ensuring that the client has configured output buffers.
    uint32_t packet_count = PacketCountFromPortSettings(*port_settings_[port]);
    (void)packet_count;
    assert(all_packets_[port].size() == packet_count);
  }
  assert(omx_input_buffer_oob_);
  assert(omx_input_packet_oob_);
  assert(omx_input_packet_eos_);
  if (omx_state_ == OMX_StateExecuting) {
    // TODO(dustingreen): We don't actually use this method this way currently.
    // If that stays true for much longer, rename and don't check for this case
    // (but still assert below).
    return;
  }
  assert(omx_state_ == OMX_StateLoaded);

  // First, make sure OMX has the proper buffer count, for each port.
  EnsureOmxBufferCountCurrent(lock);

  VLOGF("starting transition to OMX_StateIdle\n");
  OmxStartStateSetLocked(OMX_StateIdle);
  VLOGF("transition to idle started.\n");

  // Allocate an OMX_BUFFERHEADERTYPE for each packet in all_packets_, and one
  // for omx_input_packet_oob_ and one for omx_input_packet_eos_.
  for (Port port = kFirstPort; port < kPortCount; port++) {
    OmxPortUseBuffers(lock, port);
  }
  omx_input_packet_oob_->SetOmxHeader(
      OmxUseBuffer(lock, kInput, *omx_input_packet_oob_));
  omx_input_packet_eos_->SetOmxHeader(
      OmxUseBuffer(lock, kInput, *omx_input_packet_eos_));

  // We've told the codec about all the buffers, so the codec should transition
  // to idle soon if it isn't already.
  VLOGF("waiting for OMX_StateIdle...\n");
  OmxWaitForState(lock, OMX_StateLoaded, OMX_StateIdle);
  VLOGF("OMX_StateIdle reached\n");

  // Now that the codec is idle, we can immediately transition the codec to
  // executing.
  VLOGF("starting codec transition to executing state\n");
  OmxStartStateSetLocked(OMX_StateExecuting);
  VLOGF("transition to OMX_StateExecuting started\n");

  OmxWaitForState(lock, OMX_StateIdle, OMX_StateExecuting);
  VLOGF("done with transition to OMX_StateExecuting\n");

  // Tell the codec to fill all the output buffers that are free.  This is how
  // the non-action sometimes in OmxTryRecycleOutputPacketLocked() ends up
  // calling FillThisBuffer() at the appropriate time.  This is that time.
  //
  // If an output packet is not free, that that packet is still with the Codec
  // client, which can be entirely reasonable even for long periods of time if
  // the Codec client set a non-zero packet_count_for_client.
  //
  // The input buffers thankfully start with the OMX client so that's already
  // consistent with existing packet_free_bits_[kInput].
  for (auto& output_packet : all_packets_[kOutput]) {
    if (packet_free_bits_[kOutput][output_packet->packet_index()]) {
      OmxFillThisBufferLocked(output_packet->omx_header());
    }
  }
  is_omx_recycle_enabled_ = true;
}

// Make sure OMX has the current buffer count for each port.
//
// During mid-stream format change, this method relies on input config changes
// being prohibited with an active stream - that's how this method avoids
// telling OMX to change the input config with the input port presently enabled.
void OmxCodecRunner::EnsureOmxBufferCountCurrent(
    std::unique_lock<std::mutex>& lock) {
  assert(stream_control_thread_ == thrd_current());
  // This method isn't called at a time when input or output config can be
  // changing.  Since we call OMX in here, we force the caller to provide the
  // caller's unique_lock<> only so we can be more sure that we're not holding
  // that lock while calling OMX.
  //
  // TODO(dustingreen): Change to locks that are capable of asserting that the
  // current thread doesn't hold the lock, and switch to asserting here instead.
  ScopedUnlock unlock(lock);
  OMX_PARAM_PORTDEFINITIONTYPE port_definition[kPortCount];
  for (Port port = kFirstPort; port < kPortCount; port++) {
    OMX_PARAM_PORTDEFINITIONTYPE& port_def = port_definition[port];
    InitOmxStruct(&port_def);
    port_def.nPortIndex = omx_port_index_[port];
    OMX_ERRORTYPE omx_result = omx_component_->GetParameter(
        omx_component_, OMX_IndexParamPortDefinition, &port_def);
    if (omx_result != OMX_ErrorNone) {
      Exit(
          "Couldn't get port definition from OMX - exiting - port: %d result: "
          "%d\n",
          port, omx_result);
    }
    assert(port_def.nBufferCountActual >= port_def.nBufferCountMin);
    // We don't use the omx_input_packet_oob_ unless we're sending OOB data (and
    // similar for omx_input_packet_eos_ unless we're sending an EOS), so those
    // buffers don't really count for nBufferCountMin purposes.  As in, we
    // shouldn't expect the OMX codec to work properly unless there are as many
    // normal buffers as required by OMX, not counting the
    // kHiddenInputPacketCount.
    uint32_t packet_count = PacketCountFromPortSettings(*port_settings_[port]);
    assert(packet_count >= port_def.nBufferCountMin);
    uint32_t omx_buffer_count = packet_count;
    if (port == kInput) {
      // for omx_input_packet_oob_ and omx_input_packet_eos_
      omx_buffer_count += kHiddenInputPacketCount;
      assert(omx_buffer_count >=
             port_def.nBufferCountMin + kHiddenInputPacketCount);
    }
    if (port_def.nBufferCountActual != omx_buffer_count) {
      port_def.nBufferCountActual = omx_buffer_count;
      assert(port_def.nBufferCountActual >= port_def.nBufferCountMin);
      omx_result = omx_component_->SetParameter(
          omx_component_, OMX_IndexParamPortDefinition, &port_def);
      if (omx_result != OMX_ErrorNone) {
        Exit("SetParamter(port_definition) failed - exiting\n");
      }
    }
  }
}

void OmxCodecRunner::OmxPortUseBuffers(std::unique_lock<std::mutex>& lock,
                                       Port port) {
  assert(!all_packets_[port].empty());
  for (auto& packet : all_packets_[port]) {
    packet->SetOmxHeader(OmxUseBuffer(lock, port, *packet));
  }
}

OMX_BUFFERHEADERTYPE* OmxCodecRunner::OmxUseBuffer(
    std::unique_lock<std::mutex>& lock, Port port, const Packet& packet) {
  assert(!packet.omx_header());
  const Buffer& buffer = packet.buffer();
  size_t codec_buffer_size = buffer.buffer_size();
  // For input, we can report larger size to OMX than we'll actually use for
  // any delivered input buffer when our input packets are smaller than OMX
  // thinks they ought to be.
  //
  // For output, our codec packet buffers must be at least as large as what
  // we're telling OMX, since OMX is free to fill up to header->nAllocLen.
  const size_t omx_min_buffer_size = omx_port_def_[port].nBufferSize;
  size_t omx_buffer_size_raw = std::max(omx_min_buffer_size, codec_buffer_size);
  assert(omx_buffer_size_raw >= omx_min_buffer_size);
  if (omx_buffer_size_raw > std::numeric_limits<OMX_U32>::max()) {
    Exit("internal buffer size limit exceeded - exiting\n");
  }
  OMX_U32 omx_buffer_size = static_cast<OMX_U32>(omx_buffer_size_raw);
  if (port == kOutput) {
    // If codec_buffer_size is smaller, we won't have room for the amount of
    // output OMX might create.  If that happened somehow, when did OMX
    // unilaterally change nBufferSize to be larger without informing us?  It
    // shouldn't.
    //
    // The codec_buffer_size can't be larger than omx_buffer_size due to the
    // max above.  We do want OMX to be able to fill as much of each codec
    // packet as it wants.
    assert(codec_buffer_size == omx_buffer_size);
  }
  OMX_BUFFERHEADERTYPE* header = nullptr;
  {  // scope unlock
    ScopedUnlock unlock(lock);
    OMX_ERRORTYPE omx_result = omx_component_->UseBuffer(
        omx_component_, &header, omx_port_index_[port],
        reinterpret_cast<OMX_PTR>(const_cast<Packet*>(&packet)),
        omx_buffer_size, buffer.buffer_base());
    if (omx_result != OMX_ErrorNone) {
      Exit("UseBuffer() failed - exiting - port: %d\n", port);
    }
  }  // ~unlock
  return header;
}

void OmxCodecRunner::OmxStartStateSetLocked(OMX_STATETYPE omx_state_desired) {
  omx_state_desired_ = omx_state_desired;
  PostSerial(fidl_dispatcher_, [this, omx_state_desired] {
    OMX_ERRORTYPE omx_result = omx_component_->SendCommand(
        omx_component_, OMX_CommandStateSet, omx_state_desired, nullptr);
    if (omx_result != OMX_ErrorNone) {
      Exit("SendCommand(StateSet) failed - result: %d omx_state_desired: %d\n",
           omx_result, omx_state_desired);
    }
  });
}

void OmxCodecRunner::QueueInputEndOfStream(uint64_t stream_lifetime_ordinal) {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    EnsureFutureStreamSeenLocked(stream_lifetime_ordinal);
  }  // ~lock
  PostSerial(stream_control_dispatcher_, [this, stream_lifetime_ordinal] {
    QueueInputEndOfStream_StreamControl(stream_lifetime_ordinal);
  });
}

void OmxCodecRunner::QueueInputEndOfStream_StreamControl(
    uint64_t stream_lifetime_ordinal) {
  std::unique_lock<std::mutex> lock(lock_);
  CheckStreamLifetimeOrdinalLocked(stream_lifetime_ordinal);
  assert(stream_lifetime_ordinal >= stream_lifetime_ordinal_);
  if (stream_lifetime_ordinal > stream_lifetime_ordinal_) {
    // It might seem odd to start a new stream given an end-of-stream for a
    // stream we've not seen before, but in my experience, allowing empty things
    // to not be errors is better.
    StartNewStream(lock, stream_lifetime_ordinal);
  }

  if (stream_->future_discarded()) {
    // Don't queue to OMX.  The stream_ may have never fully started, or may
    // have been future-discarded since.  Either way, skip queueing to OMX. We
    // only really must do this because the stream may not have ever fully
    // started, in the case where the client moves on to a new stream before
    // catching up to latest output config.
    return;
  }

  // Convert to an input OMX packet with EOS set.  We have an extra OMX buffer
  // reserved for this purpose.
  OmxQueueInputEOS();
}

void OmxCodecRunner::OmxQueueInputPacket(
    const fuchsia::mediacodec::CodecPacket& packet) {
  assert(thrd_current() == stream_control_thread_);
  // The OMX codec can report an error unilaterally, but it can't change state
  // unilaterally.  So on the StreamControl ordering domain it's ok to check the
  // omx_state_ outside lock_.
  assert(omx_state_ == OMX_StateExecuting);
  // We only modify all_packets_[kInput] on StreamControl, so it's ok to read
  // from it outside lock_.
  if (!decoder_params_->promise_separate_access_units_on_input &&
      packet.timestamp_ish != 0) {
    Exit(
        "timestamp_ish must be 0 unless promise_separate_access_units_on_input "
        "- exiting\n");
  }
  OMX_BUFFERHEADERTYPE* header =
      all_packets_[kInput][packet.header.packet_index]->omx_header();
  header->nFilledLen = packet.valid_length_bytes;
  header->nOffset = 0;
  header->nTimeStamp = packet.timestamp_ish;
  header->nFlags = 0;
  OMX_ERRORTYPE omx_result =
      omx_component_->EmptyThisBuffer(omx_component_, header);
  if (omx_result != OMX_ErrorNone) {
    Exit("component_->EmptyThisBuffer() failed - exiting - omx_result: %d\n",
         omx_result);
  }
}

void OmxCodecRunner::OmxQueueInputOOB() {
  assert(thrd_current() == stream_control_thread_);
  assert(omx_state_ == OMX_StateExecuting);

  // Unlike for the omx_input_packet_eos_, there's no particular guarantee that
  // the OOB packet is actually free at this point, so wait for it to be free
  // first.  This relies on the InputData domain not being the same as the
  // StreamControl domain.
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    while (!omx_input_packet_oob_free_) {
      omx_input_packet_oob_free_condition_.wait(lock);
    }
  }  // ~lock

  // Whether codec_oob_bytes is needed can depend on codec type or specific
  // input format.  If there is no codec_oob_bytes, we won't queue any
  // OMX_BUFFERFLAG_CODECCONFIG buffer to OMX.
  //
  // TODO(dustingreen): For SoftAAC2 used in ADTS mode, extract OOB info from
  // the first input data instead of requiring a client to provide OOB info that
  // ultimately came from the first ADTS input data anyway...
  //
  // TODO(dustingreen): Consider enforcing whether a codec needs codec_oob_bytes
  // or whether it must not have codec_oob_bytes, rather than relying on OMX SW
  // codecs to fail in a reasonably sane way.  Cover the case of empty
  // codec_oob_bytes also.

  assert(initial_input_format_details_);
  const std::vector<uint8_t>* codec_oob_bytes = nullptr;
  if (stream_->input_format_details() &&
      stream_->input_format_details()->codec_oob_bytes) {
    codec_oob_bytes = &stream_->input_format_details()->codec_oob_bytes.get();
  } else if (initial_input_format_details_->codec_oob_bytes) {
    codec_oob_bytes = &initial_input_format_details_->codec_oob_bytes.get();
  }
  if (!codec_oob_bytes) {
    // This is potentially fine.  Let the OMX SW codec fail later if it wants to
    // based on lack of OOB data, or maybe this codec and/or format doesn't need
    // OOB data.
    printf("!codec_oob_bytes - potentially fine\n");
    return;
  }
  assert(codec_oob_bytes);
  if (codec_oob_bytes->empty()) {
    Exit("codec_oob_bytes was non-null but empty - exiting\n");
  }
  assert(omx_input_packet_oob_->buffer().buffer_size() >=
         fuchsia::mediacodec::kMaxCodecOobBytesSize);
  if (codec_oob_bytes->size() > fuchsia::mediacodec::kMaxCodecOobBytesSize) {
    Exit(
        "codec_oob_bytes.size() > fuchsia::mediacodec::kMaxCodecOobBytesSize - "
        "exiting\n");
  }
  assert(codec_oob_bytes->size() <=
         omx_input_packet_oob_->buffer().buffer_size());

  size_t copy_size = codec_oob_bytes->size();
  uint8_t* buffer_base = omx_input_packet_oob_->buffer().buffer_base();
  for (size_t i = 0; i < copy_size; i++) {
    buffer_base[i] = (*codec_oob_bytes)[i];
  }

  // This lock interval isn't strictly necessary, but to describe why it's not
  // necessary would require delving into happens-before relationships in OMX,
  // so go ahead and just grab the lock to assign false.  It's not worth having
  // different sync rules for how omx_input_packet_oob_free_ becomes true vs.
  // becomes false.  We can't do this assignment up above the state checks in
  // the previous lock hold interval, because that would potentially incorrectly
  // leave omx_input_packet_oob_free_ set to false in a case where we return
  // early and don't use omx_input_packet_oob_.
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    omx_input_packet_oob_free_ = false;
  }  // ~lock

  VLOGF("OmxQueueInputOOB() is queueing codec_oob_bytes to the OMX codec.\n");
  OMX_BUFFERHEADERTYPE* header = omx_input_packet_oob_->omx_header();
  header->nFlags = OMX_BUFFERFLAG_CODECCONFIG;
  header->nFilledLen = copy_size;
  header->nOffset = 0;
  header->nTimeStamp = 0;
  OMX_ERRORTYPE omx_result =
      omx_component_->EmptyThisBuffer(omx_component_, header);
  if (omx_result != OMX_ErrorNone) {
    Exit(
        "component_->EmptyThisBuffer() failed (OOB case) - exiting - "
        "omx_result: %d\n",
        omx_result);
  }
}

void OmxCodecRunner::OmxQueueInputEOS() {
  assert(thrd_current() == stream_control_thread_);
  assert(omx_state_ == OMX_StateExecuting);
  assert(omx_input_packet_eos_free_);
  omx_input_packet_eos_free_ = false;
  OMX_BUFFERHEADERTYPE* header = omx_input_packet_eos_->omx_header();
  header->nFlags = OMX_BUFFERFLAG_EOS;
  header->nFilledLen = 0;
  header->nOffset = 0;
  header->nTimeStamp = 0;
  OMX_ERRORTYPE omx_result =
      omx_component_->EmptyThisBuffer(omx_component_, header);
  if (omx_result != OMX_ErrorNone) {
    Exit(
        "component_->EmptyThisBuffer() failed (EOS case) - exiting - "
        "omx_result: %d\n",
        omx_result);
  }
}

bool OmxCodecRunner::IsInputConfiguredLocked() {
  return IsPortConfiguredCommonLocked(kInput);
}

bool OmxCodecRunner::IsOutputConfiguredLocked() {
  return IsPortConfiguredCommonLocked(kOutput);
}

bool OmxCodecRunner::IsPortConfiguredCommonLocked(Port port) {
  if (!port_settings_[port]) {
    return false;
  }
  assert(all_buffers_[port].size() <=
         BufferCountFromPortSettings(*port_settings_[port]));
  return all_buffers_[port].size() ==
         BufferCountFromPortSettings(*port_settings_[port]);
}

OMX_ERRORTYPE OmxCodecRunner::omx_EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                                               OMX_IN OMX_PTR pAppData,  // this
                                               OMX_IN OMX_EVENTTYPE eEvent,
                                               OMX_IN OMX_U32 nData1,
                                               OMX_IN OMX_U32 nData2,
                                               OMX_IN OMX_PTR pEventData) {
  VLOGF("omx_EventHandler eEvent: %d nData1: %d, nData2: %d pEventData: %p\n",
        eEvent, nData1, nData2, pEventData);
  fflush(nullptr);
  OmxCodecRunner* me = reinterpret_cast<OmxCodecRunner*>(pAppData);
  assert(me->omx_component_ == hComponent);
  return me->EventHandler(eEvent, nData1, nData2, pEventData);
}

OMX_ERRORTYPE OmxCodecRunner::omx_EmptyBufferDone(
    OMX_IN OMX_HANDLETYPE hComponent, OMX_IN OMX_PTR pAppData,
    OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) {
  OmxCodecRunner* me = reinterpret_cast<OmxCodecRunner*>(pAppData);
  assert(me->omx_component_ == hComponent);
  return me->EmptyBufferDone(pBuffer);
}

OMX_ERRORTYPE OmxCodecRunner::omx_FillBufferDone(
    OMX_IN OMX_HANDLETYPE hComponent, OMX_IN OMX_PTR pAppData,
    OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) {
  OmxCodecRunner* me = reinterpret_cast<OmxCodecRunner*>(pAppData);
  assert(me->omx_component_ == hComponent);
  return me->FillBufferDone(pBuffer);
}

OMX_ERRORTYPE OmxCodecRunner::EventHandler(OMX_IN OMX_EVENTTYPE eEvent,
                                           OMX_IN OMX_U32 nData1,
                                           OMX_IN OMX_U32 nData2,
                                           OMX_IN OMX_PTR pEventData) {
  // We intentionally don't acquire lock_ yet.  We postpone acquiring until the
  // more detailed handlers called after we've parsed all the fan-out in this
  // method.  The point of this is to allow more optimal notification of
  // condition variables while we're not presently holding the lock, without
  // resorting to queuing "todo" work up the stack via the lock holds, to be run
  // upon lock release, because we don't really need to go there to achieve the
  // goal.
  switch (eEvent) {
    case OMX_EventCmdComplete:
      // completed a command
      VLOGF("OMX_EventCmdComplete\n");
      switch (nData1) {
        case OMX_CommandStateSet:
          VLOGF("  OMX_CommandStateSet - state reached: %d\n", nData2);
          assert(pEventData == nullptr);
          onOmxStateSetComplete(static_cast<OMX_STATETYPE>(nData2));
          break;
        case OMX_CommandFlush:
          printf("  OMX_CommandFlush - port index: %d\n", nData2);
          assert(pEventData == nullptr);
          assert(false && "we nver send OMX_CommandFlush\n");
          break;
        case OMX_CommandPortDisable:
          VLOGF("  OMX_CommandPortDisable - port index: %d\n", nData2);
          assert(pEventData == nullptr);
          if (nData2 == omx_port_index_[kOutput]) {
            {  // scope lock
              std::unique_lock<std::mutex> lock(lock_);
              omx_output_enabled_ = false;
              assert(omx_output_enabled_ == omx_output_enabled_desired_);
            }
            omx_output_enabled_changed_.notify_all();
          }
          break;
        case OMX_CommandPortEnable:
          VLOGF("  OMX_CommandPortEnable - port index: %d\n", nData2);
          assert(pEventData == nullptr);
          if (nData2 == omx_port_index_[kOutput]) {
            {  // scope lock
              std::unique_lock<std::mutex> lock(lock_);
              omx_output_enabled_ = true;
              assert(omx_output_enabled_ == omx_output_enabled_desired_);
            }
            omx_output_enabled_changed_.notify_all();
          }
          break;
        case OMX_CommandMarkBuffer:
          printf("  OMX_CommandMarkBuffer - port index: %d\n", nData2);
          assert(pEventData == nullptr);
          assert(false && "we nver send OMX_CommandMarkBuffer\n");
          break;
      }
      break;
    case OMX_EventError:
      // detected an error condition
      {
        // OMX spec says nData2 and pEventData are 0, but apparently not
        // actually true...
        printf("OMX_EventError - error: %d, nData2: %d, pEventData: %p\n",
               nData1, nData2, pEventData);
        const char* error_string = nullptr;
        OMX_ERRORTYPE which_error = static_cast<OMX_ERRORTYPE>(nData1);
        // recoverable means recoverable by faling the stream, not recoverable
        // within a stream - there doesn't appear to be any way for AOSP OMX SW
        // codecs to report mid-stream errors (despite what the OMX spec says)
        bool recoverable = false;
        switch (which_error) {
          case OMX_ErrorNone:
            error_string = "OMX_ErrorNone";
            // Not recoverable, because delivering this would make no sense.
            break;
          case OMX_ErrorInsufficientResources:
            error_string = "OMX_ErrorInsufficientResources";
            // not recoverable because we don't use any OMX codecs where this
            // would make any sense
            break;
          case OMX_ErrorUndefined:
            error_string = "OMX_ErrorUndefined";
            // Some AOSP OMX SW codecs report recoverable errors this way.
            recoverable = true;
            break;
          case OMX_ErrorInvalidComponentName:
            error_string = "OMX_ErrorInvalidComponentName";
            break;
          case OMX_ErrorComponentNotFound:
            error_string = "OMX_ErrorComponentNotFound";
            break;
          case OMX_ErrorInvalidComponent:
            error_string = "OMX_ErrorInvalidComponent";
            break;
          case OMX_ErrorBadParameter:
            error_string = "OMX_ErrorBadParameter";
            break;
          case OMX_ErrorNotImplemented:
            error_string = "OMX_ErrorNotImplemented";
            break;
          case OMX_ErrorUnderflow:
            error_string = "OMX_ErrorUnderflow";
            break;
          case OMX_ErrorOverflow:
            error_string = "OMX_ErrorOverflow";
            break;
          case OMX_ErrorHardware:
            error_string = "OMX_ErrorHardware";
            break;
          case OMX_ErrorInvalidState:
            error_string = "OMX_ErrorInvalidState";
            break;
          case OMX_ErrorStreamCorrupt:
            error_string = "OMX_ErrorStreamCorrupt";
            // At least SoftAAC2.cpp can report a recoverable error this way in
            // ADTS mode.  Recoverable in the stream failure sense, not in the
            // continues to process normally sense that the OMX spec talks about
            // being "typical".  Not typical in practice with these codecs...
            recoverable = true;
            break;
          case OMX_ErrorPortsNotCompatible:
            error_string = "OMX_ErrorPortsNotCompatible";
            break;
          case OMX_ErrorResourcesLost:
            error_string = "OMX_ErrorResourcesLost";
            break;
          case OMX_ErrorNoMore:
            error_string = "OMX_ErrorNoMore";
            break;
          case OMX_ErrorVersionMismatch:
            error_string = "OMX_ErrorVersionMismatch";
            break;
          case OMX_ErrorNotReady:
            error_string = "OMX_ErrorNotReady";
            break;
          case OMX_ErrorTimeout:
            error_string = "OMX_ErrorTimeout";
            break;
          case OMX_ErrorSameState:
            error_string = "OMX_ErrorSameState";
            break;
          case OMX_ErrorResourcesPreempted:
            error_string = "OMX_ErrorResourcesPreempted";
            break;
          case OMX_ErrorPortUnresponsiveDuringAllocation:
            error_string = "OMX_ErrorPortUnresponsiveDuringAllocation";
            break;
          case OMX_ErrorPortUnresponsiveDuringDeallocation:
            error_string = "OMX_ErrorPortUnresponsiveDuringDeallocation";
            break;
          case OMX_ErrorPortUnresponsiveDuringStop:
            error_string = "OMX_ErrorPortUnresponsiveDuringStop";
            break;
          case OMX_ErrorIncorrectStateTransition:
            error_string = "OMX_ErrorIncorrectStateTransition";
            break;
          case OMX_ErrorIncorrectStateOperation:
            error_string = "OMX_ErrorIncorrectStateOperation";
            break;
          case OMX_ErrorUnsupportedSetting:
            error_string = "OMX_ErrorUnsupportedSetting";
            break;
          case OMX_ErrorUnsupportedIndex:
            error_string = "OMX_ErrorUnsupportedIndex";
            break;
          case OMX_ErrorBadPortIndex:
            error_string = "OMX_ErrorBadPortIndex";
            break;
          case OMX_ErrorPortUnpopulated:
            error_string = "OMX_ErrorPortUnpopulated";
            break;
          case OMX_ErrorComponentSuspended:
            error_string = "OMX_ErrorComponentSuspended";
            break;
          case OMX_ErrorDynamicResourcesUnavailable:
            error_string = "OMX_ErrorDynamicResourcesUnavailable";
            break;
          case OMX_ErrorMbErrorsInFrame:
            error_string = "OMX_ErrorMbErrorsInFrame";
            break;
          case OMX_ErrorFormatNotDetected:
            error_string = "OMX_ErrorFormatNotDetected";
            break;
          case OMX_ErrorContentPipeOpenFailed:
            error_string = "OMX_ErrorContentPipeOpenFailed";
            break;
          case OMX_ErrorContentPipeCreationFailed:
            error_string = "OMX_ErrorContentPipeCreationFailed";
            break;
          case OMX_ErrorSeperateTablesUsed:
            error_string = "OMX_ErrorSeperateTablesUsed";
            break;
          case OMX_ErrorTunnelingUnsupported:
            error_string = "OMX_ErrorTunnelingUnsupported";
            break;
          default:
            error_string = "UNRECOGNIZED ERROR";
        }
        printf("OMX_EventError error: %s\n", error_string);
        if (!recoverable) {
          Exit(
              "error is not known to be recoverable - exiting - error_string: "
              "%s\n",
              error_string);
        }
        assert(recoverable);
        // To recover, we need to get over to StreamControl domain, and we do
        // care whether the stream is the same stream as when this error was
        // delivered.  For this snap of the stream_lifetime_ordinal to be
        // meaningful we rely on the current thread to be the codec's processing
        // thread for all recoverable errors.
        //
        // TODO(dustingreen): See if we can find a good way to check that we're
        // on that thread, and if not, treat the error as not recoverable after
        // all.
        uint64_t stream_lifetime_ordinal;
        {  // scope lock
          std::unique_lock<std::mutex> lock(lock_);
          stream_lifetime_ordinal = stream_lifetime_ordinal_;
        }
        PostSerial(stream_control_dispatcher_, [this, stream_lifetime_ordinal] {
          onOmxStreamFailed(stream_lifetime_ordinal);
        });
      }
      break;
    case OMX_EventMark:
      // detected a buffer mark
      printf("OMX_EventMark\n");
      // Before anyone gets excited, OMX buffer marking doesn't actually do
      // anything in of the codecs we're interested in.
      assert(false && "we never mark buffers");
      break;
    case OMX_EventPortSettingsChanged: {
      // This is the fun one.

      // For input port, we rely on the fact that OMX SW codecs, driven the
      // way omx_codec_runner drives them, don't change the input port
      // definition's nBufferSize (because we don't drive that way) or
      // nBufferCountMin (because it probably doesn't change this field ever),
      // and also don't notify via this event even if they were to change the
      // input port definition (if we got out of sync on nBufferSize, that
      // would be unfortunate; the Codec protocol doesn't have any way to
      // force the Codec client to re-configure input, by design).
      assert(nData1 == kOutput);

      bool output_re_config_required =
          ((nData2 == 0) || (nData2 == OMX_IndexParamPortDefinition));
      VLOGF("OMX_EventPortSettingsChanged - output_re_config_required: %d\n",
            output_re_config_required);

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
        break;
      }

      // We have an OMX_EventPortSettingsChanged that does demand output
      // buffer re-config before more output data.
      assert(output_re_config_required);

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
        std::unique_lock<std::mutex> lock(lock_);
        // This part is not speculative.  OMX has indicated that it's at least
        // meh about the current output config, so ensure we do a required
        // OnOutputConfig() before the next stream starts, even if the client
        // moves on to a new stream such that the speculative part below becomes
        // stale.
        omx_meh_output_buffer_constraints_version_ordinal_ =
            port_settings_[kOutput]->buffer_constraints_version_ordinal;
        // Speculative part - this part is speculative, in that we don't know if
        // this post over to StreamControl will beat any client driving to a new
        // stream.  So we snap the stream_lifetime_ordinal so we know whether to
        // ignore the post once it reaches StreamControl.
        local_stream_lifetime_ordinal = stream_lifetime_ordinal_;
      }  // ~lock
      PostSerial(
          stream_control_dispatcher_,
          [this, stream_lifetime_ordinal = local_stream_lifetime_ordinal] {
            onOmxEventPortSettingsChanged(stream_lifetime_ordinal);
          });
    } break;
    case OMX_EventBufferFlag:
      // detected and EOS (end of stream)
      //
      // According to the EOS spec this is generated by a sink that doesn't
      // propagate anything downstream when the sink is done processing an EOS
      // that arrived at the sink.  None of the OMX SW codecs do this,
      // presumably because none of them are sinks.  If this were to arrive, it
      // would make no sense, so don't ignore.
      Exit("OMX_EventBufferFlag is unexpected");
      break;
    case OMX_EventResourcesAcquired:
      // component wanting to go to StateIdle
      //
      // None of the OMX SW codecs do this.
      Exit("OMX_EventResouresAcquired is unexpected");
      break;
    case OMX_EventComponentResumed:
      // due to reacquistion of resources
      //
      // None of the OMX SW codecs do this.
      Exit("OMX_EventComponentResumed is unexpected");
      break;
    case OMX_EventDynamicResourcesAvailable:
      // acquired previously unavailable dynamic resources
      //
      // None of the OMX SW codecs do this.
      Exit("OMX_EventDynamicResourcesAvailable is unexpected");
      break;
    case OMX_EventPortFormatDetected:
      // deteted a supported format
      //
      // None of the OMX SW codecs do this.
      Exit("OMX_EventPortFormatDetected is unexpected");
      break;
    default:
      // Despite getting annoyed for unexpected events above, we ignore any
      // events that we don't even recognize the number of.
      //
      // TODO(dustingreen): See if we hit any of these, and if not, consider
      // just failing here since ... we really don't expect these.
      Exit("OMX_Event unrecognized and ignored.");
      break;
  }
  return OMX_ErrorNone;
}

void OmxCodecRunner::onOmxEventPortSettingsChanged(
    uint64_t stream_lifetime_ordinal) {
  assert(thrd_current() == stream_control_thread_);
  std::unique_lock<std::mutex> lock(lock_);
  if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
    // ignore; The omx_meh_output_buffer_constraints_version_ordinal_ took care
    // of it.
    return;
  }
  assert(stream_lifetime_ordinal == stream_lifetime_ordinal_);

  is_omx_recycle_enabled_ = false;

  // Now we need to start disabling the port, wait for buffers to come back from
  // OMX, free buffer headers, wait for the port to become fully disabled,
  // unilaterally de-configure output buffers, demand a new output config from
  // the client, wait for the client to configure output (but be willing to bail
  // on waiting for the client if we notice future stream discard), re-enable
  // the output port, allocate headers, wait for the port to be fully enabled,
  // call FillThisBuffer() on the protocol-free buffers.

  // This is what starts the interval during which
  // OmxTryRecycleOutputPacketLocked() won't call OMX, and the interval during
  // which we'll ingore any in-progress client output config until the client
  // catches up.
  StartIgnoringClientOldOutputConfigLocked();

  // Tell the codec to disable its output port, because that's how OMX deals
  // with an output format change.
  OmxOutputStartSetEnabledLocked(false);
  // We can assert this because we still have lock_ and we've only posted the
  // disable so far.
  assert(omx_output_enabled_ && !omx_output_enabled_desired_);

  OmxWaitForOutputBuffersDoneReturning(lock);

  OmxFreeAllPortBufferHeaders(lock, kOutput);

  // State of omx_output_enabled_ in flux here (well, actually it's probably
  // already false based on how OMX just used this thread during FreeHeader()
  // just above to call back EventHandler(), but we don't assume that particular
  // behavior so we don't assert what omx_output_enabled_ is here.
  assert(!omx_output_enabled_desired_);
  OmxWaitForOutputEnableStateChangeDone(lock);
  assert(!omx_output_enabled_ && !omx_output_enabled_desired_);

  EnsureBuffersNotConfiguredLocked(kOutput);

  GenerateAndSendNewOutputConfig(lock, true);

  // Now we can wait for the client to catch up to the current output config
  // or for the client to tell the server to discard the current stream.
  while (!stream_->future_discarded() && !IsOutputConfiguredLocked()) {
    wake_stream_control_.wait(lock);
  }

  if (stream_->future_discarded()) {
    // We already know how to handle this case, and
    // omx_meh_output_buffer_constraints_version_ordinal_ is still set such that
    // the client will be forced to re-configure output buffers at the start of
    // the new stream.
    return;
  }

  // Ensure OMX has the latest buffer count (nBufferCountActual) for the output
  // port.
  //
  // This will only actually update the output port config.  The input port
  // config won't have changed since SetInputBufferSettings() with an active
  // stream is prohibited (and that is enforced elsewhere).
  EnsureOmxBufferCountCurrent(lock);

  // Re-enable output port.

  OmxOutputStartSetEnabledLocked(true);

  // allocate OMX headers for output
  OmxPortUseBuffers(lock, kOutput);

  OmxWaitForOutputEnableStateChangeDone(lock);

  // In this path, all output packets are free and with the Codec from a
  // protocol point of view (not under client control because we have yet to
  // deliver any packet under the new buffer_lifetime_ordinal).
  for (auto& output_packet : all_packets_[kOutput]) {
    assert(packet_free_bits_[kOutput][output_packet->packet_index()]);
    OmxFillThisBufferLocked(output_packet->omx_header());
  }
  is_omx_recycle_enabled_ = true;

  VLOGF("Done with mid-stream format change.\n");
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
// last_required_buffer_constraints_version_ordinal_[kOutput].
void OmxCodecRunner::StartIgnoringClientOldOutputConfigLocked() {
  // buffer_constraints_action_required true processing is only performed on the
  // StreamControl ordering domain (except during setup).
  assert(!is_setup_done_ || thrd_current() == stream_control_thread_);

  // The buffer_lifetime_ordinal_[kOutput] can be even on entry due to at least
  // two cases: 0, and when the client is switching streams repeatedly without
  // setting a new buffer_lifetime_ordinal_[kOutput].
  if (buffer_lifetime_ordinal_[kOutput] % 2 == 1) {
    assert(buffer_lifetime_ordinal_[kOutput] % 2 == 1);
    assert(buffer_lifetime_ordinal_[kOutput] ==
           port_settings_[kOutput]->buffer_lifetime_ordinal);
    buffer_lifetime_ordinal_[kOutput]++;
    assert(buffer_lifetime_ordinal_[kOutput] % 2 == 0);
    assert(buffer_lifetime_ordinal_[kOutput] ==
           port_settings_[kOutput]->buffer_lifetime_ordinal + 1);
  }

  // When buffer_constraints_action_required true, we can assert in
  // GenerateAndSendNewOutputConfig() that this value is still the
  // next_output_buffer_constraints_version_ordinal_ in that method.
  last_required_buffer_constraints_version_ordinal_[kOutput] =
      next_output_buffer_constraints_version_ordinal_;
}

void OmxCodecRunner::OmxFreeAllBufferHeaders(
    std::unique_lock<std::mutex>& lock) {
  for (Port port = kFirstPort; port < kPortCount; port++) {
    OmxFreeAllPortBufferHeaders(lock, port);
  }
  // And same for the omx_input_packet_oob_
  OmxFreeBufferHeader(lock, kInput, omx_input_packet_oob_.get());
  // And same for the omx_input_packet_eos_
  OmxFreeBufferHeader(lock, kInput, omx_input_packet_eos_.get());
}

void OmxCodecRunner::OmxFreeAllPortBufferHeaders(
    std::unique_lock<std::mutex>& lock, Port port) {
  for (auto& packet : all_packets_[port]) {
    OmxFreeBufferHeader(lock, port, packet.get());
  }
}

void OmxCodecRunner::OmxWaitForOutputBuffersDoneReturning(
    std::unique_lock<std::mutex>& lock) {
  // We only actaully call this when !omx_output_enabled_desired_, but there
  // wouldn't be any harm in calling it during move out of executing, so allow
  // that.
  assert(!omx_output_enabled_desired_ ||
         omx_state_desired_ != OMX_StateExecuting);
  while (omx_output_buffer_with_omx_count_) {
    omx_output_buffers_done_returning_condition_.wait(lock);
  }
}

void OmxCodecRunner::onOmxStreamFailed(uint64_t stream_lifetime_ordinal) {
  // When we come in here, we've just landed on the StreamControl domain, but
  // nothing has stopped the client from moving on to a new stream before we
  // got here.  Given how the relevant OMX codecs refuse to process any more
  // stream data of the stream when they fail with a "recoverable" error, it's
  // reasonable to just ignore any stale stream failures, since the stream
  // failure would only result in the client moving on to a new stream anyway,
  // so if that's already happened we can ignore the old stream failure.
  //
  // We prefer to check the state of things on the StreamControl domain since
  // this domain is in charge of stream transitions, so it's the easiest to
  // reason about why checking here is safe.  It would probably also be possible
  // to check robustly on the Output ordering domain and avoid creating any
  // invalid message orderings, but checking here is more obviously ok.
  assert(thrd_current() == stream_control_thread_);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    assert(stream_lifetime_ordinal <= stream_lifetime_ordinal_);
    if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
      // ignore - old stream is already gone, so OMX codec is already reset.  No
      // point in telling the client about the failure of an old stream.
      return;
    }
    assert(stream_lifetime_ordinal == stream_lifetime_ordinal_);
    // We're failing the current stream.  We should still queue to the output
    // ordering domain to ensure ordering vs. any previously-sent output on this
    // stream that was sent directly from codec processing thread.
    //
    // This failure is dispatcher, in the sense that the client may still be
    // sending input data, and the OMX codec is expected to not reject that
    // input data.
    //
    // There's not actually any need to track that the stream failed anywhere
    // in the OmxCodecRunner.  The client needs to move on from the failed
    // stream to a new stream, or close the Codec channel.
    printf("onOmxStreamFailed() - stream_lifetime_ordinal: %lu\n",
           stream_lifetime_ordinal);
    if (!enable_on_stream_failed_) {
      Exit(
          "onOmxStreamFailed() with a client that didn't send "
          "EnableOnOmxStreamFailed(), so closing the Codec channel instead.");
    }
    PostSerial(fidl_dispatcher_, [this, stream_lifetime_ordinal] {
      binding_->events().OnStreamFailed(stream_lifetime_ordinal);
    });
  }  // ~lock
}

// OMX is freeing an input packet.
//
// Called on InputData ordering domain.  A call from StreamControl would also
// work as long as the call into OMX that triggers this (if any) is made without
// lock_ held (which should be the case).  I don't think
// SimpleSoftOMXComponent.cpp will call EmptyBufferDone using an incoming
// thread, but OMX spec doesn't specify AFAICT.
OMX_ERRORTYPE OmxCodecRunner::EmptyBufferDone(
    OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) {
  Packet* packet = reinterpret_cast<Packet*>(pBuffer->pAppPrivate);
  assert(packet->omx_header() == pBuffer);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // We don't care if omx_state_desired_ is OMX_StateExecuting or not.  OMX
    // may be giving back a buffer which was actually "emptied", or may be
    // giving back a buffer that OMX will not be emptying because we're moving
    // from OMX executing to OMX idle.  Either way, the OMX input buffer is
    // free, the corresponding input packet is free, and the client should be
    // told about the free packet.

    // If the client did a CloseCurrentStream() with release_input_buffers true,
    // then the server is permitted to optimize away sending the free buffers
    // back to the client, but at the moment this server doesn't optimize that
    // away.

    // Because re-configuring input is only legal when there's no current
    // stream, and stopping a stream at OMX layer involves OMX giving back all
    // the OMX buffers (our packets) using this method first, this method can't
    // be called for a packet with mis-matched buffer_lifetime_ordinal.
    assert(packet->buffer_lifetime_ordinal() ==
           port_settings_[kInput]->buffer_lifetime_ordinal);
    assert(buffer_lifetime_ordinal_[kInput] ==
           port_settings_[kInput]->buffer_lifetime_ordinal);

    // If the free packet is the omx_input_packet_oob_, don't tell the client
    // about that packet/buffer, because it's not actually a packet at the Codec
    // interface layer.
    if (packet == omx_input_packet_oob_.get()) {
      // ok, it's free - this is likely to happen before the stream is closed,
      // but must happen by the time the stream is closing and the OMX codec
      // is moving from OMX executing to OMX idle.
      omx_input_packet_oob_free_ = true;
      // Don't tell the client about this packet; it's not an official
      // CodecPacket.
      goto oob_free_notify_outside_lock;
    }

    // omx_input_packet_eos_ is handled similarly to omx_input_packet_oob_.
    if (packet == omx_input_packet_eos_.get()) {
      omx_input_packet_eos_free_ = true;
      return OMX_ErrorNone;
    }

    // Free/busy coherency from Codec interface to OMX doesn't involve trusting
    // the client, so assert we're doing it right server-side.
    assert(!packet_free_bits_[kInput][packet->packet_index()]);
    packet_free_bits_[kInput][packet->packet_index()] = true;
    SendFreeInputPacketLocked(fuchsia::mediacodec::CodecPacketHeader{
        .buffer_lifetime_ordinal = packet->buffer_lifetime_ordinal(),
        .packet_index = packet->packet_index()});
  }  // ~lock
  return OMX_ErrorNone;
oob_free_notify_outside_lock:;
  omx_input_packet_oob_free_condition_.notify_all();
  return OMX_ErrorNone;
}

void OmxCodecRunner::SendFreeInputPacketLocked(
    fuchsia::mediacodec::CodecPacketHeader header) {
  // We allow calling this method on StreamControl or InputData ordering domain.
  // Because the InputData ordering domain thread isn't visible to this code,
  // if this isn't the StreamControl then we can only assert that this thread
  // isn't the FIDL thread, because we know the codec's InputData thread isn't
  // the FIDL thread.
  assert(thrd_current() == stream_control_thread_ ||
         thrd_current() != fidl_thread_);
  // We only send using the FIDL thread.
  PostSerial(fidl_dispatcher_, [this, header = std::move(header)] {
    binding_->events().OnFreeInputPacket(std::move(header));
  });
}

// OMX is either emitting some output data, or just handing us back an OMX
// buffer that OMX is done with.
OMX_ERRORTYPE OmxCodecRunner::FillBufferDone(
    OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer) {
  Packet* packet = reinterpret_cast<Packet*>(pBuffer->pAppPrivate);
  assert(packet->omx_header() == pBuffer);
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    assert(stream_);
    // We don't update packet_free_bits_[kOutput] for this, because the
    // packets aren't really free or busy during this - it's more that they're
    // not allocated.  Instead we use a count.
    omx_output_buffer_with_omx_count_--;
    if (!omx_output_enabled_desired_ ||
        omx_state_desired_ != OMX_StateExecuting) {
      VLOGF(
          "FillBufferDone() short circuit because OMX just returning the "
          "buffer\n");

      // OMX can be giving us an actual output buffer under this path, or can be
      // just trying to give us back a buffer without any data in it.
      //
      // We're not supposed to give this buffer back to OMX; we're trying to
      // disable the output port or move the OMX codec back to Loaded state.
      //
      // This is only able to be checked this way because we make sure that
      // calls to FillThisBuffer() always set the buffer to nFilledLen = 0
      // before sending the buffer to the codec.  In addition, we can only check
      // that nFilledLen == 0 if !omx_output_enabled_desired_, because only in
      // that case do we know that the OMX codec itself is yet aware of the fact
      // that we're returning output buffers without filling them, because
      // because only in this case did the OMX codec initiate the change.
      if (!omx_output_enabled_desired_ &&
          packet->omx_header()->nFilledLen != 0) {
        Exit(
            "OMX codec seems to be emitting a non-empty output buffer during "
            "mid-stream output config change");
      }

      // Only need to notify re. buffers done returning if we're trying to
      // disable port.  We also notify when moving out of executing state but
      // nobody actually cares about that notify since in that path we can just
      // wait to reach OMX_StateIdle instead.
      if (!omx_output_buffer_with_omx_count_) {
        // notify outside lock - none of the output buffers are with OMX - some
        // can still be with the client though.
        goto notify_buffers_done_returning_outside_lock;
      }
      return OMX_ErrorNone;
    }
    auto recycle_packet = fbl::MakeAutoCall([this, pBuffer] {
      // A non-EOS zero-length buffer is allowed by OMX spec AFAICT, but we
      // don't want to allow this in the Codec interface, so hand this buffer
      // back to OMX so OMX can try filling it again.
      //
      // Similarly, when we're converting from a zero-length EOS packet to
      // OnOutputEndOfStream(), the client never sees the packet, so hand the
      // buffer back to OMX.
      //
      // To avoid assuming it's safe to call OMX on this thread directly with
      // lock_ held (OMX spec basically implies it's not safe in general even
      // though it would be safe assuming SimpleSoftOMXComponent.cpp), we queue
      // the FillThisBuffer() instead.  We always queue SendCommand() the same
      // way, so we know that any SendCommand() that would change the OMX state
      // so that calling FillThisBuffer() is no longer valid will be called
      // after this FillThisBuffer() has already been called.
      //
      // We don't queue EmptyThisBuffer() from StreamControl to Output domain,
      // but that's ok because StreamControl always synchronously waits for
      // SendCommand() (which we do queue) to be done before StreamControl uses
      // EmptyThisBuffer() to queue more input to the OMX codec, and all input
      // is on StreamControl domain, so ordering is preserved between
      // EmptyThisBuffer() and SendCommand(), in both directions.
      printf("FillBufferDone() back to OMX without going to client\n");
      OmxFillThisBufferLocked(pBuffer);
    });
    // Because we already checked that both "desired" ones are set this way, and
    // because when moving to executing state or enabling the port, we don't
    assert(omx_state_ == OMX_StateExecuting &&
           omx_state_desired_ == OMX_StateExecuting && omx_output_enabled_ &&
           omx_output_enabled_desired_);
    // We don't want the Codec interface to send the client empty packets,
    // except for the empty end_of_stream packet after the last stream data, so
    // we take different action here depending on what OMX is handing us.
    //
    // If OMX is emitting an empty packet without EOS set, we want to send the
    // packet back in to OMX, but not on this thread.
    bool is_eos = ((pBuffer->nFlags & OMX_BUFFERFLAG_EOS) != 0);
    if (pBuffer->nFilledLen != 0) {
      // The output packet gets recycled later by the client.
      recycle_packet.cancel();
      uint64_t timestamp_ish = 0;
      if (decoder_params_->promise_separate_access_units_on_input) {
        timestamp_ish = pBuffer->nTimeStamp;
      }
      packet_free_bits_[kOutput][packet->packet_index()] = false;
      PostSerial(
          fidl_dispatcher_,
          [this, p = fuchsia::mediacodec::CodecPacket{
                     .header.buffer_lifetime_ordinal =
                         packet->buffer_lifetime_ordinal(),
                     .header.packet_index = packet->packet_index(),
                     .stream_lifetime_ordinal = stream_lifetime_ordinal_,
                     .start_offset = pBuffer->nOffset,
                     .valid_length_bytes = pBuffer->nFilledLen,
                     // TODO(dustingreen): verify whether other relevant codecs
                     // mess with this value - set to zero if codec wasn't
                     // created with promise_separate_access_units_on_input.
                     .timestamp_ish = timestamp_ish,
                     // TODO(dustingreen): Figure out what to do for other codec
                     // types here, especially encoders.  Might be able to be
                     // true always on output for OMX, hopefully.
                     .start_access_unit = decoder_params_ ? true : false,
                     .known_end_access_unit = decoder_params_ ? true : false,
                 }] {
            binding_->events().OnOutputPacket(std::move(p), false, false);
          });
    }
    if (is_eos) {
      VLOGF("sending OnOutputEndOfStream()\n");
      PostSerial(fidl_dispatcher_,
                 [this, stream_lifetime_ordinal = stream_lifetime_ordinal_] {
                   // OMX in AOSP in practice appears to have zero ways to
                   // report mid-stream failures that don't fail the whole
                   // stream.  I looked at both OMX_BUFFERFLAG_DATACORRUPT (OMX
                   // spec doesn't necessarily provide for this to be used on
                   // output buffers, and sparse if any usage by AOSP OMX
                   // codecs) and OMX_ErrorStreamCorrupt (OMX spec sounds
                   // somewhat promising if a bit wishy-washy, but in practice
                   // all the codecs stop processing the stream so it's
                   // effectively OnOmxStreamFailed()).  I see no other
                   // potential ways in OMX for an OMX codec to report
                   // non-stream-fatal errors.  I'm not sure to what degree
                   // various AOSP OMX codecs might silently tolerate corrupted
                   // input data.  Some AOSP OMX codecs seem to actively try to
                   // detect corrupted input data and fail the stream (as in,
                   // require moving the OMX codec to Loaded state to achieve a
                   // reset before any more data will get processed).  Those
                   // codecs do appear to report the problem via OMX_EventError
                   // with OMX_ErrorUndefined or OMX_ErrorStreamCorrupt, but
                   // refuse to process more input data until the codec goes
                   // through loaded state, so we treat those as
                   // OnOmxStreamFailed(), not error_detected_before.  We don't
                   // currently try to compensate for OMX codec behavior by
                   // tracking specific input data, re-queuing input data that
                   // had been queued to a failed OMX stream, hide the stream
                   // failure from the Codec interface, etc.
                   bool error_detected_before = false;
                   binding_->events().OnOutputEndOfStream(
                       stream_lifetime_ordinal, error_detected_before);
                 });
    }
    // ~recycle_packet may recycle if not already cancelled - this happens if
    // OMX outputs a zero-length buffer, whether EOS or not.
  }  // ~lock
  return OMX_ErrorNone;
notify_buffers_done_returning_outside_lock:;
  omx_output_buffers_done_returning_condition_.notify_all();
  return OMX_ErrorNone;
}

void OmxCodecRunner::OmxFillThisBufferLocked(OMX_BUFFERHEADERTYPE* header) {
  // This is the only reason we expect to see nFilledLen == 0 when disabling the
  // output port an getting buffers back from the codec via FillBufferDone()
  // callback.  It's also at least polite to the codec, and _maybe_ even
  // required by some - but no proof of that.
  header->nFilledLen = 0;
  // rest of these are paranoia
  header->nOffset = 0;
  header->nTimeStamp = 0;
  header->nFlags = 0;
  omx_output_buffer_with_omx_count_++;
  // Get out from under lock_ before calling OMX.  We need to queue to OMX under
  // the lock_ though, to ensure proper ordering with respect to SendCommand,
  // which is also always queued.  Since we also always queue SendCommand, the
  // header will remain valid long enough.  This is true for the same reason it
  // would be true if we were only talking about the queueing that already
  // exists internal to SimpleSoftOMXComponent.cpp.  We queue despite that
  // queueing because the OMX spec says the codec is allowed to call us back on
  // the same thread we call in on.
  PostSerial(fidl_dispatcher_, [this, header] {
    OMX_ERRORTYPE omx_result =
        omx_component_->FillThisBuffer(omx_component_, header);
    if (omx_result != OMX_ErrorNone) {
      Exit("FillThisBuffer() failed: %d", omx_result);
    }
  });
}

void OmxCodecRunner::onOmxStateSetComplete(OMX_STATETYPE state_reached) {
  if (state_reached != OMX_StateLoaded && state_reached != OMX_StateIdle &&
      state_reached != OMX_StateExecuting) {
    Exit(
        "onOmxStateSetComplete() state_reached unexpected - exiting - "
        "state_reached: %d\n",
        state_reached);
  }
  {
    std::unique_lock<std::mutex> lock(lock_);
    omx_state_ = state_reached;
  }
  omx_state_changed_.notify_all();
}

bool OmxCodecRunner::IsStreamActiveLocked() {
  return stream_lifetime_ordinal_ % 2 == 1;
}

void OmxCodecRunner::EnsureBuffersNotConfiguredLocked(Port port) {
  // This method can be called on input only if there's no current stream.
  //
  // On output, this method can be called if there's no current stream or if
  // we're in the middle of an ouput config change.
  //
  // On input, this can only be called on stream_control_thread_.
  //
  // On output, this can be called on stream_control_thread_ or output_thread_.

  assert(thrd_current() == stream_control_thread_ ||
         (port == kOutput && (thrd_current() == fidl_thread_)));
  assert(omx_state_ == omx_state_desired_);
  assert(omx_state_ == OMX_StateLoaded ||
         (OMX_StateExecuting && !omx_output_enabled_desired_ &&
          !omx_output_enabled_ && (port == kOutput)));
  // For mid-stream output config change, the caller is responsible for ensuring
  // that OMX headers have been freed first.
  assert(all_packets_[port].empty() || !all_packets_[port][0]->omx_header());
  all_packets_[port].resize(0);
  if (port == kInput) {
    omx_input_packet_oob_.reset(nullptr);
    omx_input_buffer_oob_.reset(nullptr);
    omx_input_packet_eos_.reset(nullptr);
  }
  all_buffers_[port].resize(0);
  packet_free_bits_[port].resize(0);
  assert(all_packets_[port].empty());
  assert(all_buffers_[port].empty());
  assert(packet_free_bits_[port].empty());
}

void OmxCodecRunner::CheckOldBufferLifetimeOrdinalLocked(
    Port port, uint64_t buffer_lifetime_ordinal) {
  // The client must only send odd values.  0 is even so we don't need a
  // separate check for that.
  if (buffer_lifetime_ordinal % 2 == 0) {
    Exit(
        "CheckOldBufferLifetimeOrdinalLocked() - buffer_lifetime_ordinal must "
        "be odd - exiting\n");
  }
  if (buffer_lifetime_ordinal > protocol_buffer_lifetime_ordinal_[port]) {
    Exit(
        "client sent new buffer_lifetime_ordinal in message type that doesn't "
        "allow new buffer_lifetime_ordinals");
  }
}

void OmxCodecRunner::CheckStreamLifetimeOrdinalLocked(
    uint64_t stream_lifetime_ordinal) {
  if (stream_lifetime_ordinal % 2 != 1) {
    Exit("stream_lifetime_ordinal must be odd.\n");
  }
  if (stream_lifetime_ordinal < stream_lifetime_ordinal_) {
    Exit("client sent stream_lifetime_ordinal that went backwards");
  }
}

void OmxCodecRunner::OmxTryRecycleOutputPacketLocked(
    OMX_BUFFERHEADERTYPE* header) {
  if (!is_omx_recycle_enabled_) {
    // We'll rely on packet_free_bits_ to track which packets need to be sent
    // back to OMX with FillThisBuffer() just after we've finished moving the
    // OMX codec back to a suitable state.
    return;
  }
  // We can assert all these things whenever is_omx_recycle_enabled_ is true.
  //
  // However, the reverse is not a valid statement, because we don't re-enable
  // is_omx_recycle_enabled_ until we're back under lock_ on StreamControl
  // ordering domain.  Specifically, this condition becomes true on an OMX
  // thread, followed by lock_ release, followed by lock_ acquire on
  // StreamControl, followed by sending any packet_free_bits_ true packets back
  // to OMX, followed by setting is_omx_recycle_enabled_ to true.
  assert(omx_state_ == OMX_StateExecuting &&
         omx_state_desired_ == OMX_StateExecuting && omx_output_enabled_ &&
         omx_output_enabled_desired_);
  // The caller only calls this method if the output buffers are configured at
  // codec level, and for now at least, configured at codec level == configured
  // at OMX level.
  assert(IsOutputConfiguredLocked());
  OmxFillThisBufferLocked(header);
}

fuchsia::mediacodec::AudioChannelId
OmxCodecRunner::AudioChannelIdFromOmxAudioChannelType(
    OMX_AUDIO_CHANNELTYPE omx_audio_channeltype) {
  uint32_t input_channeltype = omx_audio_channeltype;
  if (input_channeltype > kOmxAudioChannelTypeSupportedMax ||
      input_channeltype < kOmxAudioChannelTypeSupportedMin) {
    Exit("unsuppored OMX_AUDIO_CHANNELTYPE - exiting - value: %d\n",
         omx_audio_channeltype);
  }
  return kOmxAudioChannelTypeToAudioChannelId[input_channeltype];
}

void OmxCodecRunner::ValidateBufferSettingsVsConstraints(
    Port port, const fuchsia::mediacodec::CodecPortBufferSettings& settings,
    const fuchsia::mediacodec::CodecBufferConstraints& constraints) {
  if (settings.packet_count_for_codec <
      constraints.packet_count_for_codec_min) {
    Exit("packet_count_for_codec < packet_count_for_codec_min");
  }
  if (settings.packet_count_for_codec >
      constraints.packet_count_for_codec_max) {
    Exit("packet_count_for_codec > packet_count_for_codec_max");
  }
  if (settings.packet_count_for_client >
      constraints.packet_count_for_client_max) {
    Exit("packet_count_for_client > packet_count_for_client_max");
  }
  if (settings.per_packet_buffer_bytes <
      constraints.per_packet_buffer_bytes_min) {
    Exit(
        "settings.per_packet_buffer_bytes < "
        "constraints.per_packet_buffer_bytes_min - exiting - port: %u "
        "settings: %u constraint: %u",
        port, settings.per_packet_buffer_bytes,
        constraints.per_packet_buffer_bytes_min);
  }
  if (settings.per_packet_buffer_bytes >
      constraints.per_packet_buffer_bytes_max) {
    Exit(
        "settings.per_packet_buffer_bytes > "
        "constraints.per_packet_buffer_bytes_max");
  }
  if (settings.single_buffer_mode && !constraints.single_buffer_mode_allowed) {
    Exit(
        "settings.single_buffer_mode && "
        "!constraints.single_buffer_mode_allowed");
  }
}

void OmxCodecRunner::PostSerial(async_dispatcher_t* dispatcher,
                                fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  if (post_result != ZX_OK) {
    Exit("async::PostTask() failed - post_result %d", post_result);
  }
}

OmxCodecRunner::Buffer::Buffer(OmxCodecRunner* parent, Port port,
                               fuchsia::mediacodec::CodecBuffer buffer)
    : parent_(parent), port_(port), buffer_(std::move(buffer)) {
  // nothing else to do here
}

OmxCodecRunner::Buffer::~Buffer() {
  if (buffer_base_) {
    zx_status_t res = zx::vmar::root_self()->unmap(
        reinterpret_cast<uintptr_t>(buffer_base()), buffer_size());
    if (res != ZX_OK) {
      parent_->Exit(
          "OmxCodecRunner::Buffer::~Buffer() failed to unmap() Buffer");
    }
    buffer_base_ = nullptr;
  }
}

bool OmxCodecRunner::Buffer::Init(bool input_require_write) {
  assert(!input_require_write || port_ == kInput);
  // Map the VMO in the local address space.
  uintptr_t tmp;
  uint32_t flags = ZX_VM_FLAG_PERM_READ;
  if (port_ == kOutput || input_require_write) {
    flags |= ZX_VM_FLAG_PERM_WRITE;
  }
  zx_status_t res = zx::vmar::root_self()->map(
      0, buffer_.data.vmo().vmo_handle, buffer_.data.vmo().vmo_usable_start,
      buffer_.data.vmo().vmo_usable_size, flags, &tmp);
  if (res != ZX_OK) {
    printf("Failed to map %zu byte buffer vmo (res %d)\n",
           buffer_.data.vmo().vmo_usable_size, res);
    return false;
  }
  buffer_base_ = reinterpret_cast<uint8_t*>(tmp);
  return true;
}

uint64_t OmxCodecRunner::Buffer::buffer_lifetime_ordinal() const {
  return buffer_.buffer_lifetime_ordinal;
}

uint32_t OmxCodecRunner::Buffer::buffer_index() const {
  return buffer_.buffer_index;
}

uint8_t* OmxCodecRunner::Buffer::buffer_base() const {
  assert(buffer_base_ && "Shouldn't be using if Init() didn't work.");
  return buffer_base_;
}

size_t OmxCodecRunner::Buffer::buffer_size() const {
  return buffer_.data.vmo().vmo_usable_size;
}

OmxCodecRunner::Packet::Packet(uint64_t buffer_lifetime_ordinal,
                               uint32_t packet_index, Buffer* buffer)
    : buffer_lifetime_ordinal_(buffer_lifetime_ordinal),
      packet_index_(packet_index),
      buffer_(buffer) {
  // nothing else to do here
}

uint64_t OmxCodecRunner::Packet::buffer_lifetime_ordinal() const {
  return buffer_lifetime_ordinal_;
}

uint32_t OmxCodecRunner::Packet::packet_index() const { return packet_index_; }

const OmxCodecRunner::Buffer& OmxCodecRunner::Packet::buffer() const {
  return *buffer_;
}

// This can be called more than once, but must always either be moving from
// nullptr to non-nullptr, or from non-nullptr to nullptr.  This pointer is
// not owned T lifetime of the omx_header pointer.
void OmxCodecRunner::Packet::SetOmxHeader(OMX_BUFFERHEADERTYPE* omx_header) {
  omx_header_ = omx_header;
}

OMX_BUFFERHEADERTYPE* OmxCodecRunner::Packet::omx_header() const {
  return omx_header_;
}

OmxCodecRunner::Stream::Stream(uint64_t stream_lifetime_ordinal)
    : stream_lifetime_ordinal_(stream_lifetime_ordinal) {
  // nothing else to do here
}

uint64_t OmxCodecRunner::Stream::stream_lifetime_ordinal() {
  return stream_lifetime_ordinal_;
}

void OmxCodecRunner::Stream::SetFutureDiscarded() {
  assert(!future_discarded_);
  future_discarded_ = true;
}

bool OmxCodecRunner::Stream::future_discarded() { return future_discarded_; }

void OmxCodecRunner::Stream::SetFutureFlushEndOfStream() {
  assert(!future_flush_end_of_stream_);
  future_flush_end_of_stream_ = true;
}

bool OmxCodecRunner::Stream::future_flush_end_of_stream() {
  return future_flush_end_of_stream_;
}

OmxCodecRunner::Stream::~Stream() {
  VLOGF("~Stream() stream_lifetime_ordinal: %lu\n", stream_lifetime_ordinal_);
}

void OmxCodecRunner::Stream::SetInputFormatDetails(
    std::unique_ptr<fuchsia::mediacodec::CodecFormatDetails>
        input_format_details) {
  // This is allowed to happen multiple times per stream.
  input_format_details_ = std::move(input_format_details);
}

const fuchsia::mediacodec::CodecFormatDetails*
OmxCodecRunner::Stream::input_format_details() {
  return input_format_details_.get();
}

void OmxCodecRunner::Stream::SetOobConfigPending(bool pending) {
  // SetOobConfigPending(true) is legal regardless of current state, but
  // SetOobConfigPending(false) is only legal if the state is currently true.
  assert(pending || oob_config_pending_);
  oob_config_pending_ = pending;
}

bool OmxCodecRunner::Stream::oob_config_pending() {
  return oob_config_pending_;
}

void OmxCodecRunner::Stream::SetInputEndOfStream() {
  assert(!input_end_of_stream_);
  input_end_of_stream_ = true;
}

bool OmxCodecRunner::Stream::input_end_of_stream() {
  return input_end_of_stream_;
}

void OmxCodecRunner::Stream::SetOutputEndOfStream() {
  assert(!output_end_of_stream_);
  output_end_of_stream_ = true;
}

bool OmxCodecRunner::Stream::output_end_of_stream() {
  return output_end_of_stream_;
}

}  // namespace codec_runner
