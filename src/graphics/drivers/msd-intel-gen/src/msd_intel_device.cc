// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"

#include <bitset>
#include <cstdio>
#include <string>

#include <fbl/string_printf.h>

#include "device_id.h"
#include "forcewake.h"
#include "global_context.h"
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
  DestroyContextRequest(std::shared_ptr<ClientContext> client_context)
      : client_context_(std::move(client_context)) {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessDestroyContext(std::move(client_context_));
  }

 private:
  std::shared_ptr<ClientContext> client_context_;
};

class MsdIntelDevice::InterruptRequest : public DeviceRequest {
 public:
  InterruptRequest(uint64_t interrupt_time_ns, uint32_t master_interrupt_control,
                   uint32_t render_interrupt_status, uint32_t video_interrupt_status)
      : interrupt_time_ns_(interrupt_time_ns),
        master_interrupt_control_(master_interrupt_control),
        render_interrupt_status_(render_interrupt_status),
        video_interrupt_status_(video_interrupt_status) {}

 protected:
  magma::Status Process(MsdIntelDevice* device) override {
    return device->ProcessInterrupts(interrupt_time_ns_, master_interrupt_control_,
                                     render_interrupt_status_, video_interrupt_status_);
  }
  uint64_t interrupt_time_ns_;
  uint32_t master_interrupt_control_;
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

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdIntelDevice> MsdIntelDevice::Create(void* device_handle,
                                                       bool start_device_thread) {
  std::unique_ptr<MsdIntelDevice> device(new MsdIntelDevice());

  if (!device->Init(device_handle, true))
    return DRETP(nullptr, "Failed to initialize MsdIntelDevice");

  if (start_device_thread)
    device->StartDeviceThread();

  return device;
}

MsdIntelDevice::MsdIntelDevice() { magic_ = kMagic; }

MsdIntelDevice::~MsdIntelDevice() { Destroy(); }

void MsdIntelDevice::Destroy() {
  DLOG("Destroy");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  interrupt_manager_.reset();

  device_thread_quit_flag_ = true;

  if (device_request_semaphore_)
    device_request_semaphore_->Signal();

  if (device_thread_.joinable()) {
    DLOG("joining device thread");
    device_thread_.join();
    DLOG("joined");
  }

  if (render_engine_cs_) {
    // Ensure gpu is idle
    render_engine_cs_->Reset();
  }
}

std::unique_ptr<MsdIntelConnection> MsdIntelDevice::Open(msd_client_id_t client_id) {
  return MsdIntelConnection::Create(this, client_id);
}

bool MsdIntelDevice::Init(void* device_handle, bool exec_init_batch) {
  if (!BaseInit(device_handle))
    return DRETF(false, "BaseInit failed");

  InitEngine(render_engine_cs());
  InitEngine(video_command_streamer());

  // WaEnableGapsTsvCreditFix
  registers::ArbiterControl::workaround(register_io());

  if (exec_init_batch) {
    if (!RenderInitBatch())
      return DRETF(false, "RenderInitBatch failed");
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

  if (DeviceId::is_gen9(device_id_)) {
    ForceWake::reset(register_io_.get(), registers::ForceWake::GEN9_RENDER);
    ForceWake::request(register_io_.get(), registers::ForceWake::GEN9_RENDER);
  } else {
    MAGMA_LOG(WARNING, "Unrecognized graphics PCI device id 0x%x", device_id_);
    return false;
  }

  bus_mapper_ = magma::PlatformBusMapper::Create(platform_device_->GetBusTransactionInitiator());
  if (!bus_mapper_)
    return DRETF(false, "failed to create bus mapper");

  // Clear faults
  registers::AllEngineFault::clear(register_io_.get());

  QuerySliceInfo(&subslice_total_, &eu_total_);

  interrupt_manager_ = InterruptManager::CreateShim(this);
  if (!interrupt_manager_)
    return DRETF(false, "failed to create interrupt manager");

  PerProcessGtt::InitPrivatePat(register_io_.get());

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

  global_context_ = std::shared_ptr<GlobalContext>(new GlobalContext(gtt_));

  // Creates the context backing store.
  // Global context used to execute the render init batch.
  if (!render_engine_cs_->InitContext(global_context_.get()))
    return DRETF(false, "render_engine_cs failed to init global context");

  if (!global_context_->Map(gtt_, render_engine_cs_->id()))
    return DRETF(false, "global context init failed");

  device_request_semaphore_ = magma::PlatformSemaphore::Create();

  return true;
}

void MsdIntelDevice::InitEngine(EngineCommandStreamer* engine) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  engine->InitHardware();

  switch (engine->id()) {
    case RENDER_COMMAND_STREAMER:
      // Enable render command streamer interrupts.
      registers::GtInterruptMask0::write(register_io(), registers::InterruptRegisterBase::USER,
                                         registers::InterruptRegisterBase::UNMASK);
      registers::GtInterruptEnable0::write(register_io(), registers::InterruptRegisterBase::USER,
                                           true);

      registers::GtInterruptMask0::write(register_io(),
                                         registers::InterruptRegisterBase::CONTEXT_SWITCH,
                                         registers::InterruptRegisterBase::UNMASK);
      registers::GtInterruptEnable0::write(register_io(),
                                           registers::InterruptRegisterBase::CONTEXT_SWITCH, true);
      break;

    case VIDEO_COMMAND_STREAMER:
      // Enable video command streamer interrupts.
      registers::GtInterruptMask1::write(register_io(), registers::InterruptRegisterBase::USER,
                                         registers::InterruptRegisterBase::UNMASK);
      registers::GtInterruptEnable1::write(register_io(), registers::InterruptRegisterBase::USER,
                                           true);

      registers::GtInterruptMask1::write(register_io(),
                                         registers::InterruptRegisterBase::CONTEXT_SWITCH,
                                         registers::InterruptRegisterBase::UNMASK);
      registers::GtInterruptEnable1::write(register_io(),
                                           registers::InterruptRegisterBase::CONTEXT_SWITCH, true);
      break;

    default:
      DASSERT(false);
  }
}

bool MsdIntelDevice::RenderInitBatch() {
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

  registers::AllEngineFault::clear(register_io_.get());

  if (engine->id() == RENDER_COMMAND_STREAMER) {
    if (!RenderInitBatch())
      return false;
  }

  return true;
}

void MsdIntelDevice::StartDeviceThread() {
  DASSERT(!device_thread_.joinable());
  device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });

  // Don't start interrupt processing until the device thread is running.
  interrupt_manager_->RegisterCallback(
      InterruptCallback, this,
      registers::MasterInterruptControl::kRenderInterruptsPendingBitMask |
          registers::MasterInterruptControl::kVideoInterruptsPendingBitMask);
}

