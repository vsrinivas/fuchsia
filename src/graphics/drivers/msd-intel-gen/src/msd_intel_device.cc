// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"

#include <bitset>
#include <cstdio>
#include <iterator>
#include <string>

#include <fbl/string_printf.h>

#include "cache_config.h"
#include "device_id.h"
#include "forcewake.h"
#include "magma_intel_gen_defs.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_intel_semaphore.h"
#include "platform_trace.h"
#include "registers.h"

inline uint64_t get_current_time_ns() {
  return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now())
      .time_since_epoch()
      .count();
}

class MsdIntelDevice::BatchRequest : public DeviceRequest {
 public:
  BatchRequest(std::unique_ptr<MappedBatch> batch) : batch_(std::move(batch)) {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessBatch(std::move(batch_));
  }

 private:
  std::unique_ptr<MappedBatch> batch_;
};

class MsdIntelDevice::DestroyContextRequest : public DeviceRequest {
 public:
  DestroyContextRequest(std::shared_ptr<MsdIntelContext> client_context)
      : client_context_(std::move(client_context)) {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessDestroyContext(std::move(client_context_));
  }

 private:
  std::shared_ptr<MsdIntelContext> client_context_;
};

class MsdIntelDevice::InterruptRequest : public DeviceRequest {
 public:
  InterruptRequest(uint64_t interrupt_time_ns, uint32_t render_interrupt_status,
                   uint32_t video_interrupt_status)
      : interrupt_time_ns_(interrupt_time_ns),
        render_interrupt_status_(render_interrupt_status),
        video_interrupt_status_(video_interrupt_status) {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessInterrupts(interrupt_time_ns_, render_interrupt_status_,
                                     video_interrupt_status_);
  }
  uint64_t interrupt_time_ns_;
  uint32_t render_interrupt_status_;
  uint32_t video_interrupt_status_;
};

class MsdIntelDevice::DumpRequest : public DeviceRequest {
 public:
  DumpRequest() {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessDumpStatusToLog();
  }
};

class MsdIntelDevice::TimestampRequest : public DeviceRequest {
 public:
  TimestampRequest(std::shared_ptr<magma::PlatformBuffer> buffer) : buffer_(std::move(buffer)) {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessTimestampRequest(std::move(buffer_));
  }

  std::shared_ptr<magma::PlatformBuffer> buffer_;
};

class MsdIntelDevice::Topology : public magma_intel_gen_topology {
 public:
  std::vector<uint8_t> mask_data;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdIntelDevice> MsdIntelDevice::Create(void* device_handle,
                                                       bool start_device_thread) {
  std::unique_ptr<MsdIntelDevice> device(new MsdIntelDevice());

  if (!device->Init(device_handle))
    return DRETP(nullptr, "Failed to initialize MsdIntelDevice");

  if (start_device_thread) {
    if (!device->StartDeviceThread())
      return DRETP(nullptr, "Failed to start device thread");
  }

  return device;
}

MsdIntelDevice::MsdIntelDevice() { magic_ = kMagic; }

MsdIntelDevice::~MsdIntelDevice() { Destroy(); }

std::pair<magma_intel_gen_topology*, uint8_t*> MsdIntelDevice::GetTopology() {
  return {topology_.get(), topology_->mask_data.data()};
}

void MsdIntelDevice::Destroy() {
  DLOG("Destroy");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  device_thread_quit_flag_ = true;

  if (device_request_semaphore_)
    device_request_semaphore_->Signal();

  if (device_thread_.joinable()) {
    DLOG("joining device thread");
    device_thread_.join();
    DLOG("joined");
  }

  if (render_engine_cs_) {
    render_engine_cs_->Reset();
  }
  if (video_command_streamer_) {
    video_command_streamer_->Reset();
  }

  // Hardware interrupts disabled when device thread exits
  interrupt_manager_.reset();
}

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id_t client_id) {
  return MsdIntelConnection::Create(this, client_id);
}

bool MsdIntelDevice::Init(void* device_handle) {
  if (!BaseInit(device_handle))
    return DRETF(false, "BaseInit failed");

  InitEngine(render_engine_cs());
  InitEngine(video_command_streamer());

  if (DeviceId::is_gen12(device_id())) {
    if (!CacheConfig::InitCacheConfigGen12(register_io()))
      return DRETF(false, "failed to init cache config");
  }

  return true;
}

