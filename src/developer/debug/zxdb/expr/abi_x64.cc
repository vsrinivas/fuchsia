// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/abi_x64.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

namespace zxdb {

namespace {

using debug::RegisterID;

// Notes on returning collections by value
// ---------------------------------------
//
// The x64 ABI Fuchsia uses:
// https://software.intel.com/content/www/us/en/develop/articles/linux-abi.html
//
// The ABI rules for passing and returning collections by value are quite complicated (the LLVM code
// is about 1000 lines). This code doesn't attempt to implement the whole thing, but tries to get
// the common cases and give up for anything complex. Fortunately, this will support the vast
// majority of practical uses.
//
// Fortunately, many of the rules for passing collections don't apply because we know already that
// the collection is passed by value (DWARF tells us). This means that we don't have to worry about
// unaligned values and C++ rules about non-trivial copy constructors. Additionally, we don't
// support x87 floating-point, the C "complex" type, and vectors. This leaves only the POINTER,
// INTEGER, and SSE classes.
//
// POINTER and INTEGER classes are returned in the general-purpose registers, so the only thing we
// have to worry about is finding which things are SSE class. All pass-by-value collections are less
// than 16 bytes except those consisting of SSE vectors which we don't support, so we can also
// assume <= 16 bytes.
//
// The ABI wants to return things in 8-byte chunks. If a chunk is all floating-point, it's returned
// in xmm0, xmm1. If it's integer, pointer, or a combination (possibly including floating-point),
// it's returned in rax, rdx.
//
// Some examples for structure returning:
//
//  - {double, int64} -> {xmm0 (8 bytes), rax}
//  - {float, int64} -> {xmm0 (4 bytes), rax}
//  - {float, float} -> {xmm0[0] (low 4 bytes), xmm0[1] (next higher 4 bytes)}
//  - {float, float, int64} -> {xmm0[0], xmm0[1], rax}
//  - {float, char} -> {rax (low 4 bytes), rax (5th byte)}                           (!)
//  - {float, char, float} -> {rax (low 4 bytes), rax (5th byte), xmm0[0] (4 bytes)} (!)
//
// For the examples marked with (!) you can see floating-point values getting passed in integer
// registers. This is because the first two values fit into one eightbyte. When comparing the first
// two values according to the ABI parameter classification rule "If one of the classes is INTEGER,
// the result is INTEGER.", the float/char combination is assigned class INTEGER and therefore rax.
//
// For the {float, char, float} case, the second float falls into a different eightbyte, and
// according to the API parameter classification rule "If the size of the aggregate exceeds a single
// eightbyte, each is classified separately." Therefore, the 2nd float and the earlier parameters
// are never compared and keep their separate classes.

// Register classes from the ABI. We do not support SSEUP, X87, X87UP, COMPLEX_X87. The MEMORY
// class isn't handled here because that means it's not passed in registers and we already know
// the answer from the calling convention in DWARF.
enum class RegisterClass {
  kEmpty,    // Not allocated (NO_CLASS).
  kGeneral,  // Used for both the POINTER and INTEGER types from the ABI.
  kSse       // The SSE type (this counts only the low 4 or 8 bytes, not a vector).
};

struct DataMemberInfo {
  // Offset from the beginning of the collection of this member. There can be multiple members
  // at the same offset in the case of unions.
  uint32_t byte_offset;

