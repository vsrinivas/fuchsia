// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-audio-descriptors.h"

#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <pretty/hexdump.h>

#include "debug-logging.h"

namespace audio {
namespace usb {

DescriptorListMemory::~DescriptorListMemory() {
  // According to docs in the header, data allocated using
  // usb_get_descriptor_list should be free'ed using a standard C "free"
  if (data_ != nullptr) {
    free(data_);
  }
}

fbl::RefPtr<DescriptorListMemory> DescriptorListMemory::Create(usb_protocol_t* proto) {
  ZX_DEBUG_ASSERT(proto != nullptr);

  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) DescriptorListMemory());
  if (!ac.check()) {
    GLOBAL_LOG(ERROR, "Failed to allocate descriptor list memory!");
    return nullptr;
  }

  size_t desc_length = usb_get_descriptors_length(proto);
  ret->data_ = malloc(desc_length);
  if (!ret->data_) {
    return nullptr;
  }
  usb_get_descriptors(proto, ret->data_, desc_length, &ret->size_);

  if (zxlog_level_enabled(SPEW)) {
    GLOBAL_LOG(SPEW, "Descriptor List is %zu bytes long\n", ret->size_);
    hexdump8_ex(ret->data_, ret->size_, 0u);
  }

  return ret;
}

DescriptorListMemory::Iterator::Iterator(fbl::RefPtr<DescriptorListMemory> mem)
    : mem_(std::move(mem)) {
  ZX_DEBUG_ASSERT(mem_ != nullptr);
  // Make sure our offset is valid, or go ahead and invalidate it right from
  // the start.
  ValidateOffset();
}

bool DescriptorListMemory::Iterator::Next() {
  auto h = hdr();

  // Have we already run out of headers?
  if (h == nullptr) {
    return false;
  }

  // Advance to the next header, then validate our offset.  Note that there is
  // no overflow check here.  If we overflow a 64 bit size_t, the implication
  // is that we are holding a USB descriptor list in RAM whose size is within
  // 256 bytes of our entire 64 bit address space.  This really should be
  // impossible, so we don't bother to check.
  offset_ += h->bLength;
  return ValidateOffset();
}

bool DescriptorListMemory::Iterator::ValidateOffset() {
  ZX_DEBUG_ASSERT(offset_ <= mem_->size());
  size_t space = mem_->size() - offset_;

  if (!space) {
    return false;
  }

  // If anything goes wrong from here on out, make sure to invalidate our offset.
  auto cleanup = fbl::MakeAutoCall([this] { offset_ = mem_->size(); });

  if (space < sizeof(usb_descriptor_header_t)) {
    GLOBAL_LOG(WARN,
               "Insufficient space at offset %zu to contain even the most basic USB descriptor "
               "(space needed %zu, space left %zu)\n",
               offset_, sizeof(usb_descriptor_header_t), space);
    return false;
  }

  auto tmp = reinterpret_cast<uintptr_t>(mem_->data());
  auto h = reinterpret_cast<const usb_descriptor_header_t*>(tmp + offset_);
  if (h->bLength > space) {
    GLOBAL_LOG(WARN,
               "Malformed USB descriptor header (type %u) at offset %zu.  "
               "Header indicates that it is %u bytes long, but there %zu bytes remaining\n",
               h->bDescriptorType, offset_, h->bLength, space);
    return false;
  }

  GLOBAL_LOG(SPEW, "Found Descriptor [type 0x%02x, len 0x%02x] at offset 0x%zx/0x%zx\n",
             h->bDescriptorType, h->bLength, offset_, mem_->size());

  cleanup.cancel();
  return true;
}

}  // namespace usb
}  // namespace audio