bool MsdIntelDevice::BaseInit(void* device_handle) {
  DASSERT(!platform_device_);

  DLOG("Init device_handle %p", device_handle);

  platform_device_ = MsdIntelPciDevice::CreateShim(device_handle);
  if (!platform_device_)
    return DRETF(false, "failed to create pci device");

  uint16_t pci_dev_id;
  if (!platform_device_->ReadPciConfig16(2, &pci_dev_id))
    return DRETF(false, "ReadPciConfig16 failed");

  uint16_t revision;
  if (!platform_device_->ReadPciConfig16(8, &revision))
    return DRETF(false, "ReadPciConfig16 failed");

  revision_ = revision & 0xFF;

  device_id_ = pci_dev_id;
  DLOG("device_id 0x%x revision 0x%x", device_id_, revision);

  std::unique_ptr<magma::PlatformMmio> mmio(
      platform_device_->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
  if (!mmio)
    return DRETF(false, "failed to map pci bar 0");

  register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));

  if (!DeviceId::is_gen9(device_id_) && !DeviceId::is_gen12(device_id_)) {
    MAGMA_LOG(WARNING, "Unrecognized graphics PCI device id 0x%x", device_id_);
    return false;
  }

  ForceWake::reset(register_io_.get(), registers::ForceWake::RENDER);
  ForceWake::request(register_io_.get(), registers::ForceWake::RENDER);

  bus_mapper_ = magma::PlatformBusMapper::Create(platform_device_->GetBusTransactionInitiator());
  if (!bus_mapper_)
    return DRETF(false, "failed to create bus mapper");

  // Clear faults
  registers::AllEngineFault::GetAddr(device_id_)
      .FromValue(0)
      .set_valid(0)
      .WriteTo(register_io_.get());

  topology_ = std::make_unique<Topology>();

  if (DeviceId::is_gen12(device_id())) {
    QuerySliceInfoGen12(&subslice_total_, &eu_total_, topology_.get());

    PerProcessGtt::InitPrivatePatGen12(register_io_.get());
  } else {
    QuerySliceInfoGen9(&subslice_total_, &eu_total_, topology_.get());

    PerProcessGtt::InitPrivatePat(register_io_.get());
  }

  interrupt_manager_ = InterruptManager::CreateShim(this);
  if (!interrupt_manager_)
    return DRETF(false, "failed to create interrupt manager");

  gtt_ = std::shared_ptr<Gtt>(Gtt::CreateShim(this));

  // Arbitrary
  constexpr uint32_t kFirstSequenceNumber = 0x1000;
  sequencer_ = std::unique_ptr<Sequencer>(new Sequencer(kFirstSequenceNumber));

  {
    auto mapping =
        AddressSpace::MapBufferGpu(gtt_, MsdIntelBuffer::Create(magma::page_size(), "RCS HWSP"));
    if (!mapping)
      return DRETF(false, "MapBufferGpu failed for RCS HWSP");

    render_engine_cs_ = std::make_unique<RenderEngineCommandStreamer>(this, std::move(mapping));
  }

  {
    auto mapping =
        AddressSpace::MapBufferGpu(gtt_, MsdIntelBuffer::Create(magma::page_size(), "VCS HWSP"));
    if (!mapping)
      return DRETF(false, "MapBufferGpu failed for VCS HWSP");

    video_command_streamer_ = std::make_unique<VideoCommandStreamer>(this, std::move(mapping));
  }

  global_context_ = std::shared_ptr<MsdIntelContext>(new MsdIntelContext(gtt_));

  indirect_context_batch_ = render_engine_cs_->CreateIndirectContextBatch(gtt_);

  // Creates the context backing store.
  // Global context used to execute the render init batch.
  if (!InitContextForEngine(global_context_.get(), render_engine_cs_.get()))
    return DRETF(false, "Failed to init global context for RCS");

  device_request_semaphore_ = magma::PlatformSemaphore::Create();

  CheckEngines();

  return true;
}

void MsdIntelDevice::EnableInterrupts(EngineCommandStreamer* engine, bool enable) {
  auto mask_op =
      enable ? registers::InterruptRegisterBase::UNMASK : registers::InterruptRegisterBase::MASK;

  constexpr auto kBits = registers::InterruptRegisterBase::kUserBit |
                         registers::InterruptRegisterBase::kContextSwitchBit;

  if (DeviceId::is_gen12(device_id())) {
    switch (engine->id()) {
      case RENDER_COMMAND_STREAMER:
        registers::GtInterruptMask0Gen12::mask_render(register_io(), mask_op, kBits);
        registers::GtInterruptEnable0Gen12::enable_render(register_io(), enable, kBits);
        break;

      case VIDEO_COMMAND_STREAMER:
        registers::GtInterruptMask2Gen12::mask_vcs0(register_io(), mask_op, kBits);
        registers::GtInterruptEnable1Gen12::enable_video_decode(register_io(), enable, kBits);
        break;
    }
  } else {
    DASSERT(DeviceId::is_gen9(device_id()));

    switch (engine->id()) {
      case RENDER_COMMAND_STREAMER:
        registers::GtInterruptMask0::mask_render(register_io(), mask_op, kBits);
        registers::GtInterruptEnable0::enable_render(register_io(), enable, kBits);
        break;

      case VIDEO_COMMAND_STREAMER:
        registers::GtInterruptMask1::mask_vcs0(register_io(), mask_op, kBits);
        registers::GtInterruptEnable1::enable_vcs0(register_io(), enable, kBits);
        break;
    }
  }
}

void MsdIntelDevice::InitEngine(EngineCommandStreamer* engine) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  engine->InitHardware();

  // Top level (not engine specific) workarounds.
  switch (engine->id()) {
    case RENDER_COMMAND_STREAMER:
      if (DeviceId::is_gen9(device_id())) {
        // WaEnableGapsTsvCreditFix
        registers::ArbiterControl::workaround(register_io());
      }
      break;
    case VIDEO_COMMAND_STREAMER:
      break;
  }
}

bool MsdIntelDevice::RenderInitBatch() {
  if (DeviceId::is_gen12(device_id_))
    return true;

  auto init_batch = render_engine_cs_->CreateRenderInitBatch(device_id_);
  if (!init_batch)
    return DRETF(false, "failed to create render init batch");

  if (!render_engine_cs_->RenderInit(global_context_, std::move(init_batch), gtt_))
    return DRETF(false, "render_engine_cs failed RenderInit");

  return true;
}

