// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sanitizer-cov-proxy.h"

#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <fbl/auto_call.h>

namespace fuzzing {
namespace {

// Instruction tracing must happen very fast, so this class avoids taking a lock whenever possible.
// Instead, it uses a double buffered trace array, and manages concurrent access by using a "state"
// variable. The state of the SanitizerCovProxy's instruction traces is represented as an atomic
// uint64_t that tracks 4 different bit-fields:
//
// 1) Bit flags
// 2) Offset into traces array. See description in |State| below.
// 3) Active writer count for the front half of the array, i.e. the number of threads writing to it.
// 4) Active writer count for the back half of the array, i.e. the number of threads writing to it.
//
// These are arranged as follows in the uint64_t:
// LSB 0       8      16      24      32      40      48      56      64 MSB
//     [-------|-------|-------|-------|-------|-------|-------|-------)
//     [1 ][2                 ][3                 ][4                 ]
//
// The width of the offset field is enough to exceed |kMaxInstructions|. The width of the writers
// fields implies we can support ~1M threads, which "ought to be enough for anybody".
//
// The specific locations of each field are opaque to the code below, which should simply use the
// bit flag names and getter/setter functions to access or mutate the state.

const uint64_t kFlagsBits = 16;
const uint64_t kOffsetBits = 16;
const uint64_t kWritersABits = 16;
const uint64_t kWritersBBits = 16;

// If (symbol ## Bits) is defined and STATE_FIELD(..., previous, ...) has already been invoked,
// creates functions to load and store a field from a 64 bit state value.
#define STATE_FIELD(name, symbol, previous)                                                   \
  const uint64_t symbol##Shift = (previous##Shift) + (previous##Bits);                        \
  const uint64_t symbol##Mask = ((1ULL << (symbol##Bits)) - 1) << (symbol##Shift);            \
  uint64_t get_##name(uint64_t state) { return (state & (symbol##Mask)) >> (symbol##Shift); } \
  uint64_t set_##name(uint64_t state, uint64_t value) {                                       \
    return (state & ~(symbol##Mask)) | ((value << (symbol##Shift)) & (symbol##Mask));         \
  }

// Bootstrap
const uint64_t kBaseBits = 0;
const uint64_t kBaseShift = 0;

STATE_FIELD(flags, kFlags, kBase)
STATE_FIELD(offset, kOffset, kFlags)
STATE_FIELD(writers_a, kWritersA, kOffset)
STATE_FIELD(writers_b, kWritersB, kWritersA)

static_assert(kWritersBBits + kWritersBShift <= 64);
#undef STATE_FIELD

// Bit flags
const uint64_t kMappedFlag = 1 << 0;
const uint64_t kReadableFlagA = 1 << 1;
const uint64_t kReadableFlagB = 1 << 2;
const uint64_t kWritableFlagA = 1 << 3;
const uint64_t kWritableFlagB = 1 << 4;

struct State {
  // See bit flags above
  uint64_t flags;

  // Offset into the trace array.
  uint64_t offset;

  // Number of active writers to the trace array. Indices are atomically reserved, but not
  // necessarily atomically written to. Each half of the double buffer can only signalled to the
  // fuzzing engine when its corresponding writer count has dropped to zero.
  uint64_t writers[kNumInstructionBuffers];

  State(uint64_t state = 0) { from_u64(state); }

  // Converter to/from uint64_t.
  uint64_t to_u64() {
    uint64_t state = set_flags(0, flags);
    state = set_offset(state, offset);
    state = set_writers_a(state, writers[0]);
    state = set_writers_b(state, writers[1]);
    return state;
  }
  void from_u64(uint64_t state) {
    flags = get_flags(state);
    offset = get_offset(state);
    writers[0] = get_writers_a(state);
    writers[1] = get_writers_b(state);
  }

  bool has(uint64_t flag) { return flags & flag; }
};

constexpr struct BufferParameters {
  const uint64_t start;
  const uint64_t last;
  const uint64_t next_start;
  const uint64_t readable_flag;
  const zx_signals_t readable_signal;
  const uint64_t writable_flag;
  const zx_signals_t writable_signal;
} kBuffers[kNumInstructionBuffers] = {{
                                          .start = 0,
                                          .last = kInstructionBufferLen - 1,
                                          .next_start = kInstructionBufferLen,
                                          .readable_flag = kReadableFlagA,
                                          .readable_signal = kReadableSignalA,
                                          .writable_flag = kWritableFlagA,
                                          .writable_signal = kWritableSignalA,
                                      },
                                      {
                                          .start = kInstructionBufferLen,
                                          .last = kMaxInstructions - 1,
                                          .next_start = 0,
                                          .readable_flag = kReadableFlagB,
                                          .readable_signal = kReadableSignalB,
                                          .writable_flag = kWritableFlagB,
                                          .writable_signal = kWritableSignalB,
                                      }};

}  // namespace

// Public methods

/* static */ SanitizerCovProxy *SanitizerCovProxy::GetInstance(bool autoconnect) {
  static SanitizerCovProxy instance(autoconnect);
  return &instance;
}

SanitizerCovProxy::~SanitizerCovProxy() { Reset(); }

zx_status_t SanitizerCovProxy::SetCoverage(CoveragePtr coverage) {
  // Set the pointer to the Coverage service.
  if (!coverage.is_bound()) {
    return ZX_ERR_INVALID_ARGS;
  }
  Reset();
  auto cleanup = fbl::MakeAutoCall([this]() { Reset(); });

  std::lock_guard<std::mutex> lock(lock_);
  coverage_ = std::move(coverage);
  coverage_.set_error_handler([this](zx_status_t status) {
    if (status != ZX_ERR_CANCELED) {
      FX_LOGS(WARNING) << "Coverage service returned " << zx_status_get_string(status);
      Reset();
    }
  });

  // Map the trace array.
  zx_status_t status;
  zx::vmo vmo;
  if ((status = shmem_.Create(kMaxInstructions * sizeof(Instruction))) != ZX_OK ||
      (status = shmem_.Share(&vmo)) != ZX_OK) {
    return status;
  }
  traces_ = reinterpret_cast<Instruction *>(shmem_.addr());
  state_.fetch_or(kMappedFlag);

  // Add the trace array to the Coverage service.
  coverage_->AddTraces(std::move(vmo), []() {});

  // Start thread to sync with fuzzing engine.
  collector_ = std::thread([this]() {
    // If connecting between iterations, wait for the next one.
    if (vmo_->wait_one(kInIteration, zx::time::infinite(), nullptr) != ZX_OK ||
        vmo_->signal(kInIteration, 0) != ZX_OK) {
      return;
    }
    while (true) {
      if (vmo_->wait_one(kBetweenIterations, zx::time::infinite(), nullptr) != ZX_OK ||
          vmo_->signal(kBetweenIterations, 0) != ZX_OK) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(lock_);
        for (const auto &i : regions_) {
          // Copy __sanitizer_cov tables to the shared memory.
          void *src = reinterpret_cast<void *>(i.first);
          void *dst = reinterpret_cast<void *>(i.second.addr());
          size_t len = i.second.len();
          memcpy(dst, src, len);
        }
      }
      // Send any pending traces, followed by the sentinel.
      TraceImpl(Instruction::kSentinel, 0, 0, 0);
    }
  });

