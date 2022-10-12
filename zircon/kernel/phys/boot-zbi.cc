// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/boot-zbi.h"

#include <inttypes.h>
#include <lib/arch/zbi-boot.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/item.h>
#include <zircon/boot/image.h>

#include <phys/stdio.h>
#include <phys/symbolize.h>
#include <pretty/cpp/sizes.h>

namespace {

constexpr fit::error<BootZbi::Error> InputError(BootZbi::InputZbi::Error error) {
  return fit::error{BootZbi::Error{
      .zbi_error = error.zbi_error,
      .read_offset = error.item_offset,
  }};
}

constexpr fit::error<BootZbi::Error> EmptyZbi(fit::result<BootZbi::InputZbi::Error> result) {
  if (result.is_error()) {
    return InputError(result.error_value());
  }
  return fit::error{BootZbi::Error{"empty ZBI"}};
}

constexpr fit::error<BootZbi::Error> OutputError(BootZbi::Zbi::Error error) {
  return fit::error{BootZbi::Error{
      .zbi_error = error.zbi_error,
      .write_offset = error.item_offset,
  }};
}

constexpr fit::error<BootZbi::Error> OutputError(
    BootZbi::InputZbi::CopyError<BootZbi::Bytes> error) {
  return fit::error{BootZbi::Error{
      .zbi_error = error.zbi_error,
      .read_offset = error.read_offset,
      .write_offset = error.write_offset,
  }};
}

const zircon_kernel_t* GetZirconKernel(const ktl::byte* kernel_payload) {
  return reinterpret_cast<const zircon_kernel_t*>(
      // The payload is the kernel item contents, i.e. the zbi_kernel_t header
      // followed by the rest of the load image.  But the actual kernel load
      // image for purposes of address arithmetic is defined as being the whole
      // ZBI container, i.e. the whole zircon_kernel_t enchilada that has the
      // ZBI file (container) zbi_header_t followed by the kernel item's
      // zbi_header_t followed by that payload.  In a proper bootable ZBI, the
      // kernel item must be first and so kernel_ could always just be set to
      // zbi_.storage().data().  However, Init() permits synthetic
      // ZBI_TYPE_DISCARD items at the start to be left by previous boot shim
      // code and hence the kernel item might not be the first item in the
      // container here. So, instead calculate the offset back from this
      // payload in memory to where the beginning of the whole container would
      // be: thus the zircon_kernel_t pointer here finds the zbi_kernel_t
      // payload in the right place in memory, and the kernel item zbi_header_t
      // before it.  Nothing in the kernel boot protocol actually cares about
      // looking at the container zbi_header_t (or the kernel item
      // zbi_header_t, for that matter), they are just accounted for in the
      // address arithmetic to simplify the normal way a boot loader does the
      // loading.  The later uses of kernel_ in Load() and elsewhere likewise
      // don't care about those headers, only about the zbi_kernel_t portion
      // and the aligned physical memory address that corresponds to the
      // zircon_kernel_t pointer.  Unlike the formal ZBI boot protocol, the
      // Load() code handles the case where this address is not properly
      // aligned for the kernel handoff; but in the likely event that this
      // initial address is actually aligned, Load() may be able to avoid
      // additional memory allocation and copying.
      kernel_payload - (2 * sizeof(zbi_header_t)));
}

}  // namespace

BootZbi::Size BootZbi::SuggestedAllocation(uint32_t zbi_size_bytes) {
  return {.size = zbi_size_bytes, .alignment = arch::kZbiBootKernelAlignment};
}

BootZbi::Size BootZbi::GetKernelAllocationSize(BootZbi::Zbi::iterator kernel_item) {
  ZX_ASSERT(kernel_item->header->type == ZBI_TYPE_STORAGE_KERNEL);
  uint64_t kernel_without_reserve = zbitl::UncompressedLength(*kernel_item->header);
  auto zircon_kernel = GetZirconKernel(kernel_item->payload.data());
  return SuggestedAllocation(static_cast<uint32_t>(kernel_without_reserve +
                                                   zircon_kernel->data_kernel.reserve_memory_size));
}