bool MsdIntelDevice::EngineReset(EngineCommandStreamer* engine) {
  MAGMA_LOG(WARNING, "resetting engine %s", engine->Name());

  engine->ResetCurrentContext();

  InitEngine(engine);

  registers::AllEngineFault::GetAddr(device_id_)
      .FromValue(0)
      .set_valid(0)
      .WriteTo(register_io_.get());

  if (engine->id() == RENDER_COMMAND_STREAMER) {
    if (!RenderInitBatch())
      return false;
  }

  return true;
}

bool MsdIntelDevice::StartDeviceThread() {
  DASSERT(!device_thread_.joinable());
  device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });

  // Don't start interrupt processing until the device thread is running.
  uint32_t mask = DeviceId::is_gen9(device_id())
                      ? registers::MasterInterruptControl::kRenderInterruptsPendingBitMask |
                            registers::MasterInterruptControl::kVideoInterruptsPendingBitMask
                      : 0;

  return interrupt_manager_->RegisterCallback(InterruptCallback, this, mask);
}

void MsdIntelDevice::InterruptCallback(void* data, uint32_t master_interrupt_control,
                                       uint64_t interrupt_timestamp) {
  DASSERT(data);
  auto device = reinterpret_cast<MsdIntelDevice*>(data);

  device->last_interrupt_callback_timestamp_ = magma::get_monotonic_ns();
  device->last_interrupt_timestamp_ = interrupt_timestamp;

  // We're running in the core driver's interrupt thread.
  magma::RegisterIo* register_io = device->register_io_for_interrupt();

  uint64_t now = get_current_time_ns();
  uint32_t render_interrupt_status = 0;
  uint32_t video_interrupt_status = 0;

  if (DeviceId::is_gen12(device->device_id())) {
    if (auto status = registers::GtInterruptStatus0Gen12::Get(register_io); status.reg_value()) {
      if (status.rcs0()) {
        // Select the engine for the identity register.
        registers::GtInterruptSelector0Gen12::write_rcs0(register_io);

        auto identity = registers::GtInterruptIdentityGen12::GetBank0(register_io);

        if (identity.SpinUntilValid(register_io, std::chrono::microseconds(100))) {
          DASSERT(identity.data_valid());
          DASSERT(identity.instance_id() == 0);
          DASSERT(identity.class_id() == 0);

          render_interrupt_status = identity.interrupt();

          identity.Clear(register_io);
        } else {
          MAGMA_LOG(WARNING, "RCS interrupt identity invalid");
        }
      }

      status.WriteTo(register_io);  // clear
    }

    if (auto status = registers::GtInterruptStatus1Gen12::Get(register_io); status.reg_value()) {
      if (status.vcs0()) {
        // Select the engine for the identity register.
        registers::GtInterruptSelector1Gen12::write_vcs0(register_io);

        auto identity = registers::GtInterruptIdentityGen12::GetBank1(register_io);

        if (identity.SpinUntilValid(register_io, std::chrono::microseconds(100))) {
          DASSERT(identity.data_valid());
          DASSERT(identity.instance_id() == 0);
          DASSERT(identity.class_id() == 1);

          video_interrupt_status = identity.interrupt();

          identity.Clear(register_io);
        } else {
          MAGMA_LOG(WARNING, "VCS0 interrupt identity invalid");
        }
      }

      status.WriteTo(register_io);  // clear
    }
  } else {
    DASSERT(DeviceId::is_gen9(device->device_id()));

    if (master_interrupt_control &
        registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {
      render_interrupt_status = registers::GtInterruptIdentity0::read(register_io);
      DLOG("gt IIR0 0x%08x", render_interrupt_status);

      if (render_interrupt_status & registers::InterruptRegisterBase::kUserBit) {
        registers::GtInterruptIdentity0::clear(register_io,
                                               registers::InterruptRegisterBase::kUserBit);
      }
      if (render_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
        registers::GtInterruptIdentity0::clear(register_io,
                                               registers::InterruptRegisterBase::kContextSwitchBit);
      }
    }

    if (master_interrupt_control &
        registers::MasterInterruptControl::kVideoInterruptsPendingBitMask) {
      video_interrupt_status = registers::GtInterruptIdentity1::read(register_io);
      DLOG("gt IIR1 0x%08x", video_interrupt_status);

      if (video_interrupt_status & registers::InterruptRegisterBase::kUserBit) {
        registers::GtInterruptIdentity1::clear(register_io,
                                               registers::InterruptRegisterBase::kUserBit);
      }
      if (video_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
        registers::GtInterruptIdentity1::clear(register_io,
                                               registers::InterruptRegisterBase::kContextSwitchBit);
      }
    }
  }

  if (render_interrupt_status || video_interrupt_status) {
    device->EnqueueDeviceRequest(
        std::make_unique<InterruptRequest>(now, render_interrupt_status, video_interrupt_status));
  }
}

void MsdIntelDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

void MsdIntelDevice::SubmitBatch(std::unique_ptr<MappedBatch> batch) {
  DLOG("SubmitBatch");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  EnqueueDeviceRequest(std::make_unique<BatchRequest>(std::move(batch)));
}

void MsdIntelDevice::DestroyContext(std::shared_ptr<MsdIntelContext> client_context) {
  DLOG("DestroyContext");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  EnqueueDeviceRequest(std::make_unique<DestroyContextRequest>(std::move(client_context)));
}

//////////////////////////////////////////////////////////////////////////////////////////////////

void MsdIntelDevice::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request,
                                          bool enqueue_front) {
  TRACE_DURATION("magma", "EnqueueDeviceRequest");
  std::unique_lock<std::mutex> lock(device_request_mutex_);
  if (enqueue_front) {
    device_request_list_.emplace_front(std::move(request));
  } else {
    device_request_list_.emplace_back(std::move(request));
  }
  device_request_semaphore_->Signal();
}

