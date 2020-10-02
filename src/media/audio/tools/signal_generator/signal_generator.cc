// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/tools/signal_generator/signal_generator.h"

#include <fuchsia/ultrasound/cpp/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/clock.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>

#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/cli.h"

namespace media::tools {

constexpr auto kPlayStartupDelay = zx::msec(0);

const char* SampleFormatToString(const fuchsia::media::AudioSampleFormat& format) {
  switch (format) {
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return "float";
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return "int24";
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return "int16";
    default:
      return "(unknown)";
  }
}

fbl::String RefTimeStrFromZxTime(zx::time zx_time) {
  auto time = zx_time.get();

  if (time == fuchsia::media::NO_TIMESTAMP) {
    return fbl::String("  [NO_TIMESTAMP]   ");
  }
  return fbl::StringPrintf("%07lu'%03lu'%03lu'%03lu", time / ZX_SEC(1),
                           (time % ZX_SEC(1)) / ZX_MSEC(1), (time % ZX_MSEC(1)) / ZX_USEC(1),
                           time % ZX_USEC(1));
}

fbl::String RefTimeMsStrFromZxTime(zx::time zx_time) {
  auto time = zx_time.get();

  if (time == fuchsia::media::NO_TIMESTAMP) {
    return fbl::String("[NO_TIMESTAMP]   ");
  }
  return fbl::StringPrintf("%07lu'%03lu.%02lu ms", time / ZX_SEC(1),
                           (time % ZX_SEC(1)) / ZX_MSEC(1), (time % ZX_MSEC(1)) / ZX_USEC(10));
}

MediaApp::MediaApp(fit::closure quit_callback) : quit_callback_(std::move(quit_callback)) {
  CLI_CHECK(quit_callback_, "quit_callback must not be null");
}

// Prepare for playback, submit initial data, start the presentation timeline.
void MediaApp::Run(sys::ComponentContext* app_context) {
  // Check the cmdline flags; exit if any are invalid or out-of-range.
  ParameterRangeChecks();

  SetAudioCoreSettings(app_context);
  AcquireAudioRenderer(app_context);

  // Calculate the frame size, number of packets, and shared-buffer size.
  SetupPayloadCoefficients();

  // Show a summary of all our settings: exactly what we are about to do.
  DisplayConfigurationSettings();

  // If requested, configure a WavWriter that will concurrently write this signal to a WAV file.
  InitializeWavWriter();

  // Create VmoMapper(s) that Create+Map a VMO. Send these down via AudioRenderer::AddPayloadBuffer.
  CreateMemoryMapping();

  // Retrieve the default reference clock for this renderer; once a device is ready, start playback.
  GetClockAndStart();
}

void MediaApp::ParameterRangeChecks() {
  bool success = true;

  if (num_channels_ < fuchsia::media::MIN_PCM_CHANNEL_COUNT) {
    std::cerr << "Number of channels must be at least " << fuchsia::media::MIN_PCM_CHANNEL_COUNT
              << std::endl;
    success = false;
  }
  if (num_channels_ > fuchsia::media::MAX_PCM_CHANNEL_COUNT) {
    std::cerr << "Number of channels must be no greater than "
              << fuchsia::media::MAX_PCM_CHANNEL_COUNT << std::endl;
    success = false;
  }

  if (frame_rate_ < fuchsia::media::MIN_PCM_FRAMES_PER_SECOND) {
    std::cerr << "Frame rate must be at least " << fuchsia::media::MIN_PCM_FRAMES_PER_SECOND
              << std::endl;
    success = false;
  }
  if (frame_rate_ > fuchsia::media::MAX_PCM_FRAMES_PER_SECOND) {
    std::cerr << "Frame rate must be no greater than " << fuchsia::media::MAX_PCM_FRAMES_PER_SECOND
              << std::endl;
    success = false;
  }

  if (frequency_ < 0.0) {
    std::cerr << "Frequency cannot be negative" << std::endl;
    success = false;
  }

  if (amplitude_ > 1.0) {
    std::cerr << "Amplitude must be no greater than 1.0" << std::endl;
    success = false;
  }
  if (amplitude_ < -1.0) {
    std::cerr << "Amplitude must be no less than -1.0" << std::endl;
    success = false;
  }

  if (duration_secs_ < 0.0) {
    std::cerr << "Duration cannot be negative" << std::endl;
    success = false;
  }

  if (frames_per_packet_ > (num_payload_buffers_ * frames_per_payload_buffer_ / 2) &&
      frames_per_packet_ < num_payload_buffers_ * frames_per_payload_buffer_) {
    std::cerr << "Packet size cannot be larger than half the total payload space" << std::endl;
    success = false;
  }
  if (frames_per_packet_ < frame_rate_ / 1000) {
    std::cerr << "Packet size must be 1 millisecond or more" << std::endl;
    success = false;
  }

  if (clock_rate_adjustment_) {
    if (clock_type_ != ClockType::Monotonic) {
      clock_type_ = ClockType::Custom;
    }

    if (clock_rate_adjustment_.value() > ZX_CLOCK_UPDATE_MAX_RATE_ADJUST) {
      std::cerr << "Clock adjustment must be " << ZX_CLOCK_UPDATE_MAX_RATE_ADJUST
                << " parts-per-million or less" << std::endl;
      success = false;
    }
    if (clock_rate_adjustment_.value() < ZX_CLOCK_UPDATE_MIN_RATE_ADJUST) {
      std::cerr << "Clock rate adjustment must be " << ZX_CLOCK_UPDATE_MIN_RATE_ADJUST
                << " parts-per-million or more" << std::endl;
      success = false;
    }
  }

  if (stream_gain_db_) {
    stream_gain_db_ =
        std::clamp<float>(stream_gain_db_.value(), fuchsia::media::audio::MUTED_GAIN_DB,
                          fuchsia::media::audio::MAX_GAIN_DB);
  }

  if (usage_gain_db_) {
    usage_gain_db_ = std::clamp<float>(usage_gain_db_.value(), fuchsia::media::audio::MUTED_GAIN_DB,
                                       kUnityGainDb);
  }
  if (usage_volume_) {
    usage_volume_ = std::clamp<float>(usage_volume_.value(), fuchsia::media::audio::MIN_VOLUME,
                                      fuchsia::media::audio::MAX_VOLUME);
  }

  CLI_CHECK(success, "Exiting.");
}

// Based on the user-specified values for signal frequency and milliseconds per payload, calculate
// the other related coefficients needed for our mapped memory section, and for our series of
// payloads that reference that section.
//
// We share a memory section with our AudioRenderer, divided into equally-sized payloads (size
// specified by the user). For now, we trim the end of the memory section, rather than handle the
// occasional irregularly-sized packet.
// TODO(mpuryear): handle end-of-buffer wraparound; make it a true ring buffer.
void MediaApp::SetupPayloadCoefficients() {
  total_frames_to_send_ = duration_secs_ * frame_rate_;
  num_packets_to_send_ = total_frames_to_send_ / frames_per_packet_;
  if (num_packets_to_send_ * frames_per_packet_ < total_frames_to_send_) {
    ++num_packets_to_send_;
  }

  // Number of frames in each period of the recurring signal.
  frames_per_period_ = frame_rate_ / frequency_;

  amplitude_scalar_ = amplitude_;
  switch (sample_format_) {
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      amplitude_scalar_ *= (std::numeric_limits<int32_t>::max() & 0xFFFFFF00);
      sample_size_ = sizeof(int32_t);
      break;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      amplitude_scalar_ *= std::numeric_limits<int16_t>::max();
      sample_size_ = sizeof(int16_t);
      break;
    case fuchsia::media::AudioSampleFormat::FLOAT:
      sample_size_ = sizeof(float);
      break;
    default:
      printf("Unknown AudioSampleFormat: %u\n", sample_format_);
      Shutdown();
      return;
  }

  // As mentioned above, for 24-bit audio we use 32-bit samples (low byte 0).
  frame_size_ = num_channels_ * sample_size_;

  bytes_per_packet_ = frames_per_packet_ * frame_size_;

  // From the specified size|number of payload buffers, determine how many packets fit, then trim
  // the mapping to what will be used. This size will be split across |num_payload_buffers_|
  // buffers, e.g. 2 buffers of 48000 frames each will be large enough hold 200 480-frame packets.
  auto total_payload_buffer_space = num_payload_buffers_ * frames_per_payload_buffer_ * frame_size_;
  total_mappable_packets_ = total_payload_buffer_space / bytes_per_packet_;

  // Shard out the payloads across multiple buffers, ensuring we can hold at least 1 buffer.
  packets_per_payload_buffer_ = std::max(1u, total_mappable_packets_ / num_payload_buffers_);
}

void MediaApp::DisplayConfigurationSettings() {
  auto it = std::find_if(kRenderUsageOptions.cbegin(), kRenderUsageOptions.cend(),
                         [usage = usage_](auto usage_string_and_usage) {
                           return usage == usage_string_and_usage.second;
                         });
  CLI_CHECK(it != kRenderUsageOptions.cend(), "no RenderUsage found");
  auto usage_str = it->first;

  printf("\nAudioRenderer configured for %d-channel %s at %u Hz with the %s usage.", num_channels_,
         SampleFormatToString(sample_format_), frame_rate_, usage_str);

  printf("\nContent is ");
  if (output_signal_type_ == kOutputTypeNoise) {
    printf("white noise");
  } else if (output_signal_type_ == kOutputTypePinkNoise) {
    printf("pink noise");
  } else {
    printf("a %.3f Hz ", frequency_);
    if (output_signal_type_ == kOutputTypeSquare) {
      printf("square wave");
    } else if (output_signal_type_ == kOutputTypeSine) {
      printf("sine wave");
    } else if (output_signal_type_ == kOutputTypeSawtooth) {
      printf("rising sawtooth wave");
    } else if (output_signal_type_ == kOutputTypeTriangle) {
      printf("isosceles triangle wave");
    }
  }
  printf(" with amplitude %.4f", amplitude_);

  if (ramp_target_gain_db_) {
    printf(",\nramping stream gain from %.3f dB to %.3f dB over %.6lf seconds (%ld nanoseconds)",
           stream_gain_db_.value(), ramp_target_gain_db_.value(),
           static_cast<double>(ramp_duration_nsec_) / 1000000000, ramp_duration_nsec_);
  } else if (stream_gain_db_) {
    printf(",\nsetting stream gain to %.3f dB", stream_gain_db_.value());
  }
  if (stream_mute_) {
    printf(",\n after explicitly %s this stream", stream_mute_.value() ? "muting" : "unmuting");
  }

  if (usage_gain_db_ || usage_volume_) {
    printf(",\nafter setting ");
    if (usage_gain_db_) {
      printf("%s gain to %.3f dB%s", usage_str, usage_gain_db_.value(),
             (usage_volume_ ? " and " : ""));
    }
    if (usage_volume_) {
      printf("%s volume to %.1f", usage_str, usage_volume_.value());
    }
  }

  printf(".\nThe generated signal will play for %.3f seconds", duration_secs_);

  if (file_name_) {
    printf(" and will be saved to '%s'", file_name_.value().c_str());
  }

  printf(".\nThe stream's reference clock will be ");
  switch (clock_type_) {
    case ClockType::Default:
      printf("the default clock");
      break;
    case ClockType::Flexible:
      printf("the AudioCore-provided 'flexible' clock");
      break;
    case ClockType::Monotonic:
      printf("a clone of the MONOTONIC clock");
      if (clock_rate_adjustment_) {
        printf(", rate-adjusted by %i ppm", clock_rate_adjustment_.value());
      }
      break;
    case ClockType::Custom:
      printf("a custom clock");
      if (clock_rate_adjustment_) {
        printf(", rate-adjusted by %i ppm", clock_rate_adjustment_.value());
      }
      break;
  }

  printf(".\nThe renderer will transport data using %u %stimestamped buffer sections of %u frames",
         total_mappable_packets_, (timestamp_packets_ ? "" : "non-"), frames_per_packet_);

  if (pts_continuity_threshold_secs_) {
    printf(",\nhaving set the PTS continuity threshold to %f seconds",
           pts_continuity_threshold_secs_.value());
  }

  if (online_) {
    printf(",\nusing strict timing for flow control (online mode)");
  } else {
    printf(",\nusing previous packet completions for flow control (contiguous mode)");
  }

  printf(".\n\n");
}

// AudioCore interface is used to change the gain/volume of usages.
void MediaApp::SetAudioCoreSettings(sys::ComponentContext* app_context) {
  if (usage_gain_db_ || usage_volume_) {
    fuchsia::media::AudioCorePtr audio_core;
    app_context->svc()->Connect(audio_core.NewRequest());

    if (usage_gain_db_) {
      audio_core->SetRenderUsageGain(usage_, usage_gain_db_.value());
    }

    if (usage_volume_) {
      fuchsia::media::Usage usage;
      usage.set_render_usage(usage_);
      audio_core->BindUsageVolumeControl(std::move(usage), usage_volume_control_.NewRequest());

      usage_volume_control_.set_error_handler([this](zx_status_t status) {
        CLI_CHECK(Shutdown(),
                  "Client connection to fuchsia.media.audio.VolumeControl failed: " << status);
      });
    }

    // ... now just let the instance of audio_core go out of scope.
  }
}

// Use ComponentContext to acquire AudioPtr; use that to acquire AudioRendererPtr in turn. Set
// AudioRenderer error handler, in case of channel closure.
void MediaApp::AcquireAudioRenderer(sys::ComponentContext* app_context) {
  if (ultrasound_) {
    fuchsia::ultrasound::FactorySyncPtr ultrasound_factory;
    app_context->svc()->Connect(ultrasound_factory.NewRequest());

    zx::clock reference_clock;
    fuchsia::media::AudioStreamType stream_type;
    ultrasound_factory->CreateRenderer(audio_renderer_.NewRequest(), &reference_clock,
                                       &stream_type);
    frame_rate_ = stream_type.frames_per_second;
    num_channels_ = stream_type.channels;
    sample_format_ = stream_type.sample_format;
  } else {
    // Audio interface is needed to create AudioRenderer and set routing policy.
    fuchsia::media::AudioPtr audio;
    app_context->svc()->Connect(audio.NewRequest());

    audio->CreateAudioRenderer(audio_renderer_.NewRequest());

    if (stream_mute_ || stream_gain_db_ || ramp_target_gain_db_) {
      audio_renderer_->BindGainControl(gain_control_.NewRequest());
      gain_control_.set_error_handler([this](zx_status_t status) {
        CLI_CHECK(Shutdown(),
                  "Client connection to fuchsia.media.audio.GainControl failed: " << status);
      });
    }

    // Set our render stream format, plus other settings as needed: gain, clock, continuity
    // threshold
    InitializeAudibleRenderer();

    // ... now just let the instance of audio go out of scope.
    //
    // Although we could technically call gain_control_'s SetMute|SetGain|SetGainWithRamp here,
    // then disconnect it (like we do for audio_core and audio), we instead maintain our
    // GainControl throughout playback, in case we someday want to change gain during playback.
  }

  if (online_) {
    online_send_packet_ref_period_ = (zx::sec(1) * frames_per_packet_) / frame_rate_;
  }

  SetAudioRendererEvents();
  ConfigureAudioRendererPts();
}

void MediaApp::ConfigureAudioRendererPts() {
  if (timestamp_packets_) {
    audio_renderer_->SetPtsUnits(frame_rate_, 1);
  }
  if (pts_continuity_threshold_secs_) {
    audio_renderer_->SetPtsContinuityThreshold(pts_continuity_threshold_secs_.value());
  }
}

// Set the AudioRenderer's audio format, plus other settings requested by command line
void MediaApp::InitializeAudibleRenderer() {
  CLI_CHECK(audio_renderer_, "audio_renderer must not be null");

  fuchsia::media::AudioStreamType format;
  format.sample_format = sample_format_;
  format.channels = num_channels_;
  format.frames_per_second = frame_rate_;

  // To indicate we want a reference clock OTHER than the default, we'll call SetReferenceClock().
  if (clock_type_ != ClockType::Default) {
    zx::clock reference_clock_to_set;

    if (clock_type_ == ClockType::Flexible) {
      // To select the Flexible clock maintained by audio_core, we effectively SetRefClock(NULL).
      reference_clock_to_set = zx::clock(ZX_HANDLE_INVALID);
    } else {
      // For Monotonic and Custom, we create and rights-reduce a clock to send to SetRefClock().
      zx_status_t status;
      zx::clock::update_args args;
      args.reset();
      if (clock_rate_adjustment_) {
        args.set_rate_adjust(clock_rate_adjustment_.value());
      }

      if (clock_type_ == ClockType::Monotonic) {
        // This clock is already started, in lock-step with CLOCK_MONOTONIC.
        reference_clock_to_set = audio::clock::AdjustableCloneOfMonotonic();
        CLI_CHECK(reference_clock_to_set.is_valid(),
                  "Invalid clock; could not clone monotonic clock");
      } else {
        // In custom clock case, set it to start at value zero. Rate-adjust it if specified.
        status = zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr,
                                   &reference_clock_to_set);
        CLI_CHECK_OK(status, "zx::clock::create failed");

        args.set_value(zx::time(0));
      }
      if (clock_rate_adjustment_ || clock_type_ == ClockType::Custom) {
        // update starts our clock
        status = reference_clock_to_set.update(args);
        CLI_CHECK_OK(status, "zx::clock::update failed");
      }

      // The clock we send to AudioRenderer cannot have ZX_RIGHT_WRITE. Most clients would
      // retain their custom clocks for subsequent rate-adjustment, and thus would use
      // 'duplicate' to create the rights-reduced clock. This app doesn't yet allow
      // rate-adjustment during playback (we also don't need this clock to read the current ref
      // time: we call GetReferenceClock later), so we use 'replace' (not 'duplicate').
      auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
      status = reference_clock_to_set.replace(rights, &reference_clock_to_set);
      CLI_CHECK_OK(status, "zx::clock::duplicate failed");
    }

    audio_renderer_->SetReferenceClock(std::move(reference_clock_to_set));
  }