fit::result<BootZbi::Error> BootZbi::InitKernelFromItem() {
  kernel_ = GetZirconKernel(kernel_item_->payload.data());
  return fit::ok();
}

fit::result<BootZbi::Error> BootZbi::Init(InputZbi arg_zbi) {
  // Move the incoming zbitl::View into the object before using
  // iterators into it.
  zbi_ = std::move(arg_zbi);

  auto it = zbi_.begin();
  if (it == zbi_.end()) {
    return EmptyZbi(zbi_.take_error());
  }

  while (it != zbi_.end()) {
    auto [header, payload] = *it;

    switch (header->type) {
      case arch::kZbiBootKernelType: {
        kernel_item_ = it;
        // Valid kernel_item implies no iteration error.
        zbi_.ignore_error();
        return InitKernelFromItem();
      }

      case ZBI_TYPE_DISCARD:
        // A boot shim might leave a dummy item at the start.  Allow it.
        ++it;
        continue;
    }
    // Any other item should not be the first item seen.
    break;
  }

  if (auto result = zbi_.take_error(); result.is_error()) {
    return InputError(result.error_value());
  }

  return fit::error{Error{
      .zbi_error = "ZBI does not start with valid kernel item",
      .read_offset =
          it == zbi_.end() ? static_cast<uint32_t>(sizeof(zbi_header_t)) : it.item_offset(),
  }};
}

fit::result<BootZbi::Error> BootZbi::Init(InputZbi arg_zbi, InputZbi::iterator kernel_item) {
  zbi_ = std::move(arg_zbi);
  kernel_item_ = zbi_.begin();
  while (true) {
    if (kernel_item_ == zbi_.end()) {
      return InputError(zbi_.take_error().error_value());
    }
    if (kernel_item_.item_offset() == kernel_item.item_offset()) {
      break;
    }
    ++kernel_item_;
  }
  // Valid kernel_item implies no error.
  zbi_.ignore_error();
  return InitKernelFromItem();
}

bool BootZbi::KernelCanLoadInPlace() const {
  // The kernel (container header) must be aligned as per the ZBI protocol.
  if (KernelLoadAddress() % arch::kZbiBootKernelAlignment != 0) {
    return false;
  }

  // If we have relocated the kernel, then it will already be in-place.
  if (kernel_buffer_) {
    ZX_DEBUG_ASSERT(kernel_buffer_.size_bytes() >= KernelMemorySize());
    return true;
  }

  // The incoming ZBI must supply enough reusable headroom for the kernel.
  uint32_t in_place_start =
      kernel_item_.item_offset() - static_cast<uint32_t>(sizeof(zbi_header_t));
  uint32_t in_place_space = static_cast<uint32_t>(zbi_.storage().size()) - in_place_start;
  return in_place_space >= KernelMemorySize();
}

bool BootZbi::FixedKernelOverlapsData(uint64_t kernel_load_address) const {
  uint64_t start1 = kernel_load_address;
  uint64_t start2 = reinterpret_cast<uintptr_t>(data_.storage().data());
  uint64_t end1 = start1 + KernelMemorySize();
  uint64_t end2 = start2 + data_.storage().size();
  return start1 <= start2 ? start2 < end1 : start1 < end2;
}