#ifdef HANGCHECK_TIMEOUT_MS
constexpr uint32_t kHangcheckTimeoutMs = HANGCHECK_TIMEOUT_MS;
#else
constexpr uint32_t kHangcheckTimeoutMs = 1000;
#endif

constexpr uint32_t kFreqPollPeriodMs = 16;

std::chrono::milliseconds MsdIntelDevice::GetDeviceRequestTimeoutMs(
    const std::vector<EngineCommandStreamer*>& engines) {
  auto timeout = std::chrono::steady_clock::duration::max();

  for (auto& engine : engines) {
    timeout = std::min(timeout, engine->progress()->GetHangcheckTimeout(
                                    kHangcheckTimeoutMs, std::chrono::steady_clock::now()));
  }

  std::chrono::milliseconds timeout_ms =
      std::max(std::chrono::milliseconds(0), std::chrono::ceil<std::chrono::milliseconds>(timeout));

  if (timeout != std::chrono::steady_clock::duration::max()) {
    timeout_ms = std::min(timeout_ms, std::chrono::milliseconds(kFreqPollPeriodMs));
  }

  return timeout_ms;
}

void MsdIntelDevice::DeviceRequestTimedOut(const std::vector<EngineCommandStreamer*>& engines) {
  // Sometimes the interrupt thread has been observed to be massively delayed in
  // responding to a pending interrupt.  In that case the InterruptRequest can be posted
  // after the timeout has expired, so always check if there is work to do before jumping
  // to conclusions.
  {
    std::unique_lock<std::mutex> lock(device_request_mutex_);
    if (!device_request_list_.empty())
      return;
  }

  auto now = std::chrono::steady_clock::now();

  for (auto& engine : engines) {
    if (engine->progress()->GetHangcheckTimeout(kHangcheckTimeoutMs, now) <=
        std::chrono::steady_clock::duration::zero()) {
      HangCheckTimeout(kHangcheckTimeoutMs, engine->id());
    }
  }
}

void MsdIntelDevice::TraceFreq(std::chrono::steady_clock::time_point& last_freq_poll_time) {
  auto now = std::chrono::steady_clock::now();

  if (std::chrono::ceil<std::chrono::milliseconds>(now - last_freq_poll_time).count() >=
      kFreqPollPeriodMs) {
    last_freq_poll_time = now;

    if (TRACE_ENABLED()) {
      uint32_t ATTRIBUTE_UNUSED actual_mhz =
          registers::RenderPerformanceStatus::read_current_frequency_gen9(register_io_.get());
      uint32_t ATTRIBUTE_UNUSED requested_mhz =
          registers::RenderPerformanceNormalFrequencyRequest::read(register_io_.get());
      TRACE_COUNTER("magma", "gpu freq", 0, "request_mhz", requested_mhz, "actual_mhz", actual_mhz);
    }
  }
}

int MsdIntelDevice::DeviceThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

  std::unique_lock<std::mutex> lock(device_request_mutex_);
  // Manipulate device_thread_id_ while locked, here and below
  device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
  lock.unlock();

  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

  std::vector<EngineCommandStreamer*> engines = engine_command_streamers();

  for (auto& engine : engines) {
    EnableInterrupts(engine, true);
  }

  {
    bool result = RenderInitBatch();
    DASSERT(result);
  }

  std::chrono::steady_clock::time_point last_freq_poll_time;

  while (true) {
    std::chrono::milliseconds timeout_ms = GetDeviceRequestTimeoutMs(engines);

    // When the semaphore wait returns the semaphore will be reset.
    // The reset may race with subsequent enqueue/signals on the semaphore,
    // which is fine because we process everything available in the queue
    // before returning here to wait.
    DASSERT(timeout_ms.count() >= 0);
    magma::Status status = device_request_semaphore_->Wait(timeout_ms.count());

    switch (status.get()) {
      case MAGMA_STATUS_OK:
        break;
      case MAGMA_STATUS_TIMED_OUT: {
        DeviceRequestTimedOut(engines);
        break;
      }
      default:
        MAGMA_LOG(WARNING, "device_request_semaphore_ Wait failed: %d", status.get());
        DASSERT(false);
        // TODO(fxbug.dev/13287): should we trigger a restart of the driver?
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    while (true) {
      lock.lock();
      if (!device_request_list_.size())
        break;
      auto request = std::move(device_request_list_.front());
      device_request_list_.pop_front();
      lock.unlock();
      request->ProcessAndReply(this);
    }
    lock.unlock();

    if (device_thread_quit_flag_)
      break;

    TraceFreq(last_freq_poll_time);
  }

  for (auto& engine : engines) {
    EnableInterrupts(engine, false);
  }

  DLOG("DeviceThreadLoop exit");
  lock.lock();
  device_thread_id_.reset();

  return 0;
}