  cleanup.cancel();
  return ZX_OK;
}

void SanitizerCovProxy::Init8BitCountersImpl(uint8_t *start, uint8_t *stop) {
  std::lock_guard<std::mutex> lock(lock_);
  Buffer buffer;
  zx_status_t status = CreateSharedBufferLocked(start, stop, &buffer);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to map inline 8 bit counters: " << zx_status_get_string(status);
    return;
  }
  sync_completion_t sync;
  coverage_->AddInline8BitCounters(std::move(buffer), [&sync]() { sync_completion_signal(&sync); });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
}

void SanitizerCovProxy::InitPcsImpl(const uintptr_t *pcs_beg, const uintptr_t *pcs_end) {
  std::lock_guard<std::mutex> lock(lock_);
  Buffer buffer;
  zx_status_t status = CreateSharedBufferLocked(pcs_beg, pcs_end, &buffer);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to map PC table: " << zx_status_get_string(status);
    return;
  }
  sync_completion_t sync;
  coverage_->AddPcTable(std::move(buffer), [&sync]() { sync_completion_signal(&sync); });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
}

void SanitizerCovProxy::TraceImpl(Instruction::Type type, uintptr_t pc, uint64_t arg0,
                                  uint64_t arg1) {
  // Accessor struct for atomic state
  State s;
  uint64_t state = state_.load();

  // Buffer related variables.
  uint64_t offset;
  size_t i;
  const BufferParameters *buffer;
  BufferSync *sync;

  do {
    s.from_u64(state);
    // First, check if unmapped.
    if (!s.has(kMappedFlag)) {
      return;
    }

    // Identify an offset to try to reserve, and the buffer it is in.
    offset = s.offset;
    i = offset / kInstructionBufferLen;
    buffer = &kBuffers[i];
    sync = &syncs_[i];

    // Check if the buffer is writable. The thread that successfully grabs the first offset of a
    // buffer will also clear the writable flag, and then ensure the offset is ready before
    // signaling the `write_sync` and unblocking other writers. It's possible that a thread that
    // gets a non-zero offset tries to wait on `write_sync` before it has been reset by the
    // zero-offset thread below, but this is harmless. It will simply loop and try to wait again.
    if (offset == buffer->start) {
      s.flags &= ~buffer->writable_flag;
    } else if (!s.has(buffer->writable_flag)) {
      sync_completion_wait(&sync->write, ZX_TIME_INFINITE);
      state = state_.load();
      continue;
    }

    // Record that this thread is writing to a buffer.
    s.writers[i] += 1;

    // Advance the offset to the next offset.
    if ((type == Instruction::kSentinel) || (offset == buffer->last)) {
      s.offset = buffer->next_start;
      s.flags |= buffer->readable_flag;
    } else {
      s.offset += 1;
    }
    // Atomically update offset and writer counts.
  } while (!state_.compare_exchange_weak(state, s.to_u64()));
  state = s.to_u64();

  // If this thread grabbed the first offset, it should now check if it is writable and unblock any
  // other threads once it is. If |Reset| is called during the wait (e.g. the Coverage connection is
  // closed), the wait will return a non-ZX_OK status, and the thread must avoid writing to
  // previously shared memory.
  zx_status_t status = ZX_OK;
  if (offset == buffer->start) {
    sync_completion_reset(&sync->write);
    status = vmo_->wait_one(buffer->writable_signal, zx::time::infinite(), nullptr);
    vmo_->signal(buffer->writable_signal, 0);
    state_.fetch_or(buffer->writable_flag);
    sync_completion_signal(&sync->write);
  }

  if (status == ZX_OK) {
    Instruction *trace = &traces_[offset];
    trace->type = type;
    trace->pc = pc;
    trace->args[0] = arg0;
    trace->args[1] = arg1;
  }

  // Done writing; decrement the active writer count.
  bool readable = false;
  do {
    s.from_u64(state);
    FX_DCHECK(s.writers[i] != 0);
    readable = s.writers[i] == 1 && s.has(buffer->readable_flag);
    if (readable) {
      s.flags &= ~buffer->readable_flag;
      s.writers[i] = 0;
    } else {
      s.writers[i] -= 1;
    }
  } while (!state_.compare_exchange_weak(state, s.to_u64()));

  if (s.writers[i] == 0) {
    if (!s.has(kMappedFlag)) {
      // Last writer following a call to |Reset|.
      sync_completion_signal(&sync->reset);
    } else if (readable) {
      // Last writer for a buffer that needs to be sent to the |Coverage| service.
      vmo_->signal(0, buffer->readable_signal);
    }
  }
}

