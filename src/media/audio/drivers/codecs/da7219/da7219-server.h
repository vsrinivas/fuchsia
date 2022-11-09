// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_SERVER_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_SERVER_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/irq.h>
#include <lib/zx/interrupt.h>

#ifdef LOGGING_PATH
#include LOGGING_PATH
#else
#error "Must have logging path"
#endif

namespace audio::da7219 {

// Only one core for both server instantiations since the is one hardware servicing both
// an input and an output servers.
class Core {
 public:
  explicit Core(Logger* logger, fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c,
                zx::interrupt irq);
  ~Core() = default;

  using PlugCallback = fit::function<void(bool)>;

  void Shutdown();
  fidl::ClientEnd<fuchsia_hardware_i2c::Device>& i2c() { return i2c_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  zx_status_t Reset();
  zx_status_t Initialize();
  void AddPlugCallback(bool is_input, PlugCallback cb);

 private:
  static constexpr async_loop_config_t MakeConfig() {
    async_loop_config_t config = kAsyncLoopConfigNeverAttachToThread;
    config.irq_support = true;
    return config;
  }

  void HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq, zx_status_t status,
                 const zx_packet_interrupt_t* interrupt);
  void PlugDetected(bool plugged, bool with_mic);

  Logger* logger_;
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c_;
  zx::interrupt irq_;
  async::IrqMethod<Core, &Core::HandleIrq> irq_handler_{this};
  async_loop_config_t config_;
  async::Loop loop_;
  std::optional<PlugCallback> plug_callback_input_;
  std::optional<PlugCallback> plug_callback_output_;
};

class Server : public fidl::WireServer<fuchsia_hardware_audio::Codec> {
 public:
  explicit Server(Logger* logger, std::shared_ptr<Core> core, bool is_input);
  async_dispatcher_t* dispatcher() { return core_->dispatcher(); }

 private:
  // LLCPP implementation for the Codec API.
  void Reset(ResetCompleter::Sync& completer) override;
  void Stop(StopCompleter::Sync& completer) override;
  void Start(StartCompleter::Sync& completer) override;
  void GetInfo(GetInfoCompleter::Sync& completer) override;
  void GetHealthState(GetHealthStateCompleter::Sync& completer) override;
  void IsBridgeable(IsBridgeableCompleter::Sync& completer) override;
  void SetBridgedMode(SetBridgedModeRequestView request,
                      SetBridgedModeCompleter::Sync& completer) override;
  void GetDaiFormats(GetDaiFormatsCompleter::Sync& completer) override;
  void SetDaiFormat(SetDaiFormatRequestView request,
                    SetDaiFormatCompleter::Sync& completer) override;
  void GetPlugDetectCapabilities(GetPlugDetectCapabilitiesCompleter::Sync& completer) override;
  void WatchPlugState(WatchPlugStateCompleter::Sync& completer) override;
  void SignalProcessingConnect(SignalProcessingConnectRequestView request,
                               SignalProcessingConnectCompleter::Sync& completer) override;

  Logger* logger_;
  std::shared_ptr<Core> core_;
  bool is_input_;

  // Plug state. Must reply to the first Watch request, if there is no plug state update before the
  // first Watch, reply with unplugged at time 0.
  bool plugged_ = false;
  zx::time plugged_time_;
  bool plug_state_updated_ = true;
  std::optional<WatchPlugStateCompleter::Async> plug_state_completer_;
};

}  // namespace audio::da7219

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_SERVER_H_
