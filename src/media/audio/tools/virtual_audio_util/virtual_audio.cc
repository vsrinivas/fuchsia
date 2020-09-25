// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/drivers/virtual_audio/virtual_audio.h"

#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/media/cpp/timeline_rate.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/device/audio.h>
#include <zircon/syscalls/clock.h>

#include <iterator>

#include <fbl/algorithm.h>

#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace virtual_audio {

class VirtualAudioUtil {
 public:
  VirtualAudioUtil(async::Loop* loop) { VirtualAudioUtil::loop_ = loop; }

  void Run(fxl::CommandLine* cmdline);

 private:
  enum class Command {
    ENABLE_VIRTUAL_AUDIO,
    DISABLE_VIRTUAL_AUDIO,
    GET_NUM_VIRTUAL_DEVICES,

    SET_DEVICE_NAME,
    SET_MANUFACTURER,
    SET_PRODUCT_NAME,
    SET_UNIQUE_ID,
    ADD_FORMAT_RANGE,
    CLEAR_FORMAT_RANGES,
    SET_CLOCK_DOMAIN,
    SET_INITIAL_CLOCK_RATE,
    SET_FIFO_DEPTH,
    SET_EXTERNAL_DELAY,
    SET_RING_BUFFER_RESTRICTIONS,
    SET_GAIN_PROPS,
    SET_PLUG_PROPS,
    RESET_CONFIG,

    ADD_DEVICE,
    REMOVE_DEVICE,
    PLUG,
    UNPLUG,
    GET_GAIN,
    GET_FORMAT,
    RETRIEVE_BUFFER,
    WRITE_BUFFER,
    GET_POSITION,
    SET_NOTIFICATION_FREQUENCY,
    ADJUST_CLOCK_RATE,

    SET_IN,
    SET_OUT,
    WAIT,
    INVALID,
  };