void SanitizerCovProxy::TraceSwitchImpl(uintptr_t pc, uint64_t val, uint64_t *cases) {
  // Switches are "special", in that their traces may be arbitrarily long based on the number of
  // different cases they have. libFuzzer ignores small switches and treats the others as two
  // comparisions against the nearest cases. This method breaks the libFuzzer abstraction and mimics
  // its switch handling. This dramatically simplifies handling the trace array, as all entries can
  // then be a fixed size. The drawback is that this method now should be kept in sync with
  // libFuzzer's FuzzerTracePC.cpp. This isn't a "must" however; in the worst case that libFuzzer's
  // implementation improves, this method won't break. It will work only as well as it does today.
  uint64_t num_cases = cases[0];
  uint64_t bits = cases[1];
  cases += 2;

  // Skip empty cases.
  if (num_cases == 0) {
    return;
  }

  // Skip the same low-signal cases as libFuzzer.
  if (val < 256 || cases[num_cases - 1] < 256) {
    return;
  }

  // Find same bounds as in libFuzzer.
  size_t i;
  uint64_t smaller = 0;
  uint64_t larger = ~(uint64_t)0;
  for (i = 0; i < num_cases; i++) {
    if (val < cases[i]) {
      larger = cases[i];
      break;
    }
    if (val > cases[i]) {
      smaller = cases[i];
    }
  }

  // Skip invalid bits.
  if (bits == 16) {
    TraceImpl(Instruction::kCmp2, pc + 2 * i, static_cast<uint16_t>(val),
              static_cast<uint16_t>(smaller));
    TraceImpl(Instruction::kCmp2, pc + 2 * i + 1, static_cast<uint16_t>(val),
              static_cast<uint16_t>(larger));
  } else if (bits == 32) {
    TraceImpl(Instruction::kCmp4, pc + 2 * i, static_cast<uint32_t>(val),
              static_cast<uint32_t>(smaller));
    TraceImpl(Instruction::kCmp4, pc + 2 * i + 1, static_cast<uint32_t>(val),
              static_cast<uint32_t>(larger));
  } else if (bits == 64) {
    TraceImpl(Instruction::kCmp8, pc + 2 * i, static_cast<uint64_t>(val),
              static_cast<uint64_t>(smaller));
    TraceImpl(Instruction::kCmp8, pc + 2 * i + 1, static_cast<uint64_t>(val),
              static_cast<uint64_t>(larger));
  } else {
    return;
  }
}

