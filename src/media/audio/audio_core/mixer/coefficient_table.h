// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace media::audio::mixer {

// CoefficientTable is a shim around std::vector that maps indicies into a physical addressing
// scheme that is most optimal WRT to how this table is typically accessed. Specifically accesses
// are most commonly with an integral stride (that is 1 << frac_bits stride). Optimize for this use
// case by placing these values physically contiguously in memory.
//
// Note we still expose iterators, but the order is no longer preserved (iteration will be performed
// in physical order).
class CoefficientTable {
 public:
  // |width| is the filter width of this table, in fixed point format with |frac_bits| of fractional
  // precision. The |width| will determine the number of entries in the table, which will be |width|
  // rounded up to the nearest integer in the same fixed-point format.
  CoefficientTable(uint32_t width, uint32_t frac_bits)
      : stride_(ComputeStride(width, frac_bits)),
        frac_bits_(frac_bits),
        frac_mask_((1 << frac_bits_) - 1),
        table_(stride_ * (1 << frac_bits)) {}

  float& operator[](uint32_t offset) { return table_[PhysicalIndex(offset)]; }

  const float& operator[](uint32_t offset) const { return table_[PhysicalIndex(offset)]; }

  auto begin() { return table_.begin(); }
  auto end() { return table_.end(); }

  size_t PhysicalIndex(uint32_t offset) const {
    auto integer = offset >> frac_bits_;
    auto fraction = offset & frac_mask_;
    return fraction * stride_ + integer;
  }

 private:
  static uint32_t ComputeStride(uint32_t filter_width, uint32_t frac_bits) {
    return (filter_width + ((1 << frac_bits) - 1)) / (1 << frac_bits);
  }

  const uint32_t stride_;
  const uint32_t frac_bits_;
  const uint32_t frac_mask_;
  std::vector<float> table_;
};

// A cache of CoefficientTables. These use a lot of memory so we try to reuse them as much as
// possible. For example, different Filters might use the same underlying coefficient table with
// slightly different Filter parameters. Additionally, different mixers might use the same Filter.
//
// InputT defines the set of inputs that are used to construct the CoefficientTable.
template <class InputT>
class CoefficientTableCache {
 public:
  // Thread-safe reference-counted pointer to a cached CoefficientTable.
  // This is like a std::shared_ptr, except the destructor runs atomically with Get()
  // to simplify cache garbage collection.
  class SharedPtr {
   public:
    SharedPtr() : ptr_(nullptr) {}
    SharedPtr(SharedPtr&& r) : ptr_(r.ptr_), drop_(std::move(r.drop_)) { r.ptr_ = nullptr; }

    ~SharedPtr() {
      if (ptr_) {
        drop_();
      }
    }

    SharedPtr(const SharedPtr& r) = delete;
    SharedPtr& operator=(const SharedPtr& r) = delete;
    SharedPtr& operator=(SharedPtr&& r) {
      if (ptr_) {
        drop_();
      }
      ptr_ = r.ptr_;
      drop_ = std::move(r.drop_);
      r.ptr_ = nullptr;
      return *this;
    }

    CoefficientTable* get() const { return ptr_; }
    operator bool() const { return ptr_ != nullptr; }

   private:
    friend class CoefficientTableCache<InputT>;
    SharedPtr(CoefficientTable* p, fit::function<void()> drop) : ptr_(p), drop_(std::move(drop)) {
      FX_CHECK(p);
    }

    CoefficientTable* ptr_;
    fit::function<void()> drop_;
  };

  explicit CoefficientTableCache(fit::function<CoefficientTable*(const InputT&)> create_table)
      : create_table_(std::move(create_table)) {}

  // Get returns a cached table for the given inputs, or if a cached tabled does not exist, a new
  // table is created and stored in the cache.
  SharedPtr Get(InputT inputs) {
    // Don't use a locker here so we can release mutex_ before creating a new table.
    // This allows multiple threads to create tables concurrently.
    mutex_.lock();

    // std::map guarantees that iterators are not invalidated until erased.
    // We hold onto this iterator until the reference is dropped.
    auto lookup_result = cache_.insert(std::make_pair(inputs, std::make_unique<Entry>()));
    auto it = lookup_result.first;

    std::lock_guard<std::mutex> entry_locker(it->second->mutex);
    mutex_.unlock();

    it->second->ref_cnt++;
    if (!it->second->table) {
      FX_DCHECK(lookup_result.second);
      FX_CHECK(it->second->ref_cnt == 1);
      it->second->table = create_table_(inputs);
    } else {
      FX_DCHECK(!lookup_result.second);
    }

    return SharedPtr(it->second->table, [this, it]() {
      std::lock_guard<std::mutex> cache_locker(mutex_);
      it->second->mutex.lock();
      it->second->ref_cnt--;
      if (it->second->ref_cnt == 0) {
        delete it->second->table;
        it->second->mutex.unlock();
        cache_.erase(it);
      } else {
        it->second->mutex.unlock();
      }
    });
  }

 private:
  struct Entry {
    std::mutex mutex;
    size_t ref_cnt FXL_GUARDED_BY(mutex) = 0;
    CoefficientTable* table FXL_GUARDED_BY(mutex) = nullptr;
  };

  std::mutex mutex_;
  std::map<InputT, std::unique_ptr<Entry>> cache_ FXL_GUARDED_BY(mutex_);
  fit::function<CoefficientTable*(InputT)> create_table_;
};

// LazySharedCoefficientTable is a wrapper around CoefficientTables that are constructed lazily.
// This is a simple way to construct a CoefficientTable table in any thread (such as the FIDL loop
// thread) but delay the potentially-expensive step of building the table until the table is
// actually needed, possibly on another thread.
template <class InputT>
class LazySharedCoefficientTable {
 public:
  using CacheT = CoefficientTableCache<InputT>;
  LazySharedCoefficientTable(CacheT* cache, InputT inputs) : cache_(cache), inputs_(inputs) {}

  LazySharedCoefficientTable(const LazySharedCoefficientTable&) = delete;
  LazySharedCoefficientTable& operator=(const LazySharedCoefficientTable&) = delete;

  CoefficientTable* get() {
    if (unlikely(!ptr_)) {
      ptr_ = cache_->Get(inputs_);
    }
    return ptr_.get();
  }

  CoefficientTable& operator*() { return *get(); }

 private:
  CacheT* cache_;
  InputT inputs_;
  typename CacheT::SharedPtr ptr_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