  // This type will be concrete.
  fxl::RefPtr<Type> type;
};

// Makes a list of all data members and their locations inside of a collection for the purposes of
// allocating registers for returning it by value.
//
// Returns true on success or false if there are some member types that we don't support for
// computing by-value returns.
bool GetDataMembersForByValueReturning(const fxl::RefPtr<EvalContext>& context,
                                       const Collection* collection,
                                       std::vector<DataMemberInfo>& result) {
  VisitResult visit_result = VisitDataMembers(
      collection,
      [context, &result](bool is_leaf, uint32_t net_byte_offset,
                         const DataMember* member) -> VisitResult {
        if (!is_leaf)
          return VisitResult::kContinue;  // Intermediate collections, we'll catch members later.
        if (member->is_external())
          return VisitResult::kContinue;  // Static member, doesn't count toward returning.

        // Don't support bitfields.
        if (member->is_bitfield())
          return VisitResult::kAbort;

        // Decode the member type.
        auto type = context->GetConcreteType(member->type());
        if (!type)
          return VisitResult::kAbort;

        // Save the mapping.
        result.push_back({net_byte_offset, std::move(type)});
        return VisitResult::kContinue;
      });
  return visit_result != VisitResult::kAbort;
}

// Performs the merge step from the ABI document to get the allocations for a structure. Returns
// true on success, false means there was an unsupported feature or error.
//
// As per the above algorithm, we should have at most 2 "eightbyte" values. Check each one to
// see if there is anything of type "SSE" (floating point values). Everything else we can assume
// is either a pointer or integer type that goes into a general-purpose register.
bool MergeDataMembersForByValueReturning(const std::vector<DataMemberInfo>& members,
                                         std::vector<RegisterClass>& classes) {
  // The current algorithm assumes a maximum of two "eightbytes".
  classes.resize(2);
  classes[0] = RegisterClass::kEmpty;
  classes[1] = RegisterClass::kEmpty;

  // The ABI algorithm requires everything be aligned to be passed by value (which we know it is).
  // This means that as long as all values are < 8 bytes and not bitfields, nothing will cross this
  // boundary.
  for (const auto& member : members) {
    // Figure out which eightbyte value this member belongs in.
    size_t class_index;
    if (member.byte_offset < 8) {
      class_index = 0;
    } else if (member.byte_offset < 16) {
      class_index = 1;
    } else {
      return false;  // Value beyond type size.
    }

    // Simplifying assumption: don't support members greater than 8 bytes. This eliminates "long
    // double" which is "x87" class, uint128, SSE vectors, and arrays.
    if (member.type->byte_size() > 8)
      return false;

    if (const BaseType* base_type = member.type->As<BaseType>()) {
      if (base_type->base_type() == BaseType::kBaseTypeFloat) {
        // Class SSE, but don't overwrite a "general" class (this can happen if there's a uint32
        // followed by a 32-bit float). The integer takes precedence.
        if (classes[class_index] != RegisterClass::kGeneral)
          classes[class_index] = RegisterClass::kSse;
      } else {
        // All other base types are "general" (pointers or integers). This also overwrites SSE if
        // there is a 32-bit float followed by a int32.
        classes[class_index] = RegisterClass::kGeneral;
      }
    } else if (DwarfTagIsPointerOrReference(member.type->tag())) {
      // Pointers or reference types. "General" takes precedence over SSE so overwrite.
      classes[class_index] = RegisterClass::kGeneral;
    } else {
      // Any other member types we don't support, give up.
      return false;
    }
  }

  return true;
}

// Given a sequence of register classes, allocates it to the registers that would be used. This
// step can not fail.
Abi::CollectionByValueReturn AllocateRegistersForByValueReturning(
    uint32_t dest_byte_size, const std::vector<RegisterClass>& classes) {
  // Currently expect exactly 2 entries in the list generated by Merge above.
  constexpr size_t kMaxRegs = 2;
  FX_DCHECK(classes.size() <= kMaxRegs);

  // These are the registers to use for each class.
  static const debug::RegisterID kGeneralRegs[kMaxRegs] = {debug::RegisterID::kX64_rax,
                                                           debug::RegisterID::kX64_rdx};
  static const debug::RegisterID kSseRegs[kMaxRegs] = {debug::RegisterID::kX64_xmm0,
                                                       debug::RegisterID::kX64_xmm1};

  // The next register of each category to use.
  int next_general = 0;
  int next_sse = 0;

  // The number of bytes in the collection left to allocate to registers.
  uint32_t remaining_bytes = dest_byte_size;

  Abi::CollectionByValueReturn result;
  for (size_t i = 0; i < classes.size(); i++) {
    switch (classes[i]) {
      case RegisterClass::kGeneral:
        result.regs.push_back(
            {.reg = kGeneralRegs[next_general], .bytes = std::min(remaining_bytes, 8u)});
        remaining_bytes -= 8u;
        next_general++;
        break;
      case RegisterClass::kSse:
        result.regs.push_back({.reg = kSseRegs[next_sse], .bytes = std::min(remaining_bytes, 8u)});
        remaining_bytes -= 8u;
        next_sse++;
        break;
      case RegisterClass::kEmpty:
        break;
    }
  }

  return result;
}

}  // namespace

bool AbiX64::IsRegisterCalleeSaved(debug::RegisterID reg) const {
  switch (reg) {
    case debug::RegisterID::kX64_rbx:
    case debug::RegisterID::kX64_bh:  // All variants of "rbx".
    case debug::RegisterID::kX64_bl:
    case debug::RegisterID::kX64_bx:
    case debug::RegisterID::kX64_ebx:
    case debug::RegisterID::kX64_rsp:
    case debug::RegisterID::kX64_rbp:
    case debug::RegisterID::kX64_r12:
    case debug::RegisterID::kX64_r13:
    case debug::RegisterID::kX64_r14:
    case debug::RegisterID::kX64_r15:
    case debug::RegisterID::kX64_rip:
      return true;
    default:
      return false;
  }
}

std::optional<debug::RegisterID> AbiX64::GetReturnRegisterForBaseType(const BaseType* base_type) {
  switch (base_type->base_type()) {
    case BaseType::kBaseTypeFloat:
      if (base_type->byte_size() <= 8)
        return RegisterID::kX64_xmm0;

      // Don't support larger floating-point numbers.
      return std::nullopt;

    case BaseType::kBaseTypeBoolean:
    case BaseType::kBaseTypeSigned:
    case BaseType::kBaseTypeSignedChar:
    case BaseType::kBaseTypeUnsigned:
    case BaseType::kBaseTypeUnsignedChar:
    case BaseType::kBaseTypeUTF:
      if (base_type->byte_size() <= 8)
        return GetReturnRegisterForMachineInt();

      // Larger numbers are spread across multiple registers which we don't support yet.
      return std::nullopt;

    case BaseType::kBaseTypeNone:
    case BaseType::kBaseTypeAddress:  // Not used in C.
    default:
      return std::nullopt;
  }
}

std::optional<Abi::CollectionReturn> AbiX64::GetCollectionReturnByRefLocation(
    const Collection* collection) {
  FX_DCHECK(collection->calling_convention() == Collection::kPassByReference);

  // Pass-by-reference collections are placed into a location indicated by the caller and that
  // location is echoed back upon return in the rax register.
  return CollectionReturn{.addr_return_reg = RegisterID::kX64_rax};
}

std::optional<Abi::CollectionByValueReturn> AbiX64::GetCollectionReturnByValueLocation(
    const fxl::RefPtr<EvalContext>& eval_context, const Collection* collection) {
  if (collection->byte_size() == 0 || collection->byte_size() > 16)
    return std::nullopt;  // Too big.

  // Get all the data members.
  std::vector<DataMemberInfo> members;
  if (!GetDataMembersForByValueReturning(eval_context, collection, members))
    return std::nullopt;

  // Merge into classes representing each eightbyte section.
  std::vector<RegisterClass> classes;
  if (!MergeDataMembersForByValueReturning(members, classes))
    return std::nullopt;

  return AllocateRegistersForByValueReturning(collection->byte_size(), classes);
}

}  // namespace zxdb
