// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/tones/tones.h"

#include <cmath>
#include <iostream>
#include <limits>

#include <fbl/auto_call.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/examples/media/tones/midi_keyboard.h"
#include "lib/fxl/logging.h"

namespace examples {
namespace {

static constexpr uint32_t kChannelCount = 1;
static constexpr uint32_t kFramesPerSecond = 48000;
static constexpr uint32_t kFramesPerBuffer = 240;
static constexpr int64_t kLeadTimeOverheadNSec = ZX_MSEC(15);
static constexpr float kEffectivelySilentVolume = 0.001f;
static constexpr float kA4Frequency = 440.0f;
static constexpr float kVolume = 0.2f;
static constexpr float kDecay = 0.95f;
static constexpr uint32_t kBeatsPerMinute = 90;
static inline constexpr uint32_t nsec_to_packets(uint64_t nsec) {
  return static_cast<uint32_t>(
      ((nsec * kFramesPerSecond) + (kFramesPerBuffer - 1)) /
      (ZX_SEC(1) * kFramesPerBuffer));
}
static constexpr uint32_t kSharedBufferPackets = nsec_to_packets(ZX_MSEC(300));

// Translates a note number into a frequency.
float Note(int32_t note) {
  // Map note ordinal zero to middle C (eg. C4) on a standard piano tuning.  Use
  // A4 (440Hz) as our reference frequency, keeping in mind that A4 is 9 half
  // steps above C4.
  constexpr int32_t kA4C4HalfStepDistance = 9;
  note -= kA4C4HalfStepDistance;
  return kA4Frequency * pow(2.0f, note / 12.0f);
}

// Translates a beat number into a time.
constexpr int64_t Beat(float beat) {
  return static_cast<int64_t>((beat * 60.0f * kFramesPerSecond) /
                              kBeatsPerMinute);
}

static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
    fuchsia::media::AudioSampleFormat::FLOAT;
static constexpr uint32_t kBytesPerFrame = kChannelCount * sizeof(float);
static constexpr size_t kBytesPerBuffer = kBytesPerFrame * kFramesPerBuffer;

static const std::map<int, float> notes_by_key_ = {
    {'a', Note(-4)}, {'z', Note(-3)}, {'s', Note(-2)}, {'x', Note(-1)},
    {'c', Note(0)},  {'f', Note(1)},  {'v', Note(2)},  {'g', Note(3)},
    {'b', Note(4)},  {'n', Note(5)},  {'j', Note(6)},  {'m', Note(7)},
    {'k', Note(8)},  {',', Note(9)},  {'l', Note(10)}, {'.', Note(11)},
    {'/', Note(12)}, {'\'', Note(13)}};

}  // namespace

Tones::Tones(bool interactive, fit::closure quit_callback)
    : interactive_(interactive), quit_callback_(std::move(quit_callback)) {
  // Connect to the audio service and get a renderer.
  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  fuchsia::media::AudioPtr audio =
      startup_context->ConnectToEnvironmentService<fuchsia::media::Audio>();

  audio->CreateAudioOut(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this]() {
    std::cerr << "Unexpected error: channel to audio service closed\n";
    Quit();
  });

  // Configure the stream_type of the renderer.
  fuchsia::media::AudioStreamType stream_type;
  stream_type.sample_format = kSampleFormat;
  stream_type.channels = kChannelCount;
  stream_type.frames_per_second = kFramesPerSecond;
  audio_renderer_->SetPcmStreamType(std::move(stream_type));

  // Fetch the minimum lead time.  When we know what this is, we can allocate
  // our payload buffer and start the synthesis loop.
  audio_renderer_.events().OnMinLeadTimeChanged = [this](int64_t nsec) {
    OnMinLeadTimeChanged(nsec);
  };
  audio_renderer_->EnableMinLeadTimeEvents(true);
}

Tones::~Tones() {}

void Tones::Quit() {
  midi_keyboard_.reset();
  audio_renderer_.Unbind();
  quit_callback_();
}

void Tones::WaitForKeystroke() {
  fd_waiter_.Wait(
      [this](zx_status_t status, uint32_t events) { HandleKeystroke(); }, 0,
      POLLIN);
}