fit::result<BootZbi::Error> BootZbi::Load(uint32_t extra_data_capacity,
                                          ktl::optional<uintptr_t> kernel_load_address,
                                          ktl::optional<uintptr_t> data_load_address) {
  ZX_ASSERT(data_.storage().empty());

  auto input_address = reinterpret_cast<uintptr_t>(zbi_.storage().data());
  auto input_capacity = zbi_.storage().size();

  auto it = kernel_item_;
  ++it;

  // Init() has identified the kernel item in the input ZBI.  We now have an
  // image in memory and know what its pieces are:
  //
  //  zbi_.storage().data() -> offset 0: zbi_header_t (ZBI_TYPE_CONTAINER)
  //                          ~~~
  //         kernel_item_.item_offset(): zbi_header_t (ZBI_TYPE_KERNEL_*)
  //                  .payload_offset(): zbi_kernel_t
  //                                     ...kernel load image...
  //                   it.item_offset(): zbi_header_t       (first data item)
  //                  .payload_offset(): data payload...    (first data item)
  //                                     ...                (first data item)
  //                                     zbi_header_t       (second data item)
  //                                     data payload...    (second data item)
  //                                     ...                (second data item)
  //                                          .
  //                                          .
  //                                          .
  //                                     zbi_header_t       (last data item)
  //                                     data payload ...   (last data item)
  //  input_address + zbi_.size_bytes(): <end of input ZBI as loaded>
  //                          ~~~
  //  input_address +    input_capacity: <end of known-available extra memory>
  //
  // To meet the ZBI boot protocol, we're transforming that into two separate
  // contiguous blocks of memory, each of which can be located anywhere.
  //
  // Legend: KLA = KernelLoadAddress(), KH = KernelHeader()
  //         KLS = KernelLoadSize(), KMS = KernelMemorySize()
  //         DLA = DataLoadAddress(), DLS = DataLoadSize()
  //         EDC = extra_data_capacity argument to Load()
  //
  // Kernel memory image, aligned to arch::kZbiBootKernelAlignment:
  //
  //                     KLA +  0: zbi_header_t (ZBI_TYPE_CONTAINER, ignored)
  //                     KLA + 32: zbi_header_t (ZBI_TYPE_KERNEL_*, ignored)
  //                KH = KLA + 64: zbi_kernel_t
  //                               ...start of kernel load image proper...
  //                               .
  //              KLA + KH->entry: kernel entry point instruction
  //                               .
  //                               ...more kernel load image...
  //                               .
  //                    KLA + KLS: ...zbi_kernel_t.reserve_memory_size bytes...
  //                    KLA + KMS: <end of kernel memory image>
  //
  // Data ZBI image, aligned to arch::kZbiBootDataAlignment:
  //
  //                     DLA +  0: zbi_header_t       (ZBI_TYPE_CONTAINER)
  //                     DLA + 32: zbi_header_t       (first data item)
  //                     DLA + 64: data payload...    (first data item)
  //                               ...                (first data item)
  //                               zbi_header_t       (second data item)
  //                               data payload...    (second data item)
  //                               ...                (second data item)
  //                                    .
  //                                    .
  //                                    .
  //                               zbi_header_t       (last original data item)
  //                               data payload ...   (last original data item)
  //                    DLA + DLS: <zbi_header_t>     (caller fills in later)
  //                               <data payload...>  (caller fills in later)
  //                                    .
  //                                    .
  //                                    .
  //                               <zbi_header_t>     (last shim-added item)
  //                               <data payload...>  (last shim-added item)
  //              DLA + DLS + EDC: <end of DataZbi().storage() capacity>
  //
  // In a proper bootable ZBI in its original state, the kernel item must be
  // the first item so kernel_item_.item_offset() is 32 (sizeof(zbi_header_t)).
  // However, this code supports uses in boot shims that had other fish to fry
  // first and so Init() allowed any number of ZBI_TYPE_DISCARD items at the
  // start before kernel_item_.  In that case kernel_ now points 32 bytes back
  // into the ~~~ discard area.  When there are no discard items, then kernel_
  // now points directly at the start of the container; if this is already
  // aligned to arch::kZbiBootKernelAlignment, then it may be possible to use
  // it where it is.
  //
  // In the kernel memory image, the ZBI headers and the zbi_kernel_t payload
  // header are only there for the convenience of the boot loader (or shim,
  // i.e. this code).  The loaded kernel code doesn't actually care about any
  // what's in that memory.  It's just part of the address arithmetic for the
  // kernel to calculate its own aligned load address (KLA) from the runtime PC
  // value at its entry point (KLA + KH->entry).  However, for the data ZBI,
  // the container header must be well-formed and give the correct length since
  // it is the only means to communicate the size of the data ZBI, and all data
  // item headers must be well-formed items understood (or at least tolerated)
  // by the system being booted.  (Items added by boot loaders do not
  // necessarily having strictly valid ZBI item headers beyond the basic length
  // and type fields, so permissive checking mode is used.)
  //
  // In the general case, we can just allocate two new separate blocks of
  // memory and copy the kernel and data images into them respectively.  But
  // when possible we can optimize the total memory use by reusing some or all
  // of the space where the input ZBI (and any excess capacity known to follow
  // it) sits, and/or optimize CPU time by leaving either the kernel load image
  // or the data items' image where it is rather than copying it.  This code
  // calculates what options are available based on the sizes and alignments
  // required and then chooses the option that copies the fewest bytes.  To
  // increase the cases where copying can be avoided, this can yield a data ZBI
  // that actually has a ZBI_TYPE_DISCARD item inserted before the first real
  // data item from the input ZBI just to make alignment arithmetic line up.

  uintptr_t data_address = 0, aligned_data_address = 0;
  uint32_t data_load_size = sizeof(zbi_header_t);
  if (it != zbi_.end()) {
    data_address = input_address + it.item_offset() - sizeof(zbi_header_t);
    aligned_data_address = data_address & -arch::kZbiBootDataAlignment;
    data_load_size =
        static_cast<uint32_t>(zbi_.size_bytes() - it.item_offset()) + sizeof(zbi_header_t);
  }

  // There must be a container header for the data ZBI even if it's empty.
  const uint32_t data_required_size = data_load_size + extra_data_capacity;

  // The incoming space can be reused for the data ZBI if either the tail is
  // already exactly aligned to leave space for a header with correct
  // alignment, or there's enough space to insert a ZBI_TYPE_DISCARD item after
  // an aligned header.
  if (data_address != 0 && data_address % arch::kZbiBootDataAlignment == 0) {
    // It so happens it's perfectly aligned to use the whole thing in place.
    // The lower pages used for the kernel image will just be skipped over.
    data_.storage() = {
        reinterpret_cast<std::byte*>(data_address),
        input_capacity - (data_address - input_address),
    };
  } else if (aligned_data_address > input_address &&
             data_address - aligned_data_address >= (2 * sizeof(zbi_header_t))) {
    // Aligning down leaves enough space to insert a ZBI header to consume
    // the remaining space with a ZBI_TYPE_DISCARD item so the actual
    // contents can be left in place.
    data_.storage() = {
        reinterpret_cast<std::byte*>(aligned_data_address),
        input_capacity - (aligned_data_address - input_address),
    };
  }

  if (kernel_load_address && FixedKernelOverlapsData(*kernel_load_address)) {
    // There's a fixed kernel load address, so the data ZBI cannot be allowed
    // to reuse the memory where it will go.  This memory will already have
    // been reserved from the allocator, but the incoming data might be there.
    data_.storage() = {};
  }

  // If we can reuse either the kernel image or the data ZBI items in place,
  // choose whichever makes for less copying.
  if (input_address + input_capacity - data_address < data_required_size ||
      (KernelCanLoadInPlace() && KernelLoadSize() < data_load_size)) {
    data_.storage() = {};
  }

  // If we are relocating the data zbi, and the destination data overlaps with the kernel current
  // location, we need to relocate the kernel image, to avoid clubbering the kernel data, by copying
  // the data zbi over it.
  bool relocated_data_overlaps_with_kernel =
      data_load_address.has_value() &&
      *data_load_address >= KernelLoadAddress() + KernelLoadSize() - DataLoadSize();

  if (!KernelCanLoadInPlace() || !data_.storage().empty() || relocated_data_overlaps_with_kernel) {
    // Allocate space for the kernel image and copy it in.
    fbl::AllocChecker ac;
    kernel_buffer_ =
        Allocation::New(ac, memalloc::Type::kKernel, static_cast<size_t>(KernelMemorySize()),
                        arch::kZbiBootKernelAlignment);
    if (!ac.check()) {
      return fit::error{Error{
          .zbi_error = "cannot allocate memory for kernel image",
          .write_offset = static_cast<uint32_t>(KernelMemorySize()),
      }};
    }
    memcpy(kernel_buffer_.get(), KernelImage(), KernelLoadSize());
    kernel_ = reinterpret_cast<zircon_kernel_t*>(kernel_buffer_.get());
  }

  if (data_.storage().empty()) {
    // Allocate new space for the data ZBI and copy it over.
    fbl::AllocChecker ac;
    data_buffer_ = Allocation::New(ac, memalloc::Type::kDataZbi, data_required_size,
                                   arch::kZbiBootDataAlignment);
    if (!ac.check()) {
      return fit::error{Error{
          .zbi_error = "cannot allocate memory for data ZBI",
          .write_offset = data_required_size,
      }};
    }
    data_.storage() = data_buffer_.data();
    if (auto result = data_.clear(); result.is_error()) {
      return OutputError(result.error_value());
    }
    if (auto result = data_.Extend(it, zbi_.end()); result.is_error()) {
      return OutputError(result.error_value());
    }
  } else if (data_address % arch::kZbiBootDataAlignment == 0) {
    // The data ZBI is perfect where it is.  Just overwrite where the end
    // of the kernel item was copied from with the new container header.
    auto hdr = reinterpret_cast<zbi_header_t*>(data_.storage().data());
    hdr[0] = ZBI_CONTAINER_HEADER(static_cast<uint32_t>(data_load_size - sizeof(zbi_header_t)));
  } else {
    // There's an aligned spot before the data ZBI's first item where we can
    // insert both a new container header and an item header to sop up the
    // remaining space before the first item without copying any data.
    auto hdr = reinterpret_cast<zbi_header_t*>(data_.storage().data());
    auto discard_size = data_address - aligned_data_address - sizeof(hdr[1]);
    auto data_size = data_load_size + sizeof(hdr[1]) + discard_size;
    ZX_ASSERT(aligned_data_address > input_address);
    ZX_ASSERT(data_address > aligned_data_address);
    ZX_ASSERT(data_address - aligned_data_address >= sizeof(hdr[1]));
    ZX_ASSERT(discard_size < data_size);
    hdr[0] = ZBI_CONTAINER_HEADER(static_cast<uint32_t>(data_size - sizeof(zbi_header_t)));
    hdr[1] = zbitl::SanitizeHeader({
        .type = ZBI_TYPE_DISCARD,
        .length = static_cast<uint32_t>(discard_size),
    });
  }

  ZX_ASSERT(KernelCanLoadInPlace());
  ZX_ASSERT(data_.storage().size() >= data_required_size);
  ZX_ASSERT(data_.storage().size() - data_.size_bytes() >= extra_data_capacity);
  return fit::ok();
}