  static constexpr struct {
    const char* name;
    Command cmd;
  } COMMANDS[] = {
      {"enable", Command::ENABLE_VIRTUAL_AUDIO},
      {"disable", Command::DISABLE_VIRTUAL_AUDIO},
      {"num-devs", Command::GET_NUM_VIRTUAL_DEVICES},

      {"dev", Command::SET_DEVICE_NAME},
      {"mfg", Command::SET_MANUFACTURER},
      {"prod", Command::SET_PRODUCT_NAME},
      {"id", Command::SET_UNIQUE_ID},
      {"add-format", Command::ADD_FORMAT_RANGE},
      {"clear-format", Command::CLEAR_FORMAT_RANGES},
      {"domain", Command::SET_CLOCK_DOMAIN},
      {"initial-rate", Command::SET_INITIAL_CLOCK_RATE},
      {"fifo", Command::SET_FIFO_DEPTH},
      {"delay", Command::SET_EXTERNAL_DELAY},
      {"rb", Command::SET_RING_BUFFER_RESTRICTIONS},
      {"gain-props", Command::SET_GAIN_PROPS},
      {"plug-props", Command::SET_PLUG_PROPS},
      {"reset", Command::RESET_CONFIG},

      {"add", Command::ADD_DEVICE},
      {"remove", Command::REMOVE_DEVICE},

      {"plug", Command::PLUG},
      {"unplug", Command::UNPLUG},
      {"get-gain", Command::GET_GAIN},
      {"get-format", Command::GET_FORMAT},
      {"get-rb", Command::RETRIEVE_BUFFER},
      {"write-rb", Command::WRITE_BUFFER},
      {"get-pos", Command::GET_POSITION},
      {"notifs", Command::SET_NOTIFICATION_FREQUENCY},
      {"rate", Command::ADJUST_CLOCK_RATE},

      {"in", Command::SET_IN},
      {"out", Command::SET_OUT},
      {"wait", Command::WAIT},
  };
  static constexpr char kDefaultDeviceName[] = "Vertex";
  static constexpr char kDefaultManufacturer[] = "Puerile Virtual Functions, Incorporated";
  static constexpr char kDefaultProductName[] = "Virgil, version 1.0";
  static constexpr uint8_t kDefaultUniqueId[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                                   0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

  static constexpr int32_t kDefaultClockDomain = 0;
  static constexpr int32_t kDefaultInitialClockRatePpm = 0;

  static constexpr uint8_t kDefaultFormatRangeOption = 0;

  static constexpr uint32_t kDefaultFifoDepth = 0x100;
  static constexpr uint64_t kDefaultExternalDelayNsec = zx::msec(1).get();
  static constexpr uint8_t kDefaultRingBufferOption = 0;

  // This repeated value can be interpreted various ways, at various sample_sizes and num_chans.
  static constexpr uint64_t kDefaultValueToWrite = 0x0000765400009ABC;

  static constexpr uint8_t kDefaultGainPropsOption = 0;
  static constexpr uint8_t kDefaultPlugPropsOption = 0;
  static constexpr uint32_t kDefaultNotificationFrequency = 4;

  static async::Loop* loop_;
  static bool received_callback_;

  void QuitLoop();
  bool RunForDuration(zx::duration duration);
  bool WaitForNoCallback();
  bool WaitForCallback();

  void RegisterKeyWaiter();
  bool WaitForKey();

  bool ConnectToController();
  bool ConnectToDevice();
  bool ConnectToInput();
  bool ConnectToOutput();
  void SetUpEvents();

  void ParseAndExecute(fxl::CommandLine* cmdline);
  bool ExecuteCommand(Command cmd, const std::string& value);

  // Methods using the FIDL Service interface
  bool Enable(bool enable);
  bool GetNumDevices();

  // Methods using the FIDL Configuration interface
  bool SetDeviceName(const std::string& name);
  bool SetManufacturer(const std::string& name);
  bool SetProductName(const std::string& name);
  bool SetUniqueId(const std::string& unique_id);
  bool AddFormatRange(const std::string& format_str);
  bool ClearFormatRanges();
  bool SetClockDomain(const std::string& clock_domain_str);
  bool SetInitialClockRate(const std::string& initial_clock_rate_str);
  bool SetFifoDepth(const std::string& fifo_str);
  bool SetExternalDelay(const std::string& delay_str);
  bool SetRingBufferRestrictions(const std::string& rb_restr_str);
  bool SetGainProps(const std::string& gain_props_str);
  bool SetPlugProps(const std::string& plug_props_str);
  bool ResetConfiguration();

  bool AddDevice();

  // Methods using the FIDL Device interface
  bool RemoveDevice();
  bool ChangePlugState(const std::string& plug_time_str, bool plugged);
  bool GetGain();
  bool GetFormat();
  bool GetBuffer();
  bool WriteBuffer(const std::string& write_val_str);
  bool GetPosition();
  bool SetNotificationFrequency(const std::string& override_notifs_str);
  bool AdjustClockRate(const std::string& clock_adjust_str);

  std::unique_ptr<sys::ComponentContext> component_context_;
  fsl::FDWaiter keystroke_waiter_;
  bool key_quit_ = false;

  fuchsia::virtualaudio::ControlPtr controller_ = nullptr;
  fuchsia::virtualaudio::InputPtr input_ = nullptr;
  fuchsia::virtualaudio::OutputPtr output_ = nullptr;

  bool configuring_output_ = true;
  static zx::vmo ring_buffer_vmo_;
  static uint64_t ring_buffer_size_;

  static uint32_t BytesPerSample(uint32_t format);
  static void UpdateRunningPosition(uint32_t rb_pos, bool is_output_);

  static size_t rb_size_[2];
  static uint32_t last_rb_position_[2];
  static uint64_t running_position_[2];

 public:
  static uint32_t frame_size_[2];
  static media::TimelineRate ref_time_to_running_position_rate_[2];
  static media::TimelineFunction ref_time_to_running_position_[2];

 private:
  static void CallbackReceived();
  static void EnableCallback();
  static void DisableCallback();
  static void NumDevicesCallback(uint32_t num_inputs, uint32_t num_outputs);
  template <bool is_out>
  static void FormatNotification(uint32_t fps, uint32_t fmt, uint32_t chans, zx_duration_t delay);
  template <bool is_out>
  static void FormatCallback(uint32_t fps, uint32_t fmt, uint32_t chans, zx_duration_t delay);

  template <bool is_out>
  static void GainNotification(bool current_mute, bool current_agc, float gain_db);
  template <bool is_out>
  static void GainCallback(bool current_mute, bool current_agc, float gain_db);

  template <bool is_out>
  static void BufferNotification(zx::vmo buff, uint32_t rb_frames, uint32_t notifs);
  template <bool is_out>
  static void BufferCallback(zx::vmo buff, uint32_t rb_frames, uint32_t notifs);

  template <bool is_out>
  static void StartNotification(zx_time_t start_time);
  template <bool is_out>
  static void StopNotification(zx_time_t stop_time, uint32_t ring_position);

  template <bool is_out>
  static void PositionNotification(zx_time_t monotonic_time, uint32_t ring_position);
  template <bool is_out>
  static void PositionCallback(zx_time_t monotonic_time, uint32_t ring_position);
};

::async::Loop* VirtualAudioUtil::loop_;
bool VirtualAudioUtil::received_callback_;
zx::vmo VirtualAudioUtil::ring_buffer_vmo_;
uint64_t VirtualAudioUtil::ring_buffer_size_ = 0;

size_t VirtualAudioUtil::rb_size_[2];
uint32_t VirtualAudioUtil::last_rb_position_[2];
uint64_t VirtualAudioUtil::running_position_[2];
uint32_t VirtualAudioUtil::frame_size_[2];
media::TimelineRate VirtualAudioUtil::ref_time_to_running_position_rate_[2];
media::TimelineFunction VirtualAudioUtil::ref_time_to_running_position_[2];

int32_t output_clock_domain_ = 0;
int32_t input_clock_domain_ = 0;
int32_t output_rate_adjustment_ppm_ = 0;
int32_t input_rate_adjustment_ppm_ = 0;

enum DeviceType { kOutput = 0u, kInput = 1u };
uint32_t VirtualAudioUtil::BytesPerSample(uint32_t format_bitfield) {
  if (format_bitfield & (AUDIO_SAMPLE_FORMAT_20BIT_IN32 | AUDIO_SAMPLE_FORMAT_24BIT_IN32 |
                         AUDIO_SAMPLE_FORMAT_32BIT | AUDIO_SAMPLE_FORMAT_32BIT_FLOAT)) {
    return 4;
  }
  if (format_bitfield & AUDIO_SAMPLE_FORMAT_24BIT_PACKED) {
    return 3;
  }
  if (format_bitfield & AUDIO_SAMPLE_FORMAT_16BIT) {
    return 2;
  }
  if (format_bitfield & AUDIO_SAMPLE_FORMAT_8BIT) {
    return 1;
  }

  printf("\n--Unknown format, could not determine bytes per sample. Exiting.\n");

  return 0;
}

// VirtualAudioUtil implementation
//
void VirtualAudioUtil::Run(fxl::CommandLine* cmdline) {
  ParseAndExecute(cmdline);

  // We are done!  Disconnect any error handlers.
  if (input_.is_bound()) {
    input_.set_error_handler(nullptr);
  }
  if (output_.is_bound()) {
    output_.set_error_handler(nullptr);
  }

  // If any lingering callbacks were queued, let them drain.
  if (!WaitForNoCallback()) {
    printf("Received unexpected callback!\n");
  }
}

void VirtualAudioUtil::QuitLoop() {
  async::PostTask(loop_->dispatcher(), [loop = loop_]() { loop->Quit(); });
}

// Below was borrowed from gtest, as-is
bool VirtualAudioUtil::RunForDuration(zx::duration duration) {
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  async::PostDelayedTask(
      loop_->dispatcher(),
      [loop = loop_, canceled, &timed_out] {
        if (*canceled) {
          return;
        }
        timed_out = true;
        loop->Quit();
      },
      duration);
  loop_->Run();
  loop_->ResetQuit();

  if (!timed_out) {
    *canceled = true;
  }
  return timed_out;
}
// Above was borrowed from gtest, as-is

bool VirtualAudioUtil::WaitForNoCallback() {
  received_callback_ = false;
  bool timed_out = RunForDuration(zx::msec(5));

  // If all is well, we DIDN'T get a disconnect callback and are still bound.
  if (received_callback_) {
    printf("  ... received unexpected callback\n");
  }
  return (timed_out && !received_callback_);
}

bool VirtualAudioUtil::WaitForCallback() {
  received_callback_ = false;
  bool timed_out = RunForDuration(zx::msec(2000));

  if (!received_callback_) {
    printf("  ... expected a callback; none was received\n");
  }
  return (!timed_out && received_callback_);
}

void VirtualAudioUtil::RegisterKeyWaiter() {
  keystroke_waiter_.Wait(
      [this](zx_status_t, uint32_t) {
        int c = std::tolower(getc(stdin));
        if (c == 'q') {
          key_quit_ = true;
        }
        QuitLoop();
      },
      STDIN_FILENO, POLLIN);
}

bool VirtualAudioUtil::WaitForKey() {
  printf("\tPress Q to cancel, or any other key to continue...\n");
  setbuf(stdin, nullptr);
  RegisterKeyWaiter();

  while (RunForDuration(zx::sec(1))) {
  }

  return !key_quit_;
}

bool VirtualAudioUtil::ConnectToController() {
  if (controller_.is_bound()) {
    return true;
  }

  component_context_->svc()->Connect(controller_.NewRequest());

  controller_.set_error_handler([this](zx_status_t error) {
    printf("controller_ disconnected (%d)!\n", error);
    QuitLoop();
  });

  // let VirtualAudio disconnect if all is not well.
  bool success = (WaitForNoCallback() && controller_.is_bound());

  if (!success) {
    printf("Failed to establish channel to async controller\n");
  }
  return success;
}

bool VirtualAudioUtil::ConnectToDevice() {
  return (configuring_output_ ? ConnectToOutput() : ConnectToInput());
}

bool VirtualAudioUtil::ConnectToInput() {
  if (input_.is_bound()) {
    return true;
  }

  component_context_->svc()->Connect(input_.NewRequest());

  input_.set_error_handler([this](zx_status_t error) {
    printf("input_ disconnected (%d)!\n", error);
    QuitLoop();
  });

  SetUpEvents();

  // let VirtualAudio disconnect if all is not well.
  bool success = (WaitForNoCallback() && input_.is_bound());

  if (!success) {
    printf("Failed to establish channel to input\n");
  }
  return success;
}

bool VirtualAudioUtil::ConnectToOutput() {
  if (output_.is_bound()) {
    return true;
  }

  component_context_->svc()->Connect(output_.NewRequest());

  output_.set_error_handler([this](zx_status_t error) {
    printf("output_ disconnected (%d)!\n", error);
    QuitLoop();
  });

  SetUpEvents();

  // let VirtualAudio disconnect if all is not well.
  bool success = (WaitForNoCallback() && output_.is_bound());

  if (!success) {
    printf("Failed to establish channel to output\n");
  }
  return success;
}

void VirtualAudioUtil::SetUpEvents() {
  if (configuring_output_) {
    output_.events().OnSetFormat = FormatNotification<true>;
    output_.events().OnSetGain = GainNotification<true>;
    output_.events().OnBufferCreated = BufferNotification<true>;
    output_.events().OnStart = StartNotification<true>;
    output_.events().OnStop = StopNotification<true>;
    output_.events().OnPositionNotify = PositionNotification<true>;
  } else {
    input_.events().OnSetFormat = FormatNotification<false>;
    input_.events().OnSetGain = GainNotification<false>;
    input_.events().OnBufferCreated = BufferNotification<false>;
    input_.events().OnStart = StartNotification<false>;
    input_.events().OnStop = StopNotification<false>;
    input_.events().OnPositionNotify = PositionNotification<false>;
  }
}

void VirtualAudioUtil::ParseAndExecute(fxl::CommandLine* cmdline) {
  if (!cmdline->has_argv0() || cmdline->options().size() == 0) {
    printf("No commands provided; no action taken\n");
    return;
  }

  // Looks like we will interact with the service; get ready to connect to it.
  component_context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  for (auto option : cmdline->options()) {
    bool success = false;
    Command cmd = Command::INVALID;

    for (const auto& entry : COMMANDS) {
      if (!option.name.compare(entry.name)) {
        cmd = entry.cmd;
        success = true;

        break;
      }
    }

    if (!success) {
      printf("Failed to parse command ID `--%s'\n", option.name.c_str());
      return;
    }

    printf("Executing `--%s' command...\n", option.name.c_str());
    success = ExecuteCommand(cmd, option.value);
    if (!success) {
      printf("  ... `--%s' command was unsuccessful\n", option.name.c_str());
      return;
    }
  }  // while (cmdline args) without default
}

bool VirtualAudioUtil::ExecuteCommand(Command cmd, const std::string& value) {
  bool success;
  switch (cmd) {
    // FIDL Service methods
    case Command::ENABLE_VIRTUAL_AUDIO:
      success = Enable(true);
      break;
    case Command::DISABLE_VIRTUAL_AUDIO:
      success = Enable(false);
      break;
    case Command::GET_NUM_VIRTUAL_DEVICES:
      success = GetNumDevices();
      break;

    // FIDL Configuration/Device methods
    case Command::SET_DEVICE_NAME:
      success = SetDeviceName(value);
      break;
    case Command::SET_MANUFACTURER:
      success = SetManufacturer(value);
      break;
    case Command::SET_PRODUCT_NAME:
      success = SetProductName(value);
      break;
    case Command::SET_UNIQUE_ID:
      success = SetUniqueId(value);
      break;
    case Command::SET_CLOCK_DOMAIN:
      success = SetClockDomain(value);
      break;
    case Command::SET_INITIAL_CLOCK_RATE:
      success = SetInitialClockRate(value);
      break;
    case Command::ADD_FORMAT_RANGE:
      success = AddFormatRange(value);
      break;
    case Command::CLEAR_FORMAT_RANGES:
      success = ClearFormatRanges();
      break;
    case Command::SET_FIFO_DEPTH:
      success = SetFifoDepth(value);
      break;
    case Command::SET_EXTERNAL_DELAY:
      success = SetExternalDelay(value);
      break;
    case Command::SET_RING_BUFFER_RESTRICTIONS:
      success = SetRingBufferRestrictions(value);
      break;
    case Command::SET_GAIN_PROPS:
      success = SetGainProps(value);
      break;
    case Command::SET_PLUG_PROPS:
      success = SetPlugProps(value);
      break;
    case Command::RESET_CONFIG:
      success = ResetConfiguration();
      break;

    case Command::ADD_DEVICE:
      success = AddDevice();
      break;
    case Command::REMOVE_DEVICE:
      success = RemoveDevice();
      break;

    case Command::PLUG:
      success = ChangePlugState(value, true);
      break;
    case Command::UNPLUG:
      success = ChangePlugState(value, false);
      break;
    case Command::GET_GAIN:
      success = GetGain();
      break;
    case Command::GET_FORMAT:
      success = GetFormat();
      break;
    case Command::RETRIEVE_BUFFER:
      success = GetBuffer();
      break;
    case Command::WRITE_BUFFER:
      success = WriteBuffer(value);
      break;
    case Command::GET_POSITION:
      success = GetPosition();
      break;
    case Command::SET_NOTIFICATION_FREQUENCY:
      success = SetNotificationFrequency(value);
      break;
    case Command::ADJUST_CLOCK_RATE:
      success = AdjustClockRate(value);
      break;

    case Command::SET_IN:
      configuring_output_ = false;
      success = true;
      break;
    case Command::SET_OUT:
      configuring_output_ = true;
      success = true;
      break;
    case Command::WAIT:
      success = WaitForKey();
      break;
    case Command::INVALID:
      success = false;
      break;

      // Intentionally omitting default so new enums are not forgotten here.
  }
  return success;
}

bool VirtualAudioUtil::Enable(bool enable) {
  if (!ConnectToController()) {
    return false;
  }

  if (enable) {
    controller_->Enable(EnableCallback);
  } else {
    controller_->Disable(DisableCallback);
  }

  return WaitForCallback();
}

bool VirtualAudioUtil::GetNumDevices() {
  if (!ConnectToController()) {
    return false;
  }

  controller_->GetNumDevices(NumDevicesCallback);

  return WaitForCallback();
}

bool VirtualAudioUtil::SetDeviceName(const std::string& name) {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->SetDeviceName(name == "" ? kDefaultDeviceName : name);
  } else {
    input_->SetDeviceName(name == "" ? kDefaultDeviceName : name);
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetManufacturer(const std::string& name) {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->SetManufacturer(name == "" ? kDefaultManufacturer : name);
  } else {
    input_->SetManufacturer(name == "" ? kDefaultManufacturer : name);
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetProductName(const std::string& name) {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->SetProduct(name == "" ? kDefaultProductName : name);
  } else {
    input_->SetProduct(name == "" ? kDefaultProductName : name);
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetUniqueId(const std::string& unique_id_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  std::array<uint8_t, 16> unique_id;
  bool use_default = (unique_id_str == "");

  for (uint8_t index = 0; index < 16; ++index) {
    unique_id[index] =
        use_default ? kDefaultUniqueId[index]
        : unique_id_str.size() <= (2 * index + 1)
            ? 0
            : fxl::StringToNumber<uint8_t>(unique_id_str.substr(index * 2, 2), fxl::Base::k16);
  }
  if (configuring_output_) {
    output_->SetUniqueId(unique_id);
  } else {
    input_->SetUniqueId(unique_id);
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetClockDomain(const std::string& clock_domain_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  auto clock_domain = (clock_domain_str == "" ? kDefaultClockDomain
                                              : fxl::StringToNumber<int32_t>(clock_domain_str));

  auto rate_adjustment_ppm =
      configuring_output_ ? output_rate_adjustment_ppm_ : input_rate_adjustment_ppm_;
  if (clock_domain == 0 && rate_adjustment_ppm != 0) {
    printf("WARNING: by definition, a clock in domain 0 should never have rate variance!\n");
  }

  if (configuring_output_) {
    output_clock_domain_ = clock_domain;
    output_->SetClockProperties(output_clock_domain_, output_rate_adjustment_ppm_);
  } else {
    input_clock_domain_ = clock_domain;
    input_->SetClockProperties(input_clock_domain_, input_rate_adjustment_ppm_);
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetInitialClockRate(const std::string& initial_clock_rate_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  auto rate_adjustment_ppm =
      (initial_clock_rate_str == "" ? kDefaultInitialClockRatePpm
                                    : fxl::StringToNumber<int32_t>(initial_clock_rate_str));

  auto clock_domain = configuring_output_ ? output_clock_domain_ : input_clock_domain_;
  if (clock_domain == 0 && rate_adjustment_ppm != 0) {
    printf("WARNING: by definition, a clock in domain 0 should never have rate variance!\n");
  }

  if (configuring_output_) {
    output_rate_adjustment_ppm_ = rate_adjustment_ppm;
    output_->SetClockProperties(output_clock_domain_, output_rate_adjustment_ppm_);
  } else {
    input_rate_adjustment_ppm_ = rate_adjustment_ppm;
    input_->SetClockProperties(input_clock_domain_, input_rate_adjustment_ppm_);
  }

  return WaitForNoCallback();
}

struct Format {
  uint32_t flags;
  uint32_t min_rate;
  uint32_t max_rate;
  uint32_t min_chans;
  uint32_t max_chans;
  uint32_t rate_family_flags;
};

// These formats exercise various scenarios:
// 0: full range of rates in both families (but not 48k), both 1-2 chans
// 1: float-only, 48k family extends to 96k, 2 or 4 chan
// 2: fixed 48k 2-chan 16b
// 3: 16k 2-chan 16b
// 4: 96k and 48k, 2-chan 16b
// 5: 3-chan device at 48k 16b
// 6: 1-chan device at 8k 16b
// 7: 1-chan device at 48k 16b
//
// Going forward, it would be best to have chans, rate and bitdepth specifiable individually.
constexpr Format kFormatSpecs[8] = {
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT | AUDIO_SAMPLE_FORMAT_24BIT_IN32,
     .min_rate = 8000,
     .max_rate = 44100,
     .min_chans = 1,
     .max_chans = 2,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_44100_FAMILY | ASF_RANGE_FLAG_FPS_48000_FAMILY},
    {.flags = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
     .min_rate = 32000,
     .max_rate = 96000,
     .min_chans = 2,
     .max_chans = 4,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_48000_FAMILY},
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT,
     .min_rate = 48000,
     .max_rate = 48000,
     .min_chans = 2,
     .max_chans = 2,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_CONTINUOUS},
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT,
     .min_rate = 16000,
     .max_rate = 16000,
     .min_chans = 2,
     .max_chans = 2,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_48000_FAMILY},
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT,
     .min_rate = 48000,
     .max_rate = 96000,
     .min_chans = 2,
     .max_chans = 2,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_48000_FAMILY},
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT,
     .min_rate = 48000,
     .max_rate = 48000,
     .min_chans = 3,
     .max_chans = 3,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_48000_FAMILY},
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT,
     .min_rate = 8000,
     .max_rate = 8000,
     .min_chans = 1,
     .max_chans = 1,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_CONTINUOUS},
    {.flags = AUDIO_SAMPLE_FORMAT_16BIT,
     .min_rate = 48000,
     .max_rate = 48000,
     .min_chans = 1,
     .max_chans = 1,
     .rate_family_flags = ASF_RANGE_FLAG_FPS_48000_FAMILY}};

bool VirtualAudioUtil::AddFormatRange(const std::string& format_range_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  uint8_t format_option = (format_range_str == "" ? kDefaultFormatRangeOption
                                                  : fxl::StringToNumber<uint8_t>(format_range_str));
  if (format_option >= std::size(kFormatSpecs)) {
    printf("Format range option must be %lu or less.\n", std::size(kFormatSpecs) - 1);
    return false;
  }

  if (configuring_output_) {
    output_->AddFormatRange(
        kFormatSpecs[format_option].flags, kFormatSpecs[format_option].min_rate,
        kFormatSpecs[format_option].max_rate, kFormatSpecs[format_option].min_chans,
        kFormatSpecs[format_option].max_chans, kFormatSpecs[format_option].rate_family_flags);
  } else {
    input_->AddFormatRange(
        kFormatSpecs[format_option].flags, kFormatSpecs[format_option].min_rate,
        kFormatSpecs[format_option].max_rate, kFormatSpecs[format_option].min_chans,
        kFormatSpecs[format_option].max_chans, kFormatSpecs[format_option].rate_family_flags);
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::ClearFormatRanges() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->ClearFormatRanges();
  } else {
    input_->ClearFormatRanges();
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetFifoDepth(const std::string& fifo_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  uint32_t fifo_depth =
      (fifo_str == "" ? kDefaultFifoDepth : fxl::StringToNumber<uint32_t>(fifo_str));
  if (configuring_output_) {
    output_->SetFifoDepth(fifo_depth);
  } else {
    input_->SetFifoDepth(fifo_depth);
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::SetExternalDelay(const std::string& delay_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  zx_duration_t external_delay =
      (delay_str == "" ? kDefaultExternalDelayNsec : fxl::StringToNumber<zx_duration_t>(delay_str));
  if (configuring_output_) {
    output_->SetExternalDelay(external_delay);
  } else {
    input_->SetExternalDelay(external_delay);
  }

  return WaitForNoCallback();
}

struct BufferSpec {
  uint32_t min_frames;
  uint32_t max_frames;
  uint32_t mod_frames;
};

// Buffer sizes (at default 48kHz rate): [0] 1.0-1.5 sec, in steps of 0.125;
// [1] 0.2-0.6 sec, in steps of 0.01;    [2] exactly 2 secs;    [3] exactly 6 secs.
constexpr BufferSpec kBufferSpecs[4] = {
    {.min_frames = 48000, .max_frames = 72000, .mod_frames = 6000},
    {.min_frames = 9600, .max_frames = 28800, .mod_frames = 480},
    {.min_frames = 96000, .max_frames = 96000, .mod_frames = 96000},
    {.min_frames = 288000, .max_frames = 288000, .mod_frames = 288000}};

bool VirtualAudioUtil::SetRingBufferRestrictions(const std::string& rb_restr_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  uint8_t rb_option =
      (rb_restr_str == "" ? kDefaultRingBufferOption : fxl::StringToNumber<uint8_t>(rb_restr_str));
  if (rb_option >= std::size(kBufferSpecs)) {
    printf("Ring buffer option must be %lu or less.\n", std::size(kBufferSpecs) - 1);
    return false;
  }

  if (configuring_output_) {
    output_->SetRingBufferRestrictions(kBufferSpecs[rb_option].min_frames,
                                       kBufferSpecs[rb_option].max_frames,
                                       kBufferSpecs[rb_option].mod_frames);
  } else {
    input_->SetRingBufferRestrictions(kBufferSpecs[rb_option].min_frames,
                                      kBufferSpecs[rb_option].max_frames,
                                      kBufferSpecs[rb_option].mod_frames);
  }

  return WaitForNoCallback();
}

struct GainSpec {
  bool cur_mute;
  bool cur_agc;
  float cur_gain_db;
  bool can_mute;
  bool can_agc;
  float min_gain_db;
  float max_gain_db;
  float gain_step_db;
};

// The utility defines two preset groups of gain options. Although arbitrarily chosen, they exercise
// the available range through SetGainProperties:
// 0.Can and is mute.    Cannot AGC.       Gain -2, range [-60, 0] in 2.0dB.
// 1.Can but isn't mute. Can AGC, enabled. Gain -7.5,range [-30,+2] in 0.5db.
// 2 and above represent invalid combinations.
constexpr GainSpec kGainSpecs[4] = {{.cur_mute = true,
                                     .cur_agc = false,
                                     .cur_gain_db = -2.0,
                                     .can_mute = true,
                                     .can_agc = false,
                                     .min_gain_db = -60.0,
                                     .max_gain_db = 0.0,
                                     .gain_step_db = 2.0},
                                    {.cur_mute = false,
                                     .cur_agc = true,
                                     .cur_gain_db = -7.5,
                                     .can_mute = true,
                                     .can_agc = true,
                                     .min_gain_db = -30.0,
                                     .max_gain_db = 2.0,
                                     .gain_step_db = 0.5},
                                    {.cur_mute = true,
                                     .cur_agc = true,
                                     .cur_gain_db = -12.0,
                                     .can_mute = false,
                                     .can_agc = false,
                                     .min_gain_db = -96.0,
                                     .max_gain_db = 0.0,
                                     .gain_step_db = 1.0},
                                    {.cur_mute = false,
                                     .cur_agc = false,
                                     .cur_gain_db = 50.0,
                                     .can_mute = true,
                                     .can_agc = false,
                                     .min_gain_db = 20.0,
                                     .max_gain_db = -20.0,
                                     .gain_step_db = -3.0}};

bool VirtualAudioUtil::SetGainProps(const std::string& gain_props_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  uint8_t gain_props_option = (gain_props_str == "" ? kDefaultGainPropsOption
                                                    : fxl::StringToNumber<uint8_t>(gain_props_str));
  if (gain_props_option >= std::size(kGainSpecs)) {
    printf("Gain properties option must be %lu or less.\n", std::size(kGainSpecs));
    return false;
  }

  if (configuring_output_) {
    output_->SetGainProperties(
        kGainSpecs[gain_props_option].min_gain_db, kGainSpecs[gain_props_option].max_gain_db,
        kGainSpecs[gain_props_option].gain_step_db, kGainSpecs[gain_props_option].cur_gain_db,
        kGainSpecs[gain_props_option].can_mute, kGainSpecs[gain_props_option].cur_mute,
        kGainSpecs[gain_props_option].can_agc, kGainSpecs[gain_props_option].cur_agc);
  } else {
    input_->SetGainProperties(
        kGainSpecs[gain_props_option].min_gain_db, kGainSpecs[gain_props_option].max_gain_db,
        kGainSpecs[gain_props_option].gain_step_db, kGainSpecs[gain_props_option].cur_gain_db,
        kGainSpecs[gain_props_option].can_mute, kGainSpecs[gain_props_option].cur_mute,
        kGainSpecs[gain_props_option].can_agc, kGainSpecs[gain_props_option].cur_agc);
  }

  return WaitForNoCallback();
}

// These preset options represent the following common configurations:
// 0.(Default) Hot-pluggable;   1.Hardwired;    2.Hot-pluggable, unplugged;
// 3.Plugged (synch: detected only by polling); 4.Unplugged (synch)
constexpr audio_pd_notify_flags_t kPlugFlags[] = {
    AUDIO_PDNF_PLUGGED /*AUDIO_PDNF_HARDWIRED*/ | AUDIO_PDNF_CAN_NOTIFY,
    AUDIO_PDNF_PLUGGED | AUDIO_PDNF_HARDWIRED /*  AUDIO_PDNF_CAN_NOTIFY*/,
    /*AUDIO_PDNF_PLUGGED AUDIO_PDNF_HARDWIRED  */ AUDIO_PDNF_CAN_NOTIFY,
    AUDIO_PDNF_PLUGGED /*AUDIO_PDNF_HARDWIRED     AUDIO_PDNF_CAN_NOTIFY*/,
    0 /*AUDIO_PDNF_PLUGGED AUDIO_PDNF_HARDWIRED   AUDIO_PDNF_CAN_NOTIFY*/
};

constexpr zx_time_t kPlugTime[] = {0, -1, -1, ZX_SEC(1), ZX_SEC(2)};
static_assert(std::size(kPlugFlags) == std::size(kPlugTime));

bool VirtualAudioUtil::SetPlugProps(const std::string& plug_props_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  uint8_t plug_props_option = (plug_props_str == "" ? kDefaultPlugPropsOption
                                                    : fxl::StringToNumber<uint8_t>(plug_props_str));

  if (plug_props_option >= std::size(kPlugFlags)) {
    printf("Plug properties option must be %lu or less.\n", std::size(kPlugFlags) - 1);
    return false;
  }

  auto plug_change_time = (kPlugTime[plug_props_option] == -1 ? zx::clock::get_monotonic().get()
                                                              : kPlugTime[plug_props_option]);
  bool plugged = (kPlugFlags[plug_props_option] & AUDIO_PDNF_PLUGGED);
  bool hardwired = (kPlugFlags[plug_props_option] & AUDIO_PDNF_HARDWIRED);
  bool can_notify = (kPlugFlags[plug_props_option] & AUDIO_PDNF_CAN_NOTIFY);

  if (configuring_output_) {
    output_->SetPlugProperties(plug_change_time, plugged, hardwired, can_notify);
  } else {
    input_->SetPlugProperties(plug_change_time, plugged, hardwired, can_notify);
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::AdjustClockRate(const std::string& clock_adjust_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  auto rate_adjustment_ppm = fxl::StringToNumber<int32_t>(clock_adjust_str);
  if (rate_adjustment_ppm < ZX_CLOCK_UPDATE_MIN_RATE_ADJUST ||
      rate_adjustment_ppm > ZX_CLOCK_UPDATE_MAX_RATE_ADJUST) {
    printf("Clock rate adjustment must be within [%d, %d].\n", ZX_CLOCK_UPDATE_MIN_RATE_ADJUST,
           ZX_CLOCK_UPDATE_MAX_RATE_ADJUST);
    return false;
  }

  auto clock_domain = configuring_output_ ? output_clock_domain_ : input_clock_domain_;
  if (clock_domain == 0 && rate_adjustment_ppm != 0) {
    printf("WARNING: by definition, a clock in domain 0 should never have rate variance!\n");
  }

  if (configuring_output_) {
    output_->AdjustClockRate(rate_adjustment_ppm);
  } else {
    input_->AdjustClockRate(rate_adjustment_ppm);
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::ResetConfiguration() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->ResetConfiguration();
  } else {
    input_->ResetConfiguration();
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::AddDevice() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->Add();
  } else {
    input_->Add();
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::RemoveDevice() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->Remove();
  } else {
    input_->Remove();
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::ChangePlugState(const std::string& plug_time_str, bool plugged) {
  if (!ConnectToDevice()) {
    return false;
  }

  auto plug_change_time = (plug_time_str == "" ? zx::clock::get_monotonic().get()
                                               : fxl::StringToNumber<zx_time_t>(plug_time_str));

  if (configuring_output_) {
    output_->ChangePlugState(plug_change_time, plugged);
  } else {
    input_->ChangePlugState(plug_change_time, plugged);
  }

  return WaitForNoCallback();
}

bool VirtualAudioUtil::GetFormat() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->GetFormat(FormatCallback<true>);
  } else {
    input_->GetFormat(FormatCallback<false>);
  }

  return WaitForCallback();
}

bool VirtualAudioUtil::GetGain() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->GetGain(GainCallback<true>);
  } else {
    input_->GetGain(GainCallback<false>);
  }

  return WaitForCallback();
}

bool VirtualAudioUtil::GetBuffer() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->GetBuffer(BufferCallback<true>);
  } else {
    input_->GetBuffer(BufferCallback<false>);
  }

  return WaitForCallback() && ring_buffer_vmo_.is_valid();
}

bool VirtualAudioUtil::WriteBuffer(const std::string& write_value_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  size_t value_to_write =
      (write_value_str == "" ? kDefaultValueToWrite : fxl::StringToNumber<size_t>(write_value_str));

  if (!ring_buffer_vmo_.is_valid()) {
    if (!GetBuffer()) {
      return false;
    }
  }

  for (size_t offset = 0; offset < ring_buffer_size_; offset += sizeof(value_to_write)) {
    auto status = ring_buffer_vmo_.write(&value_to_write, offset, sizeof(value_to_write));
    if (status != ZX_OK) {
      printf("Writing 0x%016zX to rb_vmo[%zu] failed (%d)\n", value_to_write, offset, status);
      return false;
    }
  }
  return WaitForNoCallback();
}

bool VirtualAudioUtil::GetPosition() {
  if (!ConnectToDevice()) {
    return false;
  }

  if (configuring_output_) {
    output_->GetPosition(PositionCallback<true>);
  } else {
    input_->GetPosition(PositionCallback<false>);
  }

  return WaitForCallback();
}

bool VirtualAudioUtil::SetNotificationFrequency(const std::string& notifs_str) {
  if (!ConnectToDevice()) {
    return false;
  }

  uint32_t notifications_per_ring = (notifs_str == "" ? kDefaultNotificationFrequency
                                                      : fxl::StringToNumber<uint32_t>(notifs_str));
  if (configuring_output_) {
    output_->SetNotificationFrequency(notifications_per_ring);
  } else {
    input_->SetNotificationFrequency(notifications_per_ring);
  }

  return WaitForNoCallback();
}

void VirtualAudioUtil::CallbackReceived() {
  VirtualAudioUtil::received_callback_ = true;
  VirtualAudioUtil::loop_->Quit();
}

void VirtualAudioUtil::EnableCallback() {
  VirtualAudioUtil::CallbackReceived();

  printf("--Received Enable callback\n");
}

void VirtualAudioUtil::DisableCallback() {
  VirtualAudioUtil::CallbackReceived();

  printf("--Received Disable callback\n");
}

void VirtualAudioUtil::NumDevicesCallback(uint32_t num_inputs, uint32_t num_outputs) {
  VirtualAudioUtil::CallbackReceived();

  printf("--Received NumDevices (%u inputs, %u outputs)\n", num_inputs, num_outputs);
}

template <bool is_out>
void VirtualAudioUtil::FormatNotification(uint32_t fps, uint32_t fmt, uint32_t chans,
                                          zx_duration_t delay) {
  printf("--Received Format (%u fps, %x fmt, %u chan, %zu delay) for %s\n", fps, fmt, chans, delay,
         (is_out ? "output" : "input"));

  DeviceType dev_type = is_out ? kOutput : kInput;
  frame_size_[dev_type] = chans * BytesPerSample(fmt);
  ref_time_to_running_position_rate_[dev_type] =
      media::TimelineRate(fps * frame_size_[dev_type], ZX_SEC(1));
}
template <bool is_out>
void VirtualAudioUtil::FormatCallback(uint32_t fps, uint32_t fmt, uint32_t chans,
                                      zx_duration_t delay) {
  VirtualAudioUtil::CallbackReceived();
  VirtualAudioUtil::FormatNotification<is_out>(fps, fmt, chans, delay);
}

template <bool is_out>
void VirtualAudioUtil::GainNotification(bool mute, bool agc, float gain_db) {
  printf("--Received Gain   (mute: %u, agc: %u, gain: %.5f dB) for %s\n", mute, agc, gain_db,
         (is_out ? "output" : "input"));
}
template <bool is_out>
void VirtualAudioUtil::GainCallback(bool mute, bool agc, float gain_db) {
  VirtualAudioUtil::CallbackReceived();
  VirtualAudioUtil::GainNotification<is_out>(mute, agc, gain_db);
}

template <bool is_out>
void VirtualAudioUtil::BufferNotification(zx::vmo ring_buffer_vmo, uint32_t num_ring_buffer_frames,
                                          uint32_t notifications_per_ring) {
  ring_buffer_vmo_ = std::move(ring_buffer_vmo);
  uint64_t vmo_size;
  ring_buffer_vmo_.get_size(&vmo_size);
  DeviceType dev_type = is_out ? kOutput : kInput;
  rb_size_[dev_type] = num_ring_buffer_frames * frame_size_[dev_type];

  printf("--Received SetBuffer (vmo size: %zu, ring size: %zu, frames: %u, notifs: %u) for %s\n",
         vmo_size, rb_size_[dev_type], num_ring_buffer_frames, notifications_per_ring,
         (is_out ? "output" : "input"));
}
template <bool is_out>
void VirtualAudioUtil::BufferCallback(zx::vmo buff, uint32_t rb_frames, uint32_t notifs) {
  VirtualAudioUtil::CallbackReceived();
  VirtualAudioUtil::BufferNotification<is_out>(std::move(buff), rb_frames, notifs);
}

void VirtualAudioUtil::UpdateRunningPosition(uint32_t rb_pos, bool is_output) {
  auto dev_type = is_output ? kOutput : kInput;

  if (rb_pos <= last_rb_position_[dev_type]) {
    running_position_[dev_type] += rb_size_[dev_type];
  }
  running_position_[dev_type] -= last_rb_position_[dev_type];
  running_position_[dev_type] += rb_pos;
  last_rb_position_[dev_type] = rb_pos;
}

template <bool is_out>
void VirtualAudioUtil::StartNotification(zx_time_t start_time) {
  printf("--Received Start    (time: %zu) for %s\n", start_time, (is_out ? "output" : "input"));

  DeviceType dev_type = is_out ? kOutput : kInput;
  ref_time_to_running_position_[dev_type] =
      media::TimelineFunction(0, start_time, ref_time_to_running_position_rate_[dev_type]);

  running_position_[dev_type] = 0;
  last_rb_position_[dev_type] = 0;
}

template <bool is_out>
void VirtualAudioUtil::StopNotification(zx_time_t stop_time, uint32_t rb_pos) {
  DeviceType dev_type = is_out ? kOutput : kInput;
  auto expected_running_position = ref_time_to_running_position_[dev_type].Apply(stop_time);
  UpdateRunningPosition(rb_pos, is_out);

  printf("--Received Stop     (time: %zu, pos: %u) for %s\n", stop_time, rb_pos,
         (is_out ? "output" : "input"));
  printf("--Stop at  position: expected %zu; actual %zu\n", expected_running_position,
         running_position_[dev_type]);

  running_position_[dev_type] = 0;
  last_rb_position_[dev_type] = 0;
}

template <bool is_out>
void VirtualAudioUtil::PositionNotification(zx_time_t time_for_pos, uint32_t rb_pos) {
  printf("--Received Position (time: %13zu, pos: %6u) for %s", time_for_pos, rb_pos,
         (is_out ? "output" : "input "));

  DeviceType dev_type = is_out ? kOutput : kInput;
  if (time_for_pos > ref_time_to_running_position_[dev_type].reference_time()) {
    auto expected_running_position = ref_time_to_running_position_[dev_type].Apply(time_for_pos);

    UpdateRunningPosition(rb_pos, is_out);
    int64_t delta = expected_running_position - running_position_[dev_type];
    printf(" - running byte position: expect %8zu  actual %8zu  delta %6zd",
           expected_running_position, running_position_[dev_type], delta);
  }
  printf("\n");
}
template <bool is_out>
void VirtualAudioUtil::PositionCallback(zx_time_t time_for_pos, uint32_t rb_pos) {
  VirtualAudioUtil::CallbackReceived();
  VirtualAudioUtil::PositionNotification<is_out>(time_for_pos, rb_pos);
}

}  // namespace virtual_audio

int main(int argc, const char** argv) {
  syslog::SetTags({"virtual_audio_util"});

  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  virtual_audio::VirtualAudioUtil util(&loop);
  util.Run(&command_line);

  return 0;
}