void Tones::HandleKeystroke() {
  int c = std::tolower(getc(stdin));

  auto iter = notes_by_key_.find(c);
  if (iter != notes_by_key_.end()) {
    tone_generators_.emplace_back(kFramesPerSecond, iter->second, kVolume,
                                  kDecay);
  }

  switch (c) {
    case 'q':
    case 0x1b:  // escape
      Quit();
      return;
    default:
      break;
  }

  WaitForKeystroke();
}

void Tones::HandleMidiNote(int note, int velocity, bool note_on) {
  if (note_on) {
    tone_generators_.emplace_back(kFramesPerSecond, Note(note), kVolume,
                                  kDecay);
  }
}

void Tones::BuildScore() {
  frequencies_by_pts_.emplace(Beat(0.0f), Note(12));
  frequencies_by_pts_.emplace(Beat(1.0f), Note(11));
  frequencies_by_pts_.emplace(Beat(2.0f), Note(9));
  frequencies_by_pts_.emplace(Beat(3.0f), Note(7));
  frequencies_by_pts_.emplace(Beat(4.0f), Note(5));
  frequencies_by_pts_.emplace(Beat(5.0f), Note(4));
  frequencies_by_pts_.emplace(Beat(6.0f), Note(2));
  frequencies_by_pts_.emplace(Beat(7.0f), Note(7));
  frequencies_by_pts_.emplace(Beat(8.0f), Note(9));
  frequencies_by_pts_.emplace(Beat(9.0f), Note(4));
  frequencies_by_pts_.emplace(Beat(10.0f), Note(5));
  frequencies_by_pts_.emplace(Beat(11.0f), Note(0));
  frequencies_by_pts_.emplace(Beat(12.0f), Note(2));
  frequencies_by_pts_.emplace(Beat(13.0f), Note(7));
  frequencies_by_pts_.emplace(Beat(14.0f), Note(0));
  frequencies_by_pts_.emplace(Beat(14.0f), Note(4));
  frequencies_by_pts_.emplace(Beat(14.0f), Note(7));
}

void Tones::OnMinLeadTimeChanged(int64_t min_lead_time_nsec) {
  // If anything goes wrong here, shut down.
  auto cleanup = fbl::MakeAutoCall([this]() { Quit(); });

  // figure out how many packets we need to keep in flight at all times.
  if (min_lead_time_nsec < 0) {
    std::cerr << "Audio renderer reported invalid lead time ("
              << min_lead_time_nsec << "nSec)\n";
    return;
  }

  min_lead_time_nsec += kLeadTimeOverheadNSec;
  target_packets_in_flight_ = nsec_to_packets(min_lead_time_nsec);
  if (target_packets_in_flight_ > kSharedBufferPackets) {
    std::cerr
        << "Required min lead time (" << min_lead_time_nsec
        << " nsec) requires more than the maximum allowable buffers in flight ("
        << target_packets_in_flight_ << " > " << kSharedBufferPackets << ")!\n";
    return;
  }

  if (!started_) {
    constexpr size_t total_mapping_size =
        static_cast<size_t>(kSharedBufferPackets) * kFramesPerBuffer *
        kBytesPerFrame;

    // Allocate our shared payload buffer and pass a handle to it over to the
    // renderer.
    zx::vmo payload_vmo;
    zx_status_t status = payload_buffer_.CreateAndMap(
        total_mapping_size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
        nullptr, &payload_vmo,
        ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

    if (status != ZX_OK) {
      std::cerr << "VmoMapper:::CreateAndMap failed - " << status << "\n";
      return;
    }

    // Assign our shared payload buffer to the renderer.
    audio_renderer_->AddPayloadBuffer(0, std::move(payload_vmo));

    // Configure the renderer to use input frames of audio as its PTS units.
    audio_renderer_->SetPtsUnits(kFramesPerSecond, 1);

    // Listen for keystrokes.
    WaitForKeystroke();

    // If we are operating in interactive mode, go looking for a midi keyboard
    // to listen to.
    if (interactive_) {
      midi_keyboard_ = MidiKeyboard::Create(this);
    }

    if (interactive_) {
      std::cout << "| | | |  |  | | | |  |  | | | | | |  |  | |\n";
      std::cout << "|A| |S|  |  |F| |G|  |  |J| |K| |L|  |  |'|\n";
      std::cout << "+-+ +-+  |  +-+ +-+  |  +-+ +-+ +-+  |  +-+\n";
      std::cout << " |   |   |   |   |   |   |   |   |   |   | \n";
      std::cout << " | Z | X | C | V | B | N | M | , | . | / | \n";
      std::cout << "-+---+---+---+---+---+---+---+---+---+---+-\n";
    } else {
      std::cout
          << "Playing a tune. Use '--interactive' to play the keyboard.\n";
      BuildScore();
    }

    SendPackets();
    audio_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                                 fuchsia::media::NO_TIMESTAMP);
    started_ = true;
  } else {
    SendPackets();
  }

  cleanup.cancel();
}