void MsdIntelDevice::InterruptCallback(void* data, uint32_t master_interrupt_control,
                                       uint64_t interrupt_timestamp) {
  DASSERT(data);
  auto device = reinterpret_cast<MsdIntelDevice*>(data);

  device->last_interrupt_callback_timestamp_ = magma::get_monotonic_ns();
  device->last_interrupt_timestamp_ = interrupt_timestamp;

  magma::RegisterIo* register_io = device->register_io_for_interrupt();
  uint64_t now = get_current_time_ns();
  uint32_t render_interrupt_status = 0;
  uint32_t video_interrupt_status = 0;

  if (master_interrupt_control &
      registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {
    render_interrupt_status = registers::GtInterruptIdentity0::read(register_io);
    DLOG("gt IIR0 0x%08x", render_interrupt_status);

    if (render_interrupt_status & registers::InterruptRegisterBase::kUserInterruptBit) {
      registers::GtInterruptIdentity0::clear(register_io, registers::InterruptRegisterBase::USER);
    }
    if (render_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
      registers::GtInterruptIdentity0::clear(register_io,
                                             registers::InterruptRegisterBase::CONTEXT_SWITCH);
    }
  }

  if (master_interrupt_control &
      registers::MasterInterruptControl::kVideoInterruptsPendingBitMask) {
    video_interrupt_status = registers::GtInterruptIdentity1::read(register_io);
    DLOG("gt IIR1 0x%08x", video_interrupt_status);

    if (video_interrupt_status & registers::InterruptRegisterBase::kUserInterruptBit) {
      registers::GtInterruptIdentity1::clear(register_io, registers::InterruptRegisterBase::USER);
    }
    if (video_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
      registers::GtInterruptIdentity1::clear(register_io,
                                             registers::InterruptRegisterBase::CONTEXT_SWITCH);
    }
  }

  if (render_interrupt_status || video_interrupt_status) {
    device->EnqueueDeviceRequest(std::make_unique<InterruptRequest>(
        now, master_interrupt_control, render_interrupt_status, video_interrupt_status));
  }
}

void MsdIntelDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

magma::Status MsdIntelDevice::SubmitBatch(std::unique_ptr<MappedBatch> batch) {
  DLOG("SubmitBatch");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  EnqueueDeviceRequest(std::make_unique<BatchRequest>(std::move(batch)));
  return MAGMA_STATUS_OK;
}

void MsdIntelDevice::DestroyContext(std::shared_ptr<ClientContext> client_context) {
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

  std::vector<EngineCommandStreamer*> engines{render_engine_cs(), video_command_streamer()};

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
                                                uint32_t master_interrupt_control,
                                                uint32_t render_interrupt_status,
                                                uint32_t video_interrupt_status) {
  TRACE_DURATION("magma", "ProcessInterrupts");

  if (master_interrupt_control &
      registers::MasterInterruptControl::kRenderInterruptsPendingBitMask) {
    if (render_interrupt_status & registers::InterruptRegisterBase::kUserInterruptBit) {
      ProcessCompletedCommandBuffers(RENDER_COMMAND_STREAMER);
    }
    if (render_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
      render_engine_cs_->ContextSwitched();
    }
  }

  if (master_interrupt_control &
      registers::MasterInterruptControl::kVideoInterruptsPendingBitMask) {
    if (video_interrupt_status & registers::InterruptRegisterBase::kUserInterruptBit) {
      ProcessCompletedCommandBuffers(VIDEO_COMMAND_STREAMER);
    }
    if (video_interrupt_status & registers::InterruptRegisterBase::kContextSwitchBit) {
      video_command_streamer_->ContextSwitched();
    }
  }

  uint32_t fault = registers::AllEngineFault::read(register_io_.get());

  if (registers::AllEngineFault::valid(fault)) {
    std::vector<std::string> dump;
    DumpToString(dump);
    MAGMA_LOG(WARNING, "GPU fault detected\n");
    for (auto& str : dump) {
      MAGMA_LOG(WARNING, "%s", str.c_str());
    }

    switch (registers::AllEngineFault::engine(fault)) {
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

  uint32_t master_interrupt_control = registers::MasterInterruptControl::read(register_io_.get());

  bool pending_interrupt;
  EngineCommandStreamer* engine;

  switch (id) {
    case RENDER_COMMAND_STREAMER:
      pending_interrupt = (master_interrupt_control &
                           registers::MasterInterruptControl::kRenderInterruptsPendingBitMask);
      engine = render_engine_cs();
      break;

    case VIDEO_COMMAND_STREAMER:
      pending_interrupt = (master_interrupt_control &
                           registers::MasterInterruptControl::kVideoInterruptsPendingBitMask);
      engine = video_command_streamer();
      break;

    default:
      DASSERT(false);
      return;
  }

  if (pending_interrupt) {
    MAGMA_LOG(WARNING,
              "%s: Hang check timeout (%lu ms) while pending interrupt; slow interrupt handler?\n"
              "last submitted sequence number 0x%x master_interrupt_control 0x%08x "
              "last_interrupt_callback_timestamp %lu last_interrupt_timestamp %lu",
              engine->Name(), timeout_ms, engine->progress()->last_submitted_sequence_number(),
              master_interrupt_control, last_interrupt_callback_timestamp_.load(),
              last_interrupt_timestamp_.load());
    for (auto& str : dump) {
      MAGMA_LOG(WARNING, "%s", str.c_str());
    }
    return;
  }

  MAGMA_LOG(WARNING,
            "%s: Suspected GPU hang (%lu ms):\nlast submitted sequence number "
            "0x%x master_interrupt_control 0x%08x last_interrupt_callback_timestamp %lu "
            "last_interrupt_timestamp %lu",
            engine->Name(), timeout_ms, engine->progress()->last_submitted_sequence_number(),
            master_interrupt_control, last_interrupt_callback_timestamp_.load(),
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
    if (!command_streamer->InitContextWorkarounds(context))
      return DRETF(false, "failed to init workarounds");

    if (!command_streamer->InitContextCacheConfig(context))
      return DRETF(false, "failed to init cache config");
  }

  return true;
}

magma::Status MsdIntelDevice::ProcessBatch(std::unique_ptr<MappedBatch> batch) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);
  TRACE_DURATION("magma", "Device::ProcessBatch");

  DLOG("preparing command buffer for execution");

  auto context = batch->GetContext().lock();
  DASSERT(context);

  if (context->killed())
    return DRET_MSG(MAGMA_STATUS_CONTEXT_KILLED, "Context killed");

  EngineCommandStreamer* command_streamer = render_engine_cs_.get();

  if (batch->IsCommandBuffer() && (static_cast<CommandBuffer*>(batch.get())->GetFlags() &
                                   kMagmaIntelGenCommandBufferForVideo)) {
    command_streamer = video_command_streamer_.get();
  }

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

magma::Status MsdIntelDevice::ProcessDestroyContext(std::shared_ptr<ClientContext> client_context) {
  DLOG("ProcessDestroyContext");
  TRACE_DURATION("magma", "ProcessDestroyContext");

  CHECK_THREAD_IS_CURRENT(device_thread_id_);
  // Just let it go out of scope

  return MAGMA_STATUS_OK;
}

bool MsdIntelDevice::WaitIdleForTest(uint32_t timeout_ms) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  std::vector<EngineCommandStreamer*> engines{render_engine_cs(), video_command_streamer()};

  uint32_t sequence_number = Sequencer::kInvalidSequenceNumber;

  auto start = std::chrono::high_resolution_clock::now();

  for (auto engine : engines) {
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

void MsdIntelDevice::QuerySliceInfo(uint32_t* subslice_total_out, uint32_t* eu_total_out) {
  uint32_t slice_enable_mask;
  uint32_t subslice_enable_mask;

  registers::Fuse2ControlDwordMirror::read(register_io_.get(), &slice_enable_mask,
                                           &subslice_enable_mask);

  DLOG("slice_enable_mask 0x%x subslice_enable_mask 0x%x", slice_enable_mask, subslice_enable_mask);

  std::bitset<registers::MirrorEuDisable::kMaxSliceCount> slice_bitset(slice_enable_mask);
  std::bitset<registers::MirrorEuDisable::kMaxSubsliceCount> subslice_bitset(subslice_enable_mask);

  *subslice_total_out = magma::to_uint32(slice_bitset.count() * subslice_bitset.count());
  *eu_total_out = 0;

  for (uint8_t slice = 0; slice < registers::MirrorEuDisable::kMaxSliceCount; slice++) {
    if ((slice_enable_mask & (1 << slice)) == 0)
      continue;  // skip disabled slice

    std::vector<uint32_t> eu_disable_mask;
    registers::MirrorEuDisable::read(register_io_.get(), slice, eu_disable_mask);

    for (uint32_t subslice = 0; subslice < eu_disable_mask.size(); subslice++) {
      if ((subslice_enable_mask & (1 << subslice)) == 0)
        continue;  // skip disabled subslice

      DLOG("subslice %u eu_disable_mask 0x%x", subslice, eu_disable_mask[subslice]);

      size_t eu_disable_count =
          std::bitset<registers::MirrorEuDisable::kEuPerSubslice>(eu_disable_mask[subslice])
              .count();
      *eu_total_out += registers::MirrorEuDisable::kEuPerSubslice - eu_disable_count;
    }
  }
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

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out) {
  switch (id) {
    case MAGMA_QUERY_VENDOR_ID:
      *value_out = MAGMA_VENDOR_ID_INTEL;
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_DEVICE_ID:
      *value_out = MsdIntelDevice::cast(device)->device_id();
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED:
      *value_out = 0;
      return MAGMA_STATUS_OK;

    case kMagmaIntelGenQuerySubsliceAndEuTotal:
      *value_out = MsdIntelDevice::cast(device)->subslice_total();
      *value_out = (*value_out << 32) | MsdIntelDevice::cast(device)->eu_total();
      return MAGMA_STATUS_OK;

    case kMagmaIntelGenQueryGttSize:
      *value_out = 1ul << 48;
      return MAGMA_STATUS_OK;

    case kMagmaIntelGenQueryExtraPageCount:
      *value_out = PerProcessGtt::ExtraPageCount();
      return MAGMA_STATUS_OK;
  }
  return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

magma_status_t msd_device_query_returns_buffer(msd_device_t* device, uint64_t id,
                                               uint32_t* buffer_out) {
  switch (id) {
    case kMagmaIntelGenQueryTimestamp: {
      auto buffer = magma::PlatformBuffer::Create(magma::page_size(), "timestamps");
      if (!buffer)
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to create timestamp buffer");

      if (!buffer->duplicate_handle(buffer_out))
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to dupe timestamp buffer");

      return MsdIntelDevice::cast(device)->QueryTimestamp(std::move(buffer)).get();
    }
  }
  return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {
  MsdIntelDevice::cast(device)->DumpStatusToLog();
}

magma_status_t msd_device_get_icd_list(struct msd_device_t* abi_device, uint64_t count,
                                       msd_icd_info_t* icd_info_out, uint64_t* actual_count_out) {
  const char* kSuffixes[] = {"_test", ""};
  if (icd_info_out && count < std::size(kSuffixes)) {
    return MAGMA_STATUS_INVALID_ARGS;
  }
  *actual_count_out = std::size(kSuffixes);
  if (icd_info_out) {
    for (uint32_t i = 0; i < std::size(kSuffixes); i++) {
      strcpy(icd_info_out[i].component_url,
             fbl::StringPrintf("fuchsia-pkg://fuchsia.com/libvulkan_intel_gen%s#meta/vulkan.cm",
                               kSuffixes[i])
                 .c_str());
      icd_info_out[i].support_flags = ICD_SUPPORT_FLAG_VULKAN;
    }
  }
  return MAGMA_STATUS_OK;
}
