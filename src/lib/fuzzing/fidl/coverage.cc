// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coverage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/object.h>
#include <zircon/status.h>

#include "libfuzzer.h"
#include "sanitizer-cov.h"

namespace fuzzing {

////////////////////////////////////////////////////////////////////////////////////////////////////
// CoverageImpl

namespace {

const zx_signals_t kItemAdded = ZX_USER_SIGNAL_7;

}  // namespace

// Public methods

CoverageImpl::CoverageImpl(AggregatedCoverage *aggregate) : aggregate_(aggregate) {}

CoverageImpl::~CoverageImpl() {}

void CoverageImpl::AddInline8BitCounters(Buffer inline_8bit_counters,
                                         AddInline8BitCountersCallback callback) {
  SharedMemory shmem;
  if (Check(shmem.Link(inline_8bit_counters.vmo, inline_8bit_counters.size))) {
    __sanitizer_cov_8bit_counters_init(shmem.begin<uint8_t>(), shmem.end<uint8_t>());
    mapped_.push_back(std::move(shmem));
    callback();
  }
}

void CoverageImpl::AddPcTable(Buffer pc_table, AddPcTableCallback callback) {
  SharedMemory shmem;
  if (Check(shmem.Link(pc_table.vmo, pc_table.size))) {
    __sanitizer_cov_pcs_init(shmem.begin<const uintptr_t>(), shmem.end<const uintptr_t>());
    mapped_.push_back(std::move(shmem));
    callback();
  }
}

void CoverageImpl::AddTraces(zx::vmo traces, AddTracesCallback callback) {
  if ((!traces_.is_mapped() || Check(ZX_ERR_BAD_STATE)) &&
      Check(traces_.Link(traces, kMaxInstructions * sizeof(Instruction))) &&
      Check(aggregate_->Add(traces_))) {
    traces_.vmo().signal(kBetweenIterations | kReadableSignalA | kReadableSignalB,
                         kInIteration | kWritableSignalA | kWritableSignalB);
    callback();
  }
}

// Private methods

bool CoverageImpl::Check(zx_status_t status) {
  if (status != ZX_OK) {
    aggregate_->Close(this, status);
    return false;
  } else {
    return true;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// AggregatedCoverage

namespace {

bool IsValidItem(zx_wait_item_t *item) {
  return item->handle != ZX_HANDLE_INVALID && (item->pending & ZX_SIGNAL_HANDLE_CLOSED) == 0 &&
         zx_object_get_info(item->handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr) ==
             ZX_OK;
}

}  // namespace

// Public methods

AggregatedCoverage::AggregatedCoverage() { Start(); }

AggregatedCoverage::~AggregatedCoverage() { Stop(); }

fidl::InterfaceRequestHandler<Coverage> AggregatedCoverage::GetHandler() {
  return [this](fidl::InterfaceRequest<Coverage> request) {
    auto coverage = std::make_unique<CoverageImpl>(this);
    bindings_.AddBinding(std::move(coverage), std::move(request));
  };
}

zx_status_t AggregatedCoverage::CompleteIteration() {
  sync_completion_reset(&sync_);
  zx_status_t status;
  if ((status = controller_.signal(0, kBetweenIterations)) != ZX_OK) {
    return status;
  }
  if ((status = sync_completion_wait(&sync_, ZX_TIME_INFINITE)) != ZX_OK ||
      (status = controller_.signal(0, kInIteration)) != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Friend methods

zx_status_t AggregatedCoverage::Add(const SharedMemory &traces) {
  std::lock_guard<std::mutex> lock(lock_);
  size_t index = num_items_.load();
  if (index == ZX_WAIT_MANY_MAX_ITEMS ||
      num_distinguishers_ == std::numeric_limits<uint16_t>::max()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // ProcessAll may have decreased num_items_, but it never increases it. Thus it's safe to use this
  // index and simply store the new value, and rely on ProcessAll to re-compact if needed.
  items_[index].handle = traces.vmo().get();
  items_[index].waitfor = kReadableSignalA;
  traces_[index] = traces.begin<Instruction>();
  distinguishers_[index] = (++num_distinguishers_) << 48;
  num_items_.store(index + 1);
  return controller_.signal(0, kItemAdded);
}

void AggregatedCoverage::Close(CoverageImpl *coverage, zx_status_t epitaph) {
  bindings_.CloseBinding(coverage, epitaph);
}

void AggregatedCoverage::Reset() {
  Stop();
  Start();
}

// Private methods

void AggregatedCoverage::Start() {
  std::lock_guard<std::mutex> lock(lock_);

  memset(items_, 0, sizeof(items_));
  num_items_.store(0);

  memset(traces_, 0, sizeof(traces_));

  memset(distinguishers_, 0, sizeof(distinguishers_));
  num_distinguishers_ = 0;

  ZX_ASSERT(zx::event::create(0, &controller_) == ZX_OK);
  ZX_ASSERT(controller_.signal(0, kInIteration) == ZX_OK);
  items_[0].handle = controller_.get();
  items_[0].waitfor = kItemAdded | kInIteration | kBetweenIterations | kShutdown;
  num_items_.fetch_add(1);

  processor_ = std::thread([this]() { ProcessAll(); });
}

void AggregatedCoverage::ProcessAll() {
  bool in_iteration = true;
  while (true) {
    size_t num_items = num_items_.load();
    zx_status_t status = zx_object_wait_many(items_, num_items, ZX_TIME_INFINITE);
    switch (status) {
      case ZX_ERR_BAD_HANDLE:
      case ZX_ERR_CANCELED: {
        // One or more items are no longer valid. Compact the array into only valid items.
        size_t orig_num_items = num_items;
        do {
          // Loop invariants:
          //   items_[1:i] are valid.
          //   items_[num_items:orig_num_items] have been reset.
          for (size_t i = 1; i < num_items;) {
            // Keep valid items.
            if (IsValidItem(&items_[i])) {
              i++;
              continue;
            }
            // Found invalid item; replace with trailing item.
            --num_items;
            if (i != num_items) {
              items_[i] = std::move(items_[num_items]);
              traces_[i] = traces_[num_items];
              distinguishers_[i] = distinguishers_[num_items];
            }
            // Reset the trailing item.
            items_[num_items].handle = ZX_HANDLE_INVALID;
            traces_[num_items] = nullptr;
            distinguishers_[num_items] = 0;
          }
          // If more items were added in the meantime, loop and compact again.
        } while (!num_items_.compare_exchange_weak(orig_num_items, num_items));
        break;
      }
      case ZX_OK: {
        // First check the control object.
        zx_wait_item_t *item = &items_[0];
        if (item->pending & kItemAdded) {
          // New item added; interrupt all waiters and loop.
          controller_.signal(kItemAdded, 0);
          for (size_t i = 1; i < num_items; ++i) {
            zx_object_signal(items_[i].handle, 0, in_iteration ? kInIteration : kBetweenIterations);
          }
          break;

        } else if (item->pending & kInIteration) {
          // Beginning new iteration; let all waiters know.
          controller_.signal(kInIteration, 0);
          for (size_t i = 1; i < num_items; ++i) {
            zx_object_signal(items_[i].handle, kBetweenIterations, kInIteration);
          }
          in_iteration = true;
          break;

        } else if (item->pending & kBetweenIterations) {
          // Ending iteration; let all waiters know.
          controller_.signal(kBetweenIterations, 0);
          pending_.store(num_items - 1);
          for (size_t i = 1; i < num_items; ++i) {
            zx_object_signal(items_[i].handle, kInIteration, kBetweenIterations);
          }
          in_iteration = false;
          break;

        } else if (item->pending & kShutdown) {
          // Shutting down; exit loop.
          return;
        }
        // Check remaining objects.
        for (size_t i = 1; i < num_items; ++i) {
          zx_wait_item_t *item = &items_[i];
          Instruction *traces = traces_[i];
          uint64_t distinguisher = distinguishers_[i];

          if (item->waitfor & item->pending & kReadableSignalA) {
            // Expected to see kReadableSignalA, and saw it.
            ProcessTraces(&traces[0], distinguisher);
            item->waitfor = (item->waitfor & ~kReadableSignalA) | kReadableSignalB;
            zx_object_signal(item->handle, kReadableSignalA, kWritableSignalA);

          } else if (item->waitfor & item->pending & kReadableSignalB) {
            // Expected to see kReadableSignalB, and saw it.
            ProcessTraces(&traces[kInstructionBufferLen], distinguisher);
            item->waitfor = (item->waitfor & ~kReadableSignalB) | kReadableSignalA;
            zx_object_signal(item->handle, kReadableSignalB, kWritableSignalB);
          }
        }
        break;
      }
      default: {
        FX_NOTREACHED();
      }
    }
  }
}

void AggregatedCoverage::ProcessTraces(Instruction *traces, uint64_t distinguisher) {
  for (size_t i = 0; i < kInstructionBufferLen; ++i) {
    Instruction *trace = &traces[i];
    if (trace->type == Instruction::kSentinel) {
      size_t prev_pending = pending_.fetch_sub(1);
      ZX_ASSERT(prev_pending != 0);
      if (prev_pending == 1) {
        sync_completion_signal(&sync_);
      }
      break;
    }
    LLVMFuzzerSetRemoteCallerPC(trace->pc ^ distinguisher);
    switch (trace->type) {
      case Instruction::kPcIndir:
        __sanitizer_cov_trace_pc_indir(static_cast<uintptr_t>(trace->args[0]));
        break;
      case Instruction::kCmp8:
        __sanitizer_cov_trace_cmp8(trace->args[0], trace->args[1]);
        break;
      case Instruction::kConstCmp8:
        __sanitizer_cov_trace_const_cmp8(trace->args[0], trace->args[1]);
        break;
      case Instruction::kCmp4:
        __sanitizer_cov_trace_cmp4(static_cast<uint32_t>(trace->args[0]),
                                   static_cast<uint32_t>(trace->args[1]));
        break;
      case Instruction::kConstCmp4:
        __sanitizer_cov_trace_const_cmp4(static_cast<uint32_t>(trace->args[0]),
                                         static_cast<uint32_t>(trace->args[1]));
        break;
      case Instruction::kCmp2:
        __sanitizer_cov_trace_cmp2(static_cast<uint16_t>(trace->args[0]),
                                   static_cast<uint16_t>(trace->args[1]));
        break;
      case Instruction::kConstCmp2:
        __sanitizer_cov_trace_const_cmp2(static_cast<uint16_t>(trace->args[0]),
                                         static_cast<uint16_t>(trace->args[1]));
        break;
      case Instruction::kCmp1:
        __sanitizer_cov_trace_cmp1(static_cast<uint8_t>(trace->args[0]),
                                   static_cast<uint8_t>(trace->args[1]));
        break;
      case Instruction::kConstCmp1:
        __sanitizer_cov_trace_const_cmp1(static_cast<uint8_t>(trace->args[0]),
                                         static_cast<uint8_t>(trace->args[1]));
        break;
      case Instruction::kDiv8:
        __sanitizer_cov_trace_div8(trace->args[0]);
        break;
      case Instruction::kDiv4:
        __sanitizer_cov_trace_div4(static_cast<uint32_t>(trace->args[0]));
        break;
      case Instruction::kGep:
        __sanitizer_cov_trace_gep(static_cast<uintptr_t>(trace->args[0]));
        break;
      default:
        FX_NOTREACHED();
    }
  }
}

void AggregatedCoverage::Stop() {
  std::lock_guard<std::mutex> lock(lock_);
  controller_.signal(0, kShutdown);
  ZX_ASSERT(processor_.joinable());
  processor_.join();
  controller_.reset();
  sync_completion_signal(&sync_);
  bindings_.CloseAll();
  pending_.store(0);
}

// Private methods

}  // namespace fuzzing