void Tones::SendPackets() {
  while (!done() && (active_packets_in_flight_ < target_packets_in_flight_)) {
    // Allocate packet and locate its position in the buffer.
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = (pts_ * kBytesPerFrame) % payload_buffer_.size();
    packet.payload_size = kBytesPerBuffer;

    FXL_DCHECK((packet.payload_offset + packet.payload_size) <=
               payload_buffer_.size());

    auto payload_ptr = reinterpret_cast<uint8_t*>(payload_buffer_.start()) +
                       packet.payload_offset;

    // Fill it with audio.
    FillBuffer(reinterpret_cast<float*>(payload_ptr));

    // Send it.
    //
    // TODO(johngro): If we really want to minimize latency through the system,
    // we should not be using the SendPacket callbacks to drive the system to
    // mix more.  Doing this means that we need to wait until the oldest packet
    // in the pipeline is completely rendered, and then wait for the mixer to
    // release to packet back to us.  It can take a bit of time for the mixer to
    // wake up and trim the packet, and it will take time for the message that a
    // packet has been renderered to make it all of the way back to us.
    //
    // These delays really do not matter all that much for non-realtime tasks
    // which can usually buffer 50 mSec or more into the future without any
    // problem, but if we want to get rid of all of that overhead, we should
    // really shift to a pure timing based model which allows us to wake up
    // right before the minimum lead time, then synth and send a new packet just
    // before the pipeline runs dry.
    //
    // If/when we update this code to move to that model, we should really start
    // to listen for minimum lead time changed events as well (as the lead time
    // requirements can vary as we get routed to different outputs).
    if (!done()) {
      auto on_complete = [this]() {
        FXL_DCHECK(active_packets_in_flight_ > 0);
        active_packets_in_flight_--;
        SendPackets();
      };
      audio_renderer_->SendPacket(std::move(packet), std::move(on_complete));
    } else {
      audio_renderer_->SendPacket(std::move(packet), [this] { Quit(); });
    }

    active_packets_in_flight_++;
  }
}

void Tones::FillBuffer(float* buffer) {
  // Zero out the buffer, because the tone generators mix into it.
  std::memset(buffer, 0, kFramesPerBuffer * 4);

  // Mix in the tone generators we've already created.
  for (auto iter = tone_generators_.begin(); iter != tone_generators_.end();) {
    if (iter->volume() <= kEffectivelySilentVolume) {
      iter = tone_generators_.erase(iter);
    } else {
      iter->MixSamples(buffer, kFramesPerBuffer, kChannelCount);
      ++iter;
    }
  }

  // Create new tone generators as needed.
  while (!frequencies_by_pts_.empty()) {
    int64_t when = frequencies_by_pts_.begin()->first;
    float frequency = frequencies_by_pts_.begin()->second;

    if (when >= pts_ + kFramesPerBuffer) {
      break;
    }

    frequencies_by_pts_.erase(frequencies_by_pts_.begin());

    int64_t offset = when - pts_;
    tone_generators_.emplace_back(kFramesPerSecond, frequency, kVolume, kDecay);

    // Mix in the new tone generator starting at the correct offset in the
    // buffer.
    tone_generators_.back().MixSamples(buffer + (offset * kChannelCount),
                                       kFramesPerBuffer - offset,
                                       kChannelCount);
  }

  pts_ += kFramesPerBuffer;
}

}  // namespace examples