void SanitizerCovProxy::Reset() {
  // Unblock all writers
  for (size_t i = 0; i < kNumInstructionBuffers; ++i) {
    sync_completion_reset(&syncs_[i].reset);
  }
  State s(state_.fetch_and(~kMappedFlag));
  vmo_->signal(kShutdown, 0);
  {
    // Resetting the shared VMO (if present) will stop the collector.
    std::lock_guard<std::mutex> lock(lock_);
    shmem_.Reset();
  }
  for (size_t i = 0; i < kNumInstructionBuffers; ++i) {
    sync_completion_signal(&syncs_[i].write);
  }
  // Wait until they are done.
  for (size_t i = 0; i < kNumInstructionBuffers; ++i) {
    if (s.writers[i] != 0) {
      sync_completion_wait(&syncs_[i].reset, ZX_TIME_INFINITE);
      sync_completion_reset(&syncs_[i].reset);
    }
  }
  state_.store(0);
  if (collector_.joinable()) {
    collector_.join();
  }
  {
    // Disconnect from and unmap the VMOs shared with the Coverage service.
    std::lock_guard<std::mutex> lock(lock_);
    coverage_.Unbind();
    regions_.clear();
    traces_ = nullptr;
  }
  if (loop_) {
    loop_->Shutdown();
  }
}

// Private methods

SanitizerCovProxy::SanitizerCovProxy(bool autoconnect)
    : state_(0), vmo_(&shmem_.vmo()), traces_(nullptr) {
  if (autoconnect) {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    FX_CHECK(loop_->StartThread() == ZX_OK);
    auto svc = sys::ServiceDirectory::CreateFromNamespace();
    CoveragePtr coverage;
    svc->Connect(coverage.NewRequest(loop_->dispatcher()));
    FX_CHECK(SetCoverage(std::move(coverage)) == ZX_OK);
  }
}

zx_status_t SanitizerCovProxy::CreateSharedBufferLocked(const void *start, const void *end,
                                                        Buffer *out_buffer) {
  if (!start || !end || end < start) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_vaddr_t addr = reinterpret_cast<zx_vaddr_t>(start);
  size_t len = reinterpret_cast<zx_vaddr_t>(end) - addr;
  Buffer buffer;
  {
    SharedMemory *shmem = &regions_[addr];
    zx_status_t status = shmem->Create(len);
    if (status != ZX_OK) {
      return status;
    }
    status = shmem->Share(&buffer.vmo);
    if (status != ZX_OK) {
      return status;
    }
  }
  buffer.size = len;
  *out_buffer = std::move(buffer);
  return ZX_OK;
}

}  // namespace fuzzing