void MsdIntelDevice::ProcessCompletedCommandBuffers(EngineCommandStreamerId id) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);
  TRACE_DURATION("magma", "ProcessCompletedCommandBuffers");

  switch (id) {
    case RENDER_COMMAND_STREAMER:
      render_engine_cs_->ProcessCompletedCommandBuffers(
          render_engine_cs()->hardware_status_page()->read_sequence_number());
      break;
    case VIDEO_COMMAND_STREAMER:
      video_command_streamer_->ProcessCompletedCommandBuffers(
          video_command_streamer()->hardware_status_page()->read_sequence_number());
      break;
    default:
      DASSERT(false);
  }
}

magma::Status MsdIntelDevice::ProcessInterrupts(uint64_t interrupt_time_ns,
                                                uint32_t render_interrupt_status,
                                                uint32_t video_interrupt_status) {
  TRACE_DURATION("magma", "ProcessInterrupts");

  DLOG("ProcessInterrupts render_interrupt_status 0x%08x video_interrupt_status 0x%08x",
       render_interrupt_status, video_interrupt_status);

  if (render_interrupt_status & registers::InterruptRegisterBase::kUserBit) {
    ProcessCompletedCommandBuffers(RENDER_COMMAND_STREAMER);
  }
  if (render_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
    render_engine_cs_->ContextSwitched();
  }

  if (video_interrupt_status & registers::InterruptRegisterBase::kUserBit) {
    ProcessCompletedCommandBuffers(VIDEO_COMMAND_STREAMER);
  }
  if (video_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
    video_command_streamer_->ContextSwitched();
  }

  auto fault_reg = registers::AllEngineFault::GetAddr(device_id_).ReadFrom(register_io_.get());

  if (fault_reg.valid()) {
    std::vector<std::string> dump;
    DumpToString(dump);
    MAGMA_LOG(WARNING, "GPU fault detected\n");
    for (auto& str : dump) {
      MAGMA_LOG(WARNING, "%s", str.c_str());
    }

    switch (fault_reg.engine()) {
      case registers::AllEngineFault::RCS:
        EngineReset(render_engine_cs());
        break;
      case registers::AllEngineFault::VCS1:
        EngineReset(video_command_streamer());
        break;
      default:
        DASSERT(false);
    }
  }

  return MAGMA_STATUS_OK;
}

magma::Status MsdIntelDevice::ProcessDumpStatusToLog() {
  std::vector<std::string> dump;
  DumpToString(dump);
  for (auto& str : dump) {
    MAGMA_LOG(INFO, "%s", str.c_str());
  }
  return MAGMA_STATUS_OK;
}

void MsdIntelDevice::HangCheckTimeout(uint64_t timeout_ms, EngineCommandStreamerId id) {
  std::vector<std::string> dump;
  DumpToString(dump);

  uint32_t interrupt_status[2];
  bool pending_interrupt;

  if (DeviceId::is_gen12(device_id())) {
    interrupt_status[0] = registers::GtInterruptStatus0Gen12::Get(register_io_.get()).reg_value();
    interrupt_status[1] = registers::GtInterruptStatus1Gen12::Get(register_io_.get()).reg_value();
    pending_interrupt = interrupt_status[0] || interrupt_status[1];
  } else {
    interrupt_status[0] = registers::MasterInterruptControl::read(register_io_.get());
    interrupt_status[1] = 0;
    pending_interrupt = interrupt_status[0] & ~registers::MasterInterruptControl::kEnableBitMask;
  }

  EngineCommandStreamer* engine;

  switch (id) {
    case RENDER_COMMAND_STREAMER:
      engine = render_engine_cs();
      break;

    case VIDEO_COMMAND_STREAMER:
      engine = video_command_streamer();
      break;
  }

  if (pending_interrupt) {
    MAGMA_LOG(WARNING,
              "%s: Hang check timeout (%lu ms) while pending interrupt; slow interrupt handler?\n"
              "last submitted sequence number 0x%x interrupt status 0x%08x (0x%08x) "
              "last_interrupt_callback_timestamp %lu last_interrupt_timestamp %lu",
              engine->Name(), timeout_ms, engine->progress()->last_submitted_sequence_number(),
              interrupt_status[0], interrupt_status[1], last_interrupt_callback_timestamp_.load(),
              last_interrupt_timestamp_.load());
    for (auto& str : dump) {
      MAGMA_LOG(WARNING, "%s", str.c_str());
    }
    return;
  }

  MAGMA_LOG(WARNING,
            "%s: Suspected GPU hang (%lu ms):\nlast submitted sequence number "
            "0x%x interrupt status 0x%08x (0x%08x) last_interrupt_callback_timestamp %lu "
            "last_interrupt_timestamp %lu",
            engine->Name(), timeout_ms, engine->progress()->last_submitted_sequence_number(),
            interrupt_status[0], interrupt_status[1], last_interrupt_callback_timestamp_.load(),
            last_interrupt_timestamp_.load());

  for (auto& str : dump) {
    MAGMA_LOG(WARNING, "%s", str.c_str());
  }

  suspected_gpu_hang_count_ += 1;

  EngineReset(engine);
}

