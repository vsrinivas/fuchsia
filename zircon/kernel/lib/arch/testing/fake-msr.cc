// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-msr.h>
#include <zircon/assert.h>

#include <fbl/alloc_checker.h>

namespace arch::testing {

FakeMsrIo& FakeMsrIo::Populate(X86Msr msr, uint64_t initial_value) {
  if (auto it = map_.find(msr); it != map_.end()) {
    it->value_ = initial_value;
    return *this;
  }
  fbl::AllocChecker ac;
  std::unique_ptr<Hashable> hashable(new (&ac) Hashable{});
  ZX_ASSERT(ac.check());
  hashable->msr_ = msr;
  hashable->value_ = initial_value;
  map_.insert(std::move(hashable));
  return *this;
}

uint64_t FakeMsrIo::Peek(X86Msr msr) const {
  auto it = map_.find(msr);
  ZX_ASSERT_MSG(it != map_.end(),
                "FakeMsrIo::Populate() must be called with MSR %#x before it can be peeked",
                static_cast<uint32_t>(msr));
  return it->value_;
}

void FakeMsrIo::Write(X86Msr msr, uint64_t value) {
  auto it = map_.find(msr);
  ZX_ASSERT_MSG(it != map_.end(),
                "FakeMsrIo::Populate() must be called with MSR %#x before it can be written to",
                static_cast<uint32_t>(msr));
  it->value_ = value;
  on_write_(msr, it->value_);
}

uint64_t FakeMsrIo::Read(X86Msr msr) {
  auto it = map_.find(msr);
  ZX_ASSERT_MSG(it != map_.end(),
                "FakeMsrIo::Populate() must be called with MSR %#x before it can be read from",
                static_cast<uint32_t>(msr));
  uint64_t& value = it->value_;
  on_read_(msr, value);
  return value;
}

}  // namespace arch::testing
