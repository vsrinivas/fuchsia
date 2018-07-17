// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <string>
#include <utility>

#include "garnet/public/lib/fxl/macros.h"
#include "llvm/ADT/Optional.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"

namespace llvm {
class DWARFUnit;
class DWARFContext;
class DWARFDataExtractor;
class DWARFDebugInfoEntry;
class DWARFDie;
class DWARFFormValue;
}  // namespace llvm

namespace zxdb {

// Decodes the desired attributes of a given DWARF Debug Info Entry ("DIE").
//
// To use, create once for the unit and register the output variables with the
// Add* functions. Then loop through the relevant entries. In the loop first
// reset() the output variables (so you can tell which were set), then call
// Decode().
class DwarfDieDecoder {
 public:
  // The context and unit must outlive this class.
  DwarfDieDecoder(llvm::DWARFContext* context, llvm::DWARFUnit* unit);
  ~DwarfDieDecoder();

  // Adds a check for the given attribute. If the attribute is encountered,
  // the given boolean will be set to true. You can share a bool pointer
  // between different calls to AddPresenceCheck() to check if any of a set
  // of attributes is available. It does not check the type of validity of
  // the attribute.
  //
  // The output pointer must remain valid until the last call to Decode()
  // has returned.
  void AddPresenceCheck(llvm::dwarf::Attribute attribute, bool* present);

  // These register for a given attribute, and call the similarly-named
  // function in llvm::DWARFFormValue to extract the attribute and place it
  // into the given output variable.
  //
  // The output pointers must remain valid until the last call to Decode()
  // has returned.
  void AddUnsignedConstant(llvm::dwarf::Attribute attribute,
                           llvm::Optional<uint64_t>* output);
  void AddSignedConstant(llvm::dwarf::Attribute attribute,
                         llvm::Optional<int64_t>* output);
  void AddAddress(llvm::dwarf::Attribute attribute,
                  llvm::Optional<uint64_t>* output);
  void AddCString(llvm::dwarf::Attribute attribute,
                  llvm::Optional<const char*>* output);
  void AddLineTableFile(llvm::dwarf::Attribute attribute,
                        llvm::Optional<std::string>* output);

  // For cross-DIE references. These references can be within the current
  // unit (byte offsets, not DIE indices), or from within the object file.
  // To accomodate both, this function will fill in the corresponding output
  // variable according to the storage form of the attribute.
  //
  // See also the DIE wrapper below.
  void AddReference(llvm::dwarf::Attribute attribute,
                    llvm::Optional<uint64_t>* unit_offset,
                    llvm::Optional<uint64_t>* global_offset);

  // Variant ot the above AddReference that automatically converts a reference
  // to an actual DIE. If the attribute doesn't exist or is invalid, this DIE
  // will be !isValid().
  void AddReference(llvm::dwarf::Attribute attribute, llvm::DWARFDie* output);

  // Extract a file name. File names (e.g. for DW_AT_decl_file) are not
  // strings but rather indices into the file name table for the corresponding
  // unit. This accessor resolves the string automatically.
  void AddFile(llvm::dwarf::Attribute attribute,
               llvm::Optional<std::string>* output);

  // Decode one info entry. Returns true if any attributes were decoded. THe
  // outputs for each encountered attribute will be set.
  //
  // A return value of false means either that the entry was a null one (which
  // is used as a placeholder internally), or that it contained none of the
  // attributes that were requested.
  bool Decode(const llvm::DWARFDie& die);
  bool Decode(const llvm::DWARFDebugInfoEntry& die);

 public:
  using Dispatch = std::pair<llvm::dwarf::Attribute,
                             std::function<void(const llvm::DWARFFormValue&)>>;

  llvm::DWARFContext* context_;
  llvm::DWARFUnit* unit_;
  llvm::DWARFDataExtractor extractor_;

  // Normally there will be few attributes and a brute-force search through a
  // contiguous array will be faster than a map lookup.
  std::vector<Dispatch> attrs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DwarfDieDecoder);
};

}  // namespace zxdb