bool MsdIntelDevice::InitContextForEngine(MsdIntelContext* context,
                                          EngineCommandStreamer* command_streamer) {
  if (!command_streamer->InitContext(context))
    return DRETF(false, "failed to initialize context");

  if (!context->Map(gtt(), command_streamer->id()))
    return DRETF(false, "failed to map context");

  // TODO(fxbug.dev/80906): any workarounds or cache config for VCS?
  if (command_streamer->id() == RENDER_COMMAND_STREAMER) {
    if (DeviceId::is_gen9(device_id_)) {
      // TODO(fxbug.dev/109211) - workarounds for gen12
      if (!command_streamer->InitContextWorkarounds(context))
        return DRETF(false, "failed to init workarounds");

      if (!command_streamer->InitContextCacheConfig(context))
        return DRETF(false, "failed to init cache config");

      // TODO(fxbug.dev/109213) - indirect context for gen12
      command_streamer->InitIndirectContext(context, indirect_context_batch_);
    }
  }

  return true;
}

magma::Status MsdIntelDevice::ProcessBatch(std::unique_ptr<MappedBatch> batch) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);
  TRACE_DURATION("magma", "Device::ProcessBatch");

  DLOG("preparing batch for execution");

  auto context = batch->GetContext().lock();

  if (!context && batch->GetType() == MappedBatch::BatchType::MAPPING_RELEASE_BATCH) {
    // Use the global context for submitting release batches.
    reinterpret_cast<MappingReleaseBatch*>(batch.get())->SetContext(global_context_);

    context = batch->GetContext().lock();
  }

  DASSERT(context);

  if (context->killed())
    return DRET_MSG(MAGMA_STATUS_CONTEXT_KILLED, "Context killed");

  EngineCommandStreamer* command_streamer = nullptr;

  switch (batch->get_command_streamer()) {
    case RENDER_COMMAND_STREAMER:
      command_streamer = render_engine_cs_.get();
      break;
    case VIDEO_COMMAND_STREAMER:
      command_streamer = video_command_streamer_.get();
      break;
  }

  DASSERT(command_streamer);

  if (!context->IsInitializedForEngine(command_streamer->id())) {
    if (!InitContextForEngine(context.get(), command_streamer))
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to initialize context");
  }

  uint64_t ATTRIBUTE_UNUSED buffer_id = batch->GetBatchBufferId();
  {
    TRACE_DURATION("magma", "Device::SubmitBatch");
    TRACE_FLOW_STEP("magma", "command_buffer", buffer_id);
    command_streamer->SubmitBatch(std::move(batch));
  }

  RequestMaxFreq();

  return MAGMA_STATUS_OK;
}

magma::Status MsdIntelDevice::ProcessDestroyContext(
    std::shared_ptr<MsdIntelContext> client_context) {
  DLOG("ProcessDestroyContext");
  TRACE_DURATION("magma", "ProcessDestroyContext");

  CHECK_THREAD_IS_CURRENT(device_thread_id_);
  // Just let it go out of scope

  return MAGMA_STATUS_OK;
}