void BootZbi::Log() {
  LogAddresses();
  LogBoot(KernelEntryAddress());
}

[[noreturn]] void BootZbi::Boot(ktl::optional<void*> argument) {
  ZX_ASSERT_MSG(KernelCanLoadInPlace(), "Has Load() been called?");
  Log();
  auto kernel_hdr = const_cast<zircon_kernel_t*>(kernel_);
  arch::ZbiBoot(kernel_hdr, argument.value_or(data_.storage().data()));
}

#define ADDR "0x%016" PRIx64

void BootZbi::LogAddresses() {
  debugf("%s:    Kernel @ [" ADDR ", " ADDR ")  %s\n", ProgramName(), KernelLoadAddress(),
         KernelLoadAddress() + KernelLoadSize(), pretty::FormattedBytes(KernelLoadSize()).c_str());
  debugf("%s:       BSS @ [" ADDR ", " ADDR ")  %s\n", ProgramName(),
         KernelLoadAddress() + KernelLoadSize(), KernelLoadAddress() + KernelMemorySize(),
         pretty::FormattedBytes(static_cast<size_t>(KernelHeader()->reserve_memory_size)).c_str());
  debugf("%s:       ZBI @ [" ADDR ", " ADDR ")  %s\n", ProgramName(), DataLoadAddress(),
         DataLoadAddress() + DataLoadSize(),
         pretty::FormattedBytes(static_cast<size_t>(DataLoadSize())).c_str());
}

void BootZbi::LogBoot(uint64_t entry) const {
  debugf("%s:     Entry @  " ADDR "  Booting...\n", ProgramName(), entry);
}
