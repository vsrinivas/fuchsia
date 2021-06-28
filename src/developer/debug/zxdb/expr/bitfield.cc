// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/bitfield.h"

#include "src/developer/debug/zxdb/common/int128_t.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/found_member.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

namespace {

// We use 128-bit numbers for bitfield computations so we can shift around 64 bit bitfields. This
// allows us to handle anything up to 120 bits, or 128 bits if the beginning is aligned. This
// limitation seems reasonable.

// We treat "signed int" bitfields as being signed and needing sign extensions. Whether "int"
// bitfields are signed or unsigned is actually implementation-defined in the C standard.
bool NeedsSignExtension(const fxl::RefPtr<EvalContext>& context, const Type* type, uint128_t value,
                        uint32_t bit_size) {
  fxl::RefPtr<BaseType> base_type = context->GetConcreteTypeAs<BaseType>(type);
  if (!base_type)
    return false;

  if (!BaseType::IsSigned(base_type->base_type()))
    return false;  // Unsigned type.

  // Needs sign extension when the high bit is set.
  return value & (1ull << (bit_size - 1));
}

}  // namespace

ErrOrValue ResolveBitfieldMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                 const FoundMember& found_member) {
  const DataMember* data_member = found_member.data_member();
  FX_DCHECK(data_member->is_bitfield());

  if (data_member->data_bit_offset()) {
    // All of our compilers currently use bit_offset instead.
    return Err(
        "DW_AT_data_bit_offset is used for this bitfield but is not supported.\n"
        "Please file a bug with information about your compiler and build configuration.");
  }

  // Use the FoundMember's offset (not DataMember's) because FoundMember's takes into account base
  // classes and their offsets.
  // TODO(bug 41503) handle virtual inheritance.
  auto opt_byte_offset = found_member.GetDataMemberOffset();
  if (!opt_byte_offset) {
    return Err(
        "The debugger does not yet support bitfield access on virtually inherited base "
        "classes (bug 41503) or static members.");
  }

  if (data_member->bit_size() + data_member->bit_offset() > sizeof(uint128_t) * 8) {
    // If the total coverage of this bitfield is more than out number size we can't do the
    // operations and need to rewrite this code to manually do shifts on bytes rather than using
    // numeric operations in C.
    return Err("The bitfield spans more than 128 bits which is unsupported.");
  }

  // Destination type. Here we need to save the original (possibly non-concrete type) for assigning
  // to the result type at the bottom.
  const Type* dest_type = data_member->type().Get()->As<Type>();
  if (!dest_type)
    return Err("Bitfield member has no type.");
  fxl::RefPtr<Type> concrete_dest_type = context->GetConcreteType(dest_type);

  uint128_t bits = 0;

  // Copy bytes to out bitfield as long as they're in the structure and will mask the valid ones
  // later. This is because the bit offset can actually be negative to indicate it's starting at a
  // higher bit than byte_size (see below). Copyting everything we have means we don't have to worry
  // about that reading off the end of byte_size() and can just do the masking math.
  //
  // This computation assumes little-endian.
  size_t bytes_to_use = std::min<size_t>(sizeof(bits), base.data().size() - *opt_byte_offset);
  if (!base.data().RangeIsValid(*opt_byte_offset, bytes_to_use))
    return Err::OptimizedOut();
  memcpy(&bits, &base.data().bytes()[*opt_byte_offset], bytes_to_use);

  // Bits count from the high bit within byte_size(). Current compilers seem to always write
  // byte_size == sizeof(declared type) and count the high bit of the result from the high bit of
  // this field (read from memory as little-endian). If bit offsets push the high bit of the
  // result onto a later bit (say it's a 31-bit bitfield and a 32-bit underlying type, starting at
  // a 3 bit offset will make the number cover 5 bytes) the bit offset will actually be negative!
  //
  // So offset 6 in an 8-bit byte_size() selects 0b0000`0010 and we want to shift one bit. This
  // identifies the high bit in the result and we need to shift until the low bit is at the low
  // position.
  uint32_t shift_amount =
      data_member->byte_size() * 8 - data_member->bit_offset() - data_member->bit_size();
  bits >>= shift_amount;

  uint128_t valid_bit_mask = (static_cast<uint128_t>(1) << data_member->bit_size()) - 1;
  bits &= valid_bit_mask;

  if (NeedsSignExtension(context, concrete_dest_type.get(), bits, data_member->bit_size())) {
    // Set non-masked bits to 1.
    bits |= ~valid_bit_mask;
  }

  ExprValueSource source =
      base.source().GetOffsetInto(*opt_byte_offset, data_member->bit_size(), shift_amount);

  // Save the data back to the desired size (assume little-endian so truncation from the right is
  // correct).
  std::vector<uint8_t> new_data(data_member->byte_size());
  memcpy(new_data.data(), &bits, new_data.size());
  return ExprValue(RefPtrTo(dest_type), std::move(new_data), source);
}

void WriteBitfieldToMemory(const fxl::RefPtr<EvalContext>& context, const ExprValueSource& dest,
                           std::vector<uint8_t> data, fit::callback<void(const Err&)> cb) {
  FX_DCHECK(dest.is_bitfield());

  // Expect bitfields to fit in our biggest int.
  if (data.size() > sizeof(uint128_t))
    return cb(Err("Writing bitfields for data > 128-bits is not supported."));

  uint128_t value = 0;
  memcpy(&value, data.data(), data.size());

  // Number of bytes affected by this bitfield.
  size_t byte_size = (dest.bit_size() + dest.bit_shift() + 7) / 8;
  if (!byte_size)
    return cb(Err("Can't write a bitfield with no data."));

  // To only write some bits we need to do a read and a write. Since these are asynchronous there
  // is a possibility of racing with the program, but there will a race if the program is running
  // even if we do the masking in the debug_agent. This implementation is simpler than passing the
  // mask to the agent, so do that.
  context->GetDataProvider()->GetMemoryAsync(
      dest.address(), byte_size,
      [context, dest, value, byte_size, cb = std::move(cb)](
          const Err& err, std::vector<uint8_t> original_data) mutable {
        if (err.has_error())
          return cb(err);
        if (original_data.size() != byte_size)  // Short read means invalid address.
          return cb(Err("Memory at address 0x%" PRIx64 " is invalid.", dest.address()));

        uint128_t original = 0;
        memcpy(&original, original_data.data(), original_data.size());

        uint128_t result = dest.SetBits(original, value);

        // Write out the new data.
        std::vector<uint8_t> new_data(byte_size);
        memcpy(new_data.data(), &result, byte_size);
        context->GetDataProvider()->WriteMemory(dest.address(), std::move(new_data), std::move(cb));
      });
}

}  // namespace zxdb