bool MsdIntelDevice::WaitIdleForTest(uint32_t timeout_ms) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  uint32_t sequence_number = Sequencer::kInvalidSequenceNumber;

  auto start = std::chrono::high_resolution_clock::now();

  for (auto engine : engine_command_streamers()) {
    while (!engine->IsIdle()) {
      ProcessCompletedCommandBuffers(engine->id());

      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::milli> elapsed = end - start;

      if (engine->progress()->last_completed_sequence_number() != sequence_number) {
        sequence_number = engine->progress()->last_completed_sequence_number();
        start = end;
      } else {
        if (elapsed.count() > timeout_ms)
          return DRETF(false, "WaitIdle timeout (%u ms)", timeout_ms);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  }

  return true;
}

void MsdIntelDevice::RequestMaxFreq() {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  uint32_t mhz = registers::RenderPerformanceStateCapability::read_rp0_frequency(register_io());
  registers::RenderPerformanceNormalFrequencyRequest::write_frequency_request_gen9(register_io(),
                                                                                   mhz);
}

uint32_t MsdIntelDevice::GetCurrentFrequency() {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  if (DeviceId::is_gen9(device_id_))
    return registers::RenderPerformanceStatus::read_current_frequency_gen9(register_io());

  DLOG("GetCurrentGraphicsFrequency not implemented");
  return 0;
}

void MsdIntelDevice::QuerySliceInfoGen12(uint32_t* subslice_total_out, uint32_t* eu_total_out,
                                         Topology* topology_out) {
  // EU mask is shared amongst all subslices
  std::bitset<registers::MirrorEuDisableGen12::kEuDisableBits> eu_disable_bits =
      registers::MirrorEuDisableGen12::read(register_io());

  // Expand each disable bit into two enable bits.
  static_assert(registers::MirrorEuDisableGen12::kEuDisableBits * 2 <= 16);
  uint16_t eu_enable_mask = 0;
  {
    std::bitset<registers::MirrorEuDisableGen12::kEuDisableBits> eu_enable_bits(
        ~eu_disable_bits.to_ulong());

    for (size_t i = 0; i < eu_enable_bits.size(); i++) {
      uint8_t enable_bit = eu_enable_bits[i];
      eu_enable_mask |= (enable_bit << (i * 2)) | (enable_bit << (i * 2 + 1));
    }
  }

  topology_out->max_slice_count = 1;
  topology_out->max_subslice_count = registers::MirrorDssEnable::kDssPerSlice;
  topology_out->max_eu_count = registers::MirrorEuDisableGen12::kEusPerSubslice;

  {
    // Assume that the single slice is enabled.
    constexpr uint8_t kSliceMask = 1 << 0;
    topology_out->mask_data.push_back(kSliceMask);
  }

  std::vector<std::bitset<registers::MirrorDssEnable::kDssPerSlice>> dss_enable_masks =
      registers::MirrorDssEnable::read(register_io());

  uint32_t subslice_total = 0;

  {
    auto& dss_enable_mask = dss_enable_masks[0];  // subslice mask for the one enabled slice
    subslice_total += dss_enable_mask.count();

    DASSERT(dss_enable_mask.to_ulong() <= std::numeric_limits<uint8_t>::max());
    topology_out->mask_data.push_back(static_cast<uint8_t>(dss_enable_mask.to_ulong()));

    for (size_t i = 0; i < dss_enable_mask.count(); i++) {
      topology_out->mask_data.push_back(eu_enable_mask & 0xFF);
      topology_out->mask_data.push_back(eu_enable_mask >> 8);
    }
  }

  topology_out->data_byte_count = magma::to_uint32(topology_out->mask_data.size());

  if (subslice_total_out) {
    *subslice_total_out = subslice_total;
  }
  if (eu_total_out) {
    uint32_t eus_per_subslice = registers::MirrorEuDisableGen12::kEusPerSubslice -
                                2u * static_cast<uint32_t>(eu_disable_bits.count());

    *eu_total_out = subslice_total * eus_per_subslice;
  }
}

void MsdIntelDevice::QuerySliceInfoGen9(uint32_t* subslice_total_out, uint32_t* eu_total_out,
                                        Topology* topology_out) {
  uint32_t slice_enable_mask;
  uint32_t subslice_enable_mask;

  registers::Fuse2ControlDwordMirror::read(register_io_.get(), &slice_enable_mask,
                                           &subslice_enable_mask);

  DLOG("slice_enable_mask 0x%x subslice_enable_mask 0x%x", slice_enable_mask, subslice_enable_mask);

  std::bitset<registers::MirrorEuDisable::kMaxSliceCount> slice_bitset(slice_enable_mask);
  std::bitset<registers::MirrorEuDisable::kMaxSubsliceCount> subslice_bitset(subslice_enable_mask);

  *subslice_total_out = magma::to_uint32(slice_bitset.count() * subslice_bitset.count());
  *eu_total_out = 0;

  topology_out->max_slice_count = registers::MirrorEuDisable::kMaxSliceCount;
  topology_out->max_subslice_count = registers::MirrorEuDisable::kMaxSubsliceCount;
  topology_out->max_eu_count = registers::MirrorEuDisable::kEuPerSubslice;

  DASSERT(slice_enable_mask <= std::numeric_limits<uint8_t>::max());
  topology_out->mask_data.push_back(static_cast<uint8_t>(slice_enable_mask));

  for (uint8_t slice = 0; slice < registers::MirrorEuDisable::kMaxSliceCount; slice++) {
    if ((slice_enable_mask & (1 << slice)) == 0)
      continue;  // skip disabled slice

    DASSERT(subslice_enable_mask <= std::numeric_limits<uint8_t>::max());
    topology_out->mask_data.push_back(static_cast<uint8_t>(subslice_enable_mask));

    std::vector<uint32_t> eu_disable_mask;
    registers::MirrorEuDisable::read(register_io_.get(), slice, eu_disable_mask);

    for (uint32_t subslice = 0; subslice < eu_disable_mask.size(); subslice++) {
      if ((subslice_enable_mask & (1 << subslice)) == 0)
        continue;  // skip disabled subslice

      DLOG("subslice %u eu_disable_mask 0x%x", subslice, eu_disable_mask[subslice]);

      DASSERT(eu_disable_mask[subslice] <= std::numeric_limits<uint8_t>::max());
      uint8_t eu_enable_mask = ~static_cast<uint8_t>(eu_disable_mask[subslice]);
      topology_out->mask_data.push_back(eu_enable_mask);

      size_t eu_disable_count =
          std::bitset<registers::MirrorEuDisable::kEuPerSubslice>(eu_disable_mask[subslice])
              .count();
      *eu_total_out += registers::MirrorEuDisable::kEuPerSubslice - eu_disable_count;
    }
  }

  topology_out->data_byte_count = magma::to_uint32(topology_out->mask_data.size());
}

magma::Status MsdIntelDevice::QueryTimestamp(std::unique_ptr<magma::PlatformBuffer> buffer) {
  auto request = std::make_unique<TimestampRequest>(std::move(buffer));
  auto reply = request->GetReply();

  EnqueueDeviceRequest(std::move(request));

  constexpr uint32_t kWaitTimeoutMs = 1000;
  magma::Status status = reply->Wait(kWaitTimeoutMs);
  if (!status.ok())
    return DRET_MSG(status.get(), "reply wait failed");

  return MAGMA_STATUS_OK;
}

static uint64_t get_ns_monotonic(bool raw) {
  struct timespec time;
  int ret = clock_gettime(raw ? CLOCK_MONOTONIC_RAW : CLOCK_MONOTONIC, &time);
  if (ret < 0)
    return 0;
  return static_cast<uint64_t>(time.tv_sec) * 1000000000ULL + time.tv_nsec;
}

magma::Status MsdIntelDevice::ProcessTimestampRequest(
    std::shared_ptr<magma::PlatformBuffer> buffer) {
  magma_intel_gen_timestamp_query* query;
  {
    void* ptr;
    if (!buffer->MapCpu(&ptr))
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to map query buffer");
    query = reinterpret_cast<magma_intel_gen_timestamp_query*>(ptr);
  }

  // The monotonic raw timestamps represent the start/end of the sample interval.
  query->monotonic_raw_timestamp[0] = get_ns_monotonic(true);
  query->monotonic_timestamp = get_ns_monotonic(false);
  query->device_timestamp =
      registers::Timestamp::read(register_io_.get(), render_engine_cs()->mmio_base());
  query->monotonic_raw_timestamp[1] = get_ns_monotonic(true);

  buffer->UnmapCpu();

  return MAGMA_STATUS_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* dev, msd_client_id_t client_id) {
  auto connection = MsdIntelDevice::cast(dev)->Open(client_id);
  if (!connection)
    return DRETP(nullptr, "MsdIntelDevice::Open failed");
  return new MsdIntelAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* dev) { delete MsdIntelDevice::cast(dev); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id,
                                magma_handle_t* result_buffer_out, uint64_t* result_out) {
  switch (id) {
    case MAGMA_QUERY_VENDOR_ID:
      *result_out = MAGMA_VENDOR_ID_INTEL;
      break;

    case MAGMA_QUERY_DEVICE_ID:
      *result_out = MsdIntelDevice::cast(device)->device_id();
      break;

    case MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED:
      *result_out = 0;
      break;

    case kMagmaIntelGenQuerySubsliceAndEuTotal:
      *result_out = MsdIntelDevice::cast(device)->subslice_total();
      *result_out = (*result_out << 32) | MsdIntelDevice::cast(device)->eu_total();
      break;

    case kMagmaIntelGenQueryGttSize:
      *result_out = 1ul << 48;
      break;

    case kMagmaIntelGenQueryExtraPageCount:
      *result_out = PerProcessGtt::ExtraPageCount();
      break;

    case kMagmaIntelGenQueryTimestamp: {
      auto buffer = magma::PlatformBuffer::Create(magma::page_size(), "timestamps");
      if (!buffer)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to create timestamp buffer");

      if (!buffer->duplicate_handle(result_buffer_out))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to dupe timestamp buffer");

      return MsdIntelDevice::cast(device)->QueryTimestamp(std::move(buffer)).get();
    }

    case kMagmaIntelGenQueryTopology: {
      auto [topology, mask_data] = MsdIntelDevice::cast(device)->GetTopology();
      if (!topology)
        return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "topology not present");

      size_t size = sizeof(magma_intel_gen_topology) + topology->data_byte_count;
      auto buffer = magma::PlatformBuffer::Create(size, "topology");
      if (!buffer)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to create topology buffer");

      {
        void* ptr;
        if (!buffer->MapCpu(&ptr))
          return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to map topology buffer");

        memcpy(ptr, topology, sizeof(magma_intel_gen_topology));
        memcpy(reinterpret_cast<uint8_t*>(ptr) + sizeof(magma_intel_gen_topology), mask_data,
               topology->data_byte_count);

        buffer->UnmapCpu();
      }

      if (!buffer->duplicate_handle(result_buffer_out))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to dupe topology buffer");

      return MAGMA_STATUS_OK;
    }

    case kMagmaIntelGenQueryHasContextIsolation: {
      *result_out = MsdIntelDevice::cast(device)->engines_have_context_isolation();
      break;
    }

    default:
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
  }

  if (result_buffer_out)
    *result_buffer_out = magma::PlatformHandle::kInvalidHandle;

  return MAGMA_STATUS_OK;
}