  audio_renderer_->SetUsage(usage_);

  audio_renderer_->SetPcmStreamType(format);

  // Set usage volume, if specified.
  if (usage_volume_) {
    usage_volume_control_->SetVolume(usage_volume_.value());
  }

  // Set stream gain and mute, if specified.
  if (stream_mute_) {
    gain_control_->SetMute(stream_mute_.value());
  }
  if (stream_gain_db_) {
    gain_control_->SetGain(stream_gain_db_.value());
  }
  if (ramp_target_gain_db_) {
    gain_control_->SetGainWithRamp(ramp_target_gain_db_.value(), ramp_duration_nsec_,
                                   fuchsia::media::audio::RampType::SCALE_LINEAR);
  }
}

void MediaApp::InitializeWavWriter() {
  // 24-bit buffers use 32-bit samples (lowest byte zero), and when this particular utility saves to
  // .wav file, we save the entire 32 bits.
  if (file_name_) {
    wav_writer_initialized_ = wav_writer_.Initialize(file_name_.value().c_str(), sample_format_,
                                                     num_channels_, frame_rate_, sample_size_ * 8);
    CLI_CHECK(wav_writer_initialized_, "WavWriter::Initialize() failed");
  }
}

// Create a VMO and map memory for 1 sec of audio between them. Reduce rights and send handle to
// AudioRenderer: this is our shared buffer.
void MediaApp::CreateMemoryMapping() {
  for (size_t i = 0; i < num_payload_buffers_; ++i) {
    auto& payload_buffer = payload_buffers_.emplace_back();
    zx::vmo payload_vmo;
    zx_status_t status = payload_buffer.CreateAndMap(
        bytes_per_packet_ * packets_per_payload_buffer_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
        nullptr, &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

    CLI_CHECK(status == ZX_OK || Shutdown(), "VmoMapper:::CreateAndMap failed: " << status);

    audio_renderer_->AddPayloadBuffer(i, std::move(payload_vmo));
  }
}

void MediaApp::GetClockAndStart() {
  audio_renderer_->GetReferenceClock([this](zx::clock received_clock) {
    reference_clock_ = std::move(received_clock);

    if (verbose_) {
      audio::clock::GetAndDisplayClockDetails(reference_clock_);

      auto mono_now = zx::clock::get_monotonic();
      printf("- Received ref clock at %lu.  (%s sufficient min_lead_time)\n", mono_now.get(),
             (min_lead_time_ >= kRealDeviceMinLeadTime ? "Received" : "Awaiting"));
    }

    if (min_lead_time_ >= kRealDeviceMinLeadTime && !playing()) {
      Play();
    }
  });
}

// Prime (pre-submit) an initial set of packets, then start playback.
// Called from the GetReferenceClock callback
void MediaApp::Play() {
  if (num_packets_to_send_ == 0) {
    // No packets to send, so we're done! Shutdown will unwind everything and exit our loop.
    Shutdown();
    return;
  }

  zx::time ref_now;
  auto status = reference_clock_.read(ref_now.get_address());
  CLI_CHECK(status == ZX_OK || Shutdown(), "zx::clock::read failed during init: " << status);

  // read current time and use it as our rand48 seed ...
  srand48(ref_now.get());
  // ... before generating random data to prime our pink noise generator
  if (output_signal_type_ == kOutputTypePinkNoise) {
    PrimePinkNoiseFilter();
  }

  target_num_packets_outstanding_ =
      online_ ? (total_mappable_packets_ / 2) : total_mappable_packets_;
  target_num_packets_outstanding_ =
      std::min<uint64_t>(target_num_packets_outstanding_, num_packets_to_send_);

  auto target_duration_outstanding =
      (zx::sec(1) * target_num_packets_outstanding_ * frames_per_packet_) / frame_rate_;
  if (target_duration_outstanding < min_lead_time_ &&
      target_duration_outstanding < zx::nsec(ZX_SEC(1) * duration_secs_)) {
    printf("\nPayload buffer space is too small for the minimum lead time and signal duration.\n");
    Shutdown();
    return;
  }

  // We "prime" the audio renderer by submitting an initial set of packets before starting playback.
  // We will subsequently send the rest one at a time -- either from a timer (if 'online'), or from
  // the completion of a previous packet (if not 'online').
  // When priming, we send down only as many packets as concurrently fit into our payload buffer.
  // And if online, we send half that much, to provide leeway for the renderer to temporarily
  // complete packets too fast OR too slow, because of slight differences in clock rate.
  for (uint32_t packet_num = 0; packet_num < target_num_packets_outstanding_; ++packet_num) {
    SendPacket();
  }

  status = reference_clock_.read(ref_now.get_address());
  CLI_CHECK(status == ZX_OK || Shutdown(), "zx::clock::read failed during Play(): " << status);

  // Extrapolating backwards (to make future calculations easier), this represents when we would
  // have sent our first packet. This is our first approximation, we will update this when we
  // receive the actual start time.
  target_online_send_first_packet_ref_time_ = ref_now - target_duration_outstanding;

  reference_start_time_ = ref_now + kPlayStartupDelay + min_lead_time_;
  zx::time requested_ref_start_time =
      set_ref_start_time_ ? reference_start_time_ : zx::time(fuchsia::media::NO_TIMESTAMP);
  auto media_start_pts = media_start_pts_.value_or(fuchsia::media::NO_TIMESTAMP);

  if (verbose_) {
    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto requested_ref_str = RefTimeStrFromZxTime(requested_ref_start_time);
    auto requested_media_str = RefTimeStrFromZxTime(zx::time{media_start_pts});
    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("\nCalling Play (ref %s, media %s) at ref_now %s : mono_now %s\n",
           requested_ref_str.c_str(), requested_media_str.c_str(), ref_now_str.c_str(),
           mono_now_str.c_str());
  }

  auto play_completion_func = [this](int64_t actual_ref_start, int64_t actual_media_start) {
    if (verbose_) {
      zx::time ref_now;
      auto status = reference_clock_.read(ref_now.get_address());
      CLI_CHECK(status == ZX_OK || Shutdown(),
                "zx::clock::read failed during Play callback: " << status);

      auto mono_time_result =
          audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
      CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
      auto mono_now = mono_time_result.take_value();

      auto actual_ref_str = RefTimeStrFromZxTime(zx::time{actual_ref_start});
      auto actual_media_str = RefTimeStrFromZxTime(zx::time{actual_media_start});
      auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
      auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

      printf("Play callback(ref %s, media %s) at ref_now %s : mono_now %s\n\n",
             actual_ref_str.c_str(), actual_media_str.c_str(), ref_now_str.c_str(),
             mono_now_str.c_str());
    }

    // Now that we have the real start time, update our online "start" value.
    target_online_send_first_packet_ref_time_ =
        target_online_send_first_packet_ref_time_ +
        (zx::time(actual_ref_start) - reference_start_time_);
    reference_start_time_ = zx::time(actual_ref_start);
  };

  audio_renderer_->Play(requested_ref_start_time.get(), media_start_pts, play_completion_func);
  set_playing();

  if (online_) {
    ScheduleNextSendPacket();
  }
}

// We have a set of buffers each backed by its own VMO, with each buffer sub-divided into
// uniformly-sized zones, called payloads.
//
// We round robin packets across each buffer, wrapping around to the start of each buffer once
// the end is encountered. For example, with 2 buffers that can each hold 2 payloads, we would
// send audio packets in the following order:
//
//  ------------------------
// | buffer_id | payload_id |
// |   (vmo)   |  (offset)  |
// |-----------|------------|
// | buffer 0  |  payload 0 |
// | buffer 1  |  payload 0 |
// | buffer 0  |  payload 1 |
// | buffer 1  |  payload 1 |
// | buffer 0  |  payload 0 |
// |      ... etc ...       |
//  ------------------------
MediaApp::AudioPacket MediaApp::CreateAudioPacket(uint64_t packet_num) {
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = packet_num % num_payload_buffers_;

  auto buffer_payload_index = packet_num / num_payload_buffers_;
  packet.payload_offset = (buffer_payload_index % packets_per_payload_buffer_) * bytes_per_packet_;

  // If last payload, send exactly what remains (otherwise send a full payload).
  packet.payload_size =
      (packet_num + 1 == num_packets_to_send_)
          ? (total_frames_to_send_ - (packet_num * frames_per_packet_)) * frame_size_
          : bytes_per_packet_;

  // packet.pts (media time) is NO_TIMESTAMP by default unless we override it.
  if (timestamp_packets_) {
    packet.pts = packet_num * frames_per_packet_;  // assumes PTS units of "frames"

    if (media_start_pts_) {
      packet.pts += media_start_pts_.value();
    }
  }

  return {
      .stream_packet = std::move(packet),
      .vmo = &payload_buffers_[packet.payload_buffer_id],
  };
}

void MediaApp::GenerateAudioForPacket(const AudioPacket& audio_packet, uint64_t packet_num) {
  const auto& packet = audio_packet.stream_packet;
  auto audio_buff = reinterpret_cast<uint8_t*>(audio_packet.vmo->start()) + packet.payload_offset;

  // Recompute payload_frames each time, since the final packet may be 'short'.
  //
  // TODO(mpuryear): don't recompute this every time; use payload_frames_ (and pre-compute this)
  // except for last packet, which we either check for here or pass in as a boolean parameter.
  uint32_t payload_frames = packet.payload_size / frame_size_;

  switch (sample_format_) {
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      WriteAudioIntoBuffer<int32_t>(reinterpret_cast<int32_t*>(audio_buff), payload_frames,
                                    frames_per_packet_ * packet_num);
      break;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      WriteAudioIntoBuffer<int16_t>(reinterpret_cast<int16_t*>(audio_buff), payload_frames,
                                    frames_per_packet_ * packet_num);
      break;
    case fuchsia::media::AudioSampleFormat::FLOAT:
      WriteAudioIntoBuffer<float>(reinterpret_cast<float*>(audio_buff), payload_frames,
                                  frames_per_packet_ * packet_num);
      break;
    default:
      CLI_CHECK(false, "Unknown AudioSampleFormat");
  }
}

// Allocate memory for history values; advance the filter through its initial transient
void MediaApp::PrimePinkNoiseFilter() {
  input_history_ = std::make_unique<HistoryBuffer[]>(num_channels_);
  output_history_ = std::make_unique<HistoryBuffer[]>(num_channels_);

  // Skip the filter's initial transient response by pre-generating 1430 frames, the filter's T60
  // (-60 decay) interval, computed by "T60 = round(log(1000)/(1-max(abs(roots(kFeedBack)))))"
  for (auto i = 0u; i < 1430u; ++i) {
    AdvancePinkNoiseFrame();
  }
}

// Generate a pink-noise frame, using a four-stage filter with kFeedFwd and kFeedBack coefficients.
void MediaApp::AdvancePinkNoiseFrame() {
  // For each channel, calculate a new output based on cached vals plus a new random input value
  for (uint32_t chan = 0; chan < num_channels_; ++chan) {
    (void)NextPinkNoiseSample(chan);
  }
}

// Calculate and retrieve the new pink-noise sample value for this channel.
double MediaApp::NextPinkNoiseSample(uint32_t chan) {
  //
  // First, shift our previous inputs and outputs into the past, by one frame
  for (size_t i = 3; i > 0; --i) {
    output_history_[chan][i] = output_history_[chan][i - 1];
    input_history_[chan][i] = input_history_[chan][i - 1];
  }
  // (both [chan][0] values are now stale, but we overwrite them immediately)

  //
  // Second, generate the initial white-noise input, boosting to normalize the result.
  input_history_[chan][0] = drand48() * 2.0 - 1.0;
  input_history_[chan][0] *= kPinkNoiseSignalBoostFactor;

  //
  // Finally, apply the filter to {input + cached input/output values} to get the new output val.
  output_history_[chan][0] =
      (input_history_[chan][0] * kFeedFwd[0] + input_history_[chan][1] * kFeedFwd[1] +
       input_history_[chan][2] * kFeedFwd[2] + input_history_[chan][3] * kFeedFwd[3]) -
      (output_history_[chan][1] * kFeedBack[1] + output_history_[chan][2] * kFeedBack[2] +
       output_history_[chan][3] * kFeedBack[3]);

  return output_history_[chan][0];
}

// Write signal into the next section of our buffer. Track how many total frames since playback
// started, to handle arbitrary frequencies of type double.
template <typename SampleType>
void MediaApp::WriteAudioIntoBuffer(SampleType* audio_buffer, uint32_t num_frames,
                                    uint64_t frames_since_start) {
  const double rads_per_frame = 2.0 * M_PI / frames_per_period_;  // Radians/Frame.

  for (uint32_t frame = 0; frame < num_frames; ++frame, ++frames_since_start) {
    // Generated signal value, before applying amplitude scaling.
    double raw_val;

    for (auto chan_num = 0u; chan_num < num_channels_; ++chan_num) {
      switch (output_signal_type_) {
        case kOutputTypeSine:
          raw_val = sin(rads_per_frame * frames_since_start);
          break;
        case kOutputTypeSquare:
          raw_val =
              (fmod(frames_since_start, frames_per_period_) >= frames_per_period_ / 2) ? -1.0 : 1.0;
          break;
        case kOutputTypeSawtooth:
          raw_val = (fmod(frames_since_start / frames_per_period_, 1.0) * 2.0) - 1.0;
          break;
        case kOutputTypeTriangle:
          raw_val = (abs(fmod(frames_since_start / frames_per_period_, 1.0) - 0.5) * 4.0) - 1.0;
          break;
        case kOutputTypeNoise:
          raw_val = drand48() * 2.0 - 1.0;
          break;
        case kOutputTypePinkNoise:
          raw_val = NextPinkNoiseSample(chan_num);
          break;
      }

      // Final generated signal value
      SampleType val;
      if constexpr (std::is_same_v<SampleType, float>) {
        val = raw_val * amplitude_scalar_;
      } else if constexpr (std::is_same_v<SampleType,
                                          int32_t>) {       // 24-bit in 32-bit container:
        val = lround(raw_val * amplitude_scalar_ / 256.0);  // round at bit 8, and
        val = val << 8;                                     // leave bits 0-7 blank
      } else {
        val = lround(raw_val * amplitude_scalar_);
      }

      audio_buffer[frame * num_channels_ + chan_num] = val;
    }
  }
}

constexpr zx::duration kPktCompleteToleranceDuration = zx::msec(50);
constexpr uint64_t kPktCompleteTolerance = 5;

bool MediaApp::CheckPayloadSpace() {
  if (num_packets_completed_ > 0 && num_packets_sent_ <= num_packets_completed_) {
    printf("! Sending: packet %4lu; packet %4lu has already completed - did we underrun?\n",
           num_packets_sent_, num_packets_completed_);
    return false;
  }

  if (num_packets_sent_ >= num_packets_completed_ + total_mappable_packets_) {
    printf("! Sending: packet %4lu; only %4lu have completed - did we overrun?\n",
           num_packets_sent_, num_packets_completed_);
    return false;
  }

  target_num_packets_outstanding_ = std::min<uint64_t>(
      num_packets_to_send_ - num_packets_completed_, target_num_packets_outstanding_);
  auto actual_packets_outstanding = num_packets_sent_ - num_packets_completed_;

  auto target_duration_outstanding =
      (zx::sec(1) * target_num_packets_outstanding_ * frames_per_packet_) / frame_rate_;
  auto actual_duration_outstanding =
      (zx::sec(1) * actual_packets_outstanding * frames_per_packet_) / frame_rate_;

  auto elapsed_time_sec = static_cast<float>(num_frames_completed_) / frame_rate_;
  // Check whether payload buffer is staying at approx the same fullness.
  if (num_packets_completed_ > 0 &&
      actual_packets_outstanding + kPktCompleteTolerance <= target_num_packets_outstanding_ &&
      actual_duration_outstanding + kPktCompleteToleranceDuration <= target_duration_outstanding) {
    printf(
        "\n? %4lu packets outstanding (%ld msec); expected %4u (%ld msec); total elapsed %f sec: "
        "are we completing faster than sending?\n\n",
        actual_packets_outstanding, (actual_duration_outstanding / ZX_MSEC(1)).get(),
        target_num_packets_outstanding_, (target_duration_outstanding / ZX_MSEC(1)).get(),
        elapsed_time_sec);
    return false;
  }
  if (num_packets_completed_ > 0 &&
      target_num_packets_outstanding_ + kPktCompleteTolerance <= actual_packets_outstanding &&
      target_duration_outstanding + kPktCompleteToleranceDuration <= actual_duration_outstanding) {
    printf(
        "\n? %4lu packets outstanding (%ld msec); expected %4u (%ld msec); total elapsed %f sec: "
        "are we sending faster than completing?\n\n",
        actual_packets_outstanding, (actual_duration_outstanding / ZX_MSEC(1)).get(),
        target_num_packets_outstanding_, (target_duration_outstanding / ZX_MSEC(1)).get(),
        elapsed_time_sec);
    return false;
  }

  return true;
}

// Calculate the next SendPacket ref_time and mono_time, and Post to our async::TaskClosureMethod
void MediaApp::ScheduleNextSendPacket() {
  CLI_CHECK(online_, "Should only call NextSendPacket in online mode");
  CLI_CHECK(online_send_packet_ref_period_ > zx::duration(0), "SendPacket period is not set");

  if (num_packets_sent_ >= num_packets_to_send_) {
    return;
  }

  target_online_send_packet_ref_time_ = target_online_send_first_packet_ref_time_ +
                                        (online_send_packet_ref_period_ * num_packets_sent_);
  auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(
      reference_clock_, target_online_send_packet_ref_time_);
  CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
  auto target_mono_time = mono_time_result.take_value();

  if (verbose_) {
    zx::time ref_now;
    auto status = reference_clock_.read(ref_now.get_address());
    if (status != ZX_OK) {
      Shutdown();
      CLI_CHECK_OK(status, "zx::clock::read failed during Play callback");
    }

    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto target_ref_str = RefTimeStrFromZxTime(target_online_send_packet_ref_time_);
    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("Scheduling packet %4lu (reference %s) :  ref_now %s :  mono_now %s\n",
           num_packets_sent_, target_ref_str.c_str(), ref_now_str.c_str(), mono_now_str.c_str());
  }

  zx_status_t status =
      online_send_packet_timer_.PostForTime(audio_renderer_.dispatcher(), target_mono_time);
  if (status != ZX_OK) {
    Shutdown();
    CLI_CHECK_OK(status, "Failed to schedule SendPacket");
  }
}

void MediaApp::OnSendPacketTimer() {
  SendPacket();
  ScheduleNextSendPacket();
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void MediaApp::SendPacket() {
  CLI_CHECK(CheckPayloadSpace(), "Insufficient payload buffer space -- synchronization issue?");

  auto packet = CreateAudioPacket(num_packets_sent_);

  GenerateAudioForPacket(packet, num_packets_sent_);

  if (file_name_) {
    CLI_CHECK(wav_writer_.Write(reinterpret_cast<char*>(packet.vmo->start()) +
                                    packet.stream_packet.payload_offset,
                                packet.stream_packet.payload_size) ||
                  Shutdown(),
              "WavWriter::Write() failed");
  }

  if (verbose_) {
    zx::time ref_now;
    auto status = reference_clock_.read(ref_now.get_address());
    CLI_CHECK((status == ZX_OK) || Shutdown(),
              "zx::clock::read failed during SendPacket(): " << status);

    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto pts_str = RefTimeStrFromZxTime(zx::time{packet.stream_packet.pts});
    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("  Sending: packet %4lu (media pts %s) :  ref_now %s :  mono_now %s\n",
           num_packets_sent_, pts_str.c_str(), ref_now_str.c_str(), mono_now_str.c_str());
  }

  ++num_packets_sent_;
  uint64_t frames_completed = packet.stream_packet.payload_size / frame_size_;
  audio_renderer_->SendPacket(
      packet.stream_packet, [this, frames_completed]() { OnSendPacketComplete(frames_completed); });
}

void MediaApp::OnSendPacketComplete(uint64_t frames_completed) {
  num_frames_completed_ += frames_completed;

  if (verbose_) {
    zx::time ref_now;
    auto status = reference_clock_.read(ref_now.get_address());
    CLI_CHECK(status == ZX_OK || Shutdown(),
              "zx::clock::read failed during OnSendPacketComplete(): " << status);

    auto mono_time_result = audio::clock::MonotonicTimeFromReferenceTime(reference_clock_, ref_now);
    CLI_CHECK(mono_time_result.is_ok(), "Could not convert ref_time to mono_time");
    auto mono_now = mono_time_result.take_value();

    auto ref_now_str = RefTimeMsStrFromZxTime(ref_now);
    auto mono_now_str = RefTimeMsStrFromZxTime(mono_now);

    printf("Completed: packet %4lu (%5lu frames, up to %8lu ) :  ref_now %s :  mono_now %s\n",
           num_packets_completed_, frames_completed, num_frames_completed_, ref_now_str.c_str(),
           mono_now_str.c_str());
  }

  ++num_packets_completed_;
  CLI_CHECK(num_packets_completed_ <= num_packets_to_send_,
            "packets_completed cannot exceed packets_to_send");

  if (num_packets_completed_ >= num_packets_to_send_) {
    Shutdown();
  } else if (num_packets_sent_ < num_packets_to_send_ && !online_) {
    SendPacket();
  }
}

// Enable audio renderer callbacks
void MediaApp::SetAudioRendererEvents() {
  audio_renderer_.set_error_handler([this](zx_status_t status) {
    CLI_CHECK(Shutdown(), "Client connection to fuchsia.media.AudioRenderer failed: " << status);
  });

  audio_renderer_.events().OnMinLeadTimeChanged = [this](int64_t min_lead_time_nsec) {
    min_lead_time_ = zx::duration(min_lead_time_nsec);

    if (verbose_) {
      printf("- OnMinLeadTimeChanged: %lu at %lu: %s to start playback  (%s ref clock)\n",
             min_lead_time_nsec, zx::clock::get_monotonic().get(),
             (min_lead_time_ >= kRealDeviceMinLeadTime ? "sufficient" : "insufficient"),
             (reference_clock_.is_valid() ? "Received" : "Awaiting"));
    }

    if (min_lead_time_ >= kRealDeviceMinLeadTime && reference_clock_.is_valid() && !playing()) {
      Play();
    }
  };

  audio_renderer_->EnableMinLeadTimeEvents(true);
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~MediaApp).
bool MediaApp::Shutdown() {
  online_send_packet_timer_.Cancel();

  gain_control_.Unbind();
  usage_volume_control_.Unbind();
  audio_renderer_.Unbind();

  if (wav_writer_initialized_) {
    CLI_CHECK(wav_writer_.Close(), "WavWriter::Close() failed");
  }

  payload_buffers_.clear();
  quit_callback_();

  return false;
}

}  // namespace media::tools