void MsdIntelDevice::CheckEngines() {
  // Most engines support context isolation, but some may not; as support
  // for more engines are added, this should be revisited.
  engines_have_context_isolation_ = true;

  for (auto& engine : engine_command_streamers()) {
    switch (engine->id()) {
      case RENDER_COMMAND_STREAMER:
      case VIDEO_COMMAND_STREAMER:
        break;
    }
  }
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {
  MsdIntelDevice::cast(device)->DumpStatusToLog();
}

magma_status_t msd_device_get_icd_list(struct msd_device_t* abi_device, uint64_t count,
                                       msd_icd_info_t* icd_info_out, uint64_t* actual_count_out) {
  const char* kSuffixes[] = {"_test", ""};
  constexpr uint32_t kMediaIcdCount = 1;
  constexpr uint32_t kTotalIcdCount = std::size(kSuffixes) + kMediaIcdCount;

  if (icd_info_out && count < kTotalIcdCount) {
    return MAGMA_STATUS_INVALID_ARGS;
  }
  *actual_count_out = kTotalIcdCount;
  if (icd_info_out) {
    for (uint32_t i = 0; i < std::size(kSuffixes); i++) {
      strncpy(icd_info_out[i].component_url,
              fbl::StringPrintf("fuchsia-pkg://fuchsia.com/libvulkan_intel_gen%s#meta/vulkan.cm",
                                kSuffixes[i])
                  .c_str(),
              sizeof(icd_info_out[i].component_url) - 1);
      icd_info_out[i].support_flags = ICD_SUPPORT_FLAG_VULKAN;
    }
    {
      size_t media_index = std::size(kSuffixes);
      strncpy(icd_info_out[media_index].component_url,
              "fuchsia-pkg://fuchsia.com/codec_runner_intel_gen#meta/codec_runner_intel_gen.cm",
              sizeof(icd_info_out[media_index].component_url) - 1);
      icd_info_out[media_index].support_flags = ICD_SUPPORT_FLAG_MEDIA_CODEC_FACTORY;
    }
  }
  return MAGMA_STATUS_OK;
}
