// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/module_symbol_index.h"

#include <limits>

#include "garnet/bin/zxdb/common/file_util.h"
#include "garnet/bin/zxdb/common/string_util.h"
#include "garnet/bin/zxdb/symbols/dwarf_die_decoder.h"
#include "garnet/bin/zxdb/symbols/dwarf_tag.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index_node.h"
#include "garnet/public/lib/fxl/logging.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace zxdb {

namespace {

// We want to index the things that may need to be referenced globally: global
// variables, file and class static variables, and function implementations.
//
// Indexable functions are the DW_TAG_subprogram entries that have a range of
// code. These implementations won't always have the full type information,
// when the declaration is separate from the implementation, the implementation
// will reference the separate declaration node. The declaration of the
// function will contain the name and have the proper nesting inside classes
// and namespaces, etc. according to the structure of the original code.
//
// Variables work similarly. A global variable will often have a separate
// declaration (in the proper namespaces) and storage (often outside of
// namespaces), but file-level statics with the declaration and storage
// declared all-in-one will have one entry representing everything.
//
// In a compile unit (basically one object file), there will likely be lots of
// declarations from all the headers, and a smaller number of actual function
// definitions and variable storage.
//
// From a high level, we want to search the DIEs for the implementations and
// variable storage which is the stuff that will need to be referenced from
// the global context in the debugger.
//
// Then we follow the link to their definition (if separate from the
// implementation), then walk up the tree to get the full class and namespacing
// information. But walking the tree upwards requires lots of linear searching
// since the tree is stored in a flat array.
//
// To index efficiently, do two passes:
//  1. Walk linearly through all DIEs:
//     1a. Find the ones we're interested in and save the information.
//     1b. For each one, save the index of the parent so we can efficiently
//         walk up the tree in pass 2.
//  2. Resolve the full type information for each function:
//     2a. Find the declaration for each function implementation DIE.
//     2b. Walk that declaration up to get the full context.
//     2c. Index that.
//
// Performance note: Having the unit extract its DIEs via DWARFUnit::dies() and
// DWARFUnit::getNumDIEs() basically iterates through the whole table, which
// we then do again here. We can probably speed things up by eliminating this
// call, calling unit.getUnitDIE(), and manually iterating the children of
// that.

// The SymbolStorage stores the information from the "implementation" of a
// symbol (a function DIE that has code or a variable that has a location),
// representing something we want to index. The entry will always refer to the
// DIE for the implementation, and the offset will refer to the offset of the
// DIE for the definition.
//
// Some functions and variables have separate definitions, and some don't. If
// the definition and implementation is the same, the offset will just point to
// the entry.
struct SymbolStorage {
  SymbolStorage(const llvm::DWARFDebugInfoEntry* e, uint64_t offs,
                ModuleSymbolIndexNode::RefType rt)
      : entry(e), definition_unit_offset(offs), ref_type(rt) {}

  const llvm::DWARFDebugInfoEntry* entry;
  uint64_t definition_unit_offset;
  ModuleSymbolIndexNode::RefType ref_type;
};

// Index used to indicate there is no parent.
constexpr unsigned kNoParent = std::numeric_limits<unsigned>::max();

// Returns true if the given abbreviation defines a PC range.
bool AbbrevHasCode(const llvm::DWARFAbbreviationDeclaration* abbrev) {
  for (const auto spec : abbrev->attributes()) {
    if (spec.Attr == llvm::dwarf::DW_AT_low_pc ||
        spec.Attr == llvm::dwarf::DW_AT_high_pc)
      return true;
  }
  return false;
}

// Returns true if the given abbreviation defines a "location".
bool AbbrevHasLocation(const llvm::DWARFAbbreviationDeclaration* abbrev) {
  for (const auto spec : abbrev->attributes()) {
    if (spec.Attr == llvm::dwarf::DW_AT_location)
      return true;
  }
  return false;
}

size_t RecursiveCountDies(const ModuleSymbolIndexNode& node) {
  size_t result = node.dies().size();
  for (const auto& pair : node.sub())
    result += RecursiveCountDies(pair.second);
  return result;
}

// Step 1 of the algorithm above. Fills the symbol_storage array with the
// information for all function implementations (ones with addresses). Fills
// the parent_indices array with the index of the parent of each DIE in the
// unit (it will be exactly unit->getNumDIEs() long). The root node will have
// kNoParent set.
void ExtractUnitIndexableEntries(llvm::DWARFContext* context,
                                 llvm::DWARFUnit* unit,
                                 std::vector<SymbolStorage>* symbol_storage,
                                 std::vector<unsigned>* parent_indices) {
  DwarfDieDecoder decoder(context, unit);

  // The offset of the declaration. This can be unit-relative or file-absolute.
  // This code doesn't implement the file-absolute variant which it seems our
  // toolchain doesn't generate. To implement I'm thinking everything
  // with an absolute offset will be put into a global list and processed in a
  // third pass once all units are processed. This third pass will be slower
  // since probably we won't do any optimized lookups.
  llvm::Optional<uint64_t> decl_unit_offset;
  llvm::Optional<uint64_t> decl_global_offset;
  decoder.AddReference(llvm::dwarf::DW_AT_specification, &decl_unit_offset,
                       &decl_global_offset);

  llvm::Optional<bool> is_declaration;
  decoder.AddBool(llvm::dwarf::DW_AT_declaration, &is_declaration);

  // Stores the index of the parent DIE for each one we encounter. The root
  // DIE with no parent will be set to kNoParent.
  unsigned die_count = unit->getNumDIEs();
  parent_indices->resize(die_count);

  // Stores the list of parent indices according to the current depth in the
  // tree. At any given point, the parent index of the current node will be
  // tree_stack.back(). inside_function should be set if this node or any
  // parent node is a function.
  struct StackEntry {
    StackEntry(int d, unsigned i, bool f)
        : depth(d), index(i), inside_function(f) {}

    int depth;
    unsigned index;
    bool inside_function;
  };
  std::vector<StackEntry> tree_stack;
  tree_stack.reserve(8);
  tree_stack.push_back(StackEntry(-1, kNoParent, false));

  for (unsigned i = 0; i < die_count; i++) {
    // All optional variables need to be reset so we know which ones are set
    // by the current DIE.
    decl_unit_offset.reset();
    decl_global_offset.reset();

    const llvm::DWARFDebugInfoEntry* die =
        unit->getDIEAtIndex(i).getDebugInfoEntry();
    const llvm::DWARFAbbreviationDeclaration* abbrev =
        die->getAbbreviationDeclarationPtr();
    if (!abbrev)
      continue;

    // See if we should bother decoding. Decode is the slowest part of the
    // indexing so try to avoid it. Here we check the tag and whether the
    // abbreviation entry has the required attributes before doing decode since
    // this will eliminate the majority of DIEs in typical programs.
    //
    // Note: Trying to cache whether the abbreviation declaration is of the
    // right type (there are a limited number of types of these) doesn't help.
    // Checking the abbreviation array is ~6-12 comparisons, which is roughly
    // equivalent to [unordered_]map lookup.
    bool should_index = false;
    ModuleSymbolIndexNode::RefType ref_type;
    DwarfTag tag = static_cast<DwarfTag>(abbrev->getTag());
    if (tag == DwarfTag::kSubprogram && AbbrevHasCode(abbrev)) {
      // Found a function implementation.
      ref_type = ModuleSymbolIndexNode::RefType::kFunction;
      should_index = true;
    } else if (tag == DwarfTag::kNamespace) {
      ref_type = ModuleSymbolIndexNode::RefType::kNamespace;
      should_index = true;
    } else if (DwarfTagIsType(tag)) {
      // Found a type definition or declaration (these two will be
      // disambiguated once the DIE is decoded below).
      ref_type = ModuleSymbolIndexNode::RefType::kType;
      should_index = true;
    } else if (!tree_stack.back().inside_function &&
               tag == DwarfTag::kVariable &&
               AbbrevHasLocation(abbrev)) {
      // Found variable storage outside of a function (variables inside
      // functions are local so don't get added to the global index).
      ref_type = ModuleSymbolIndexNode::RefType::kVariable;
      should_index = true;
    }

    // Add this node to the index.
    if (should_index) {
      decoder.Decode(*die);

      // Apply the declaration flag for types now that we've decoded.
      if (ref_type == ModuleSymbolIndexNode::RefType::kType &&
          is_declaration && *is_declaration)
        ref_type = ModuleSymbolIndexNode::RefType::kTypeDecl;

      if (decl_unit_offset) {
        // Save the declaration for indexing.
        symbol_storage->emplace_back(die,
                                     unit->getOffset() + *decl_unit_offset,
                                     ref_type);
      } else if (decl_global_offset) {
        FXL_NOTREACHED() << "Implement DW_FORM_ref_addr for references.";
      } else {
        // This symbol has no separate definition so use it as its own
        // declaration (the name and such will be on itself).
        symbol_storage->emplace_back(die, die->getOffset(), ref_type);
      }
    }

    // Fix up the parent tracking stack.
    StackEntry& tree_stack_back = tree_stack.back();
    int current_depth = static_cast<int>(die->getDepth());
    if (current_depth == tree_stack_back.depth) {
      // Common case: depth not changing. Just update the topmost item in the
      // stack to point to the current node.
      tree_stack_back.index = i;
    } else {
      // Tree changed. First check for moving up in the tree and pop the stack
      // until we're at the parent of the current level (for going deeper in
      // the tree this will do nothing), then add the current level.
      while (tree_stack.back().depth >= current_depth)
        tree_stack.pop_back();

      tree_stack.push_back(StackEntry(
          current_depth, i,
          ref_type == ModuleSymbolIndexNode::RefType::kFunction ||
              tree_stack.back().inside_function));
    }

    // Save parent info. The parent of this node is the one right before the
    // current one (the last one in the stack).
    (*parent_indices)[i] = (tree_stack.end() - 2)->index;
  }
}

// The per-function part of step 2 of the algorithm described above. This
// finds the definition of the function in the unit's DIEs. It's given a
// map of DIE indices to their parent indices generated for the unit by
// ExtractUnitIndexableEntries for quickly finding parents.
class SymbolStorageIndexer {
 public:
  SymbolStorageIndexer(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                       const std::vector<unsigned>& parent_indices,
                       ModuleSymbolIndexNode* root)
      : unit_(unit),
        parent_indices_(parent_indices),
        root_(root),
        decoder_(context, unit) {
    decoder_.AddCString(llvm::dwarf::DW_AT_name, &name_);
  }

  void AddDIE(const SymbolStorage& impl) {
    // Components of the name in reverse order (so "foo::Bar::Fn") would be {
    // "Fn", "Bar", "foo"}
    std::vector<std::string> components;


    // Find the declaration DIE function. Perf note: getDIEForOffset() is a
    // binary search.
    llvm::DWARFDie die = unit_->getDIEForOffset(impl.definition_unit_offset);
    if (!die.isValid())
      return;  // Invalid
    if (!FillName(die))
      return;
    components.emplace_back(*name_);

    unsigned index = unit_->getDIEIndex(die);
    while (true) {
      // Move up one level in the hierarchy.
      FXL_DCHECK(index <= parent_indices_.size());
      index = parent_indices_[index];
      if (index == kNoParent) {
        // Reached the root. In practice this shouldn't happen since following
        // the parent chain from a function should always lead to the compile
        // unit (handled below).
        break;
      }

      die = unit_->getDIEAtIndex(index);
      if (!die.isValid())
        return;  // Something is corrupted.

      if (die.getTag() == llvm::dwarf::DW_TAG_compile_unit)
        break;  // Reached the root.

      // Validate the type of this entry. We don't want to index things
      // like functions inside classes locally defined in functions since
      // there's no good way to refer to these by global name.
      if (die.getTag() != llvm::dwarf::DW_TAG_namespace &&
          die.getTag() != llvm::dwarf::DW_TAG_class_type &&
          die.getTag() != llvm::dwarf::DW_TAG_structure_type)
        return;

      if (!FillName(die))
        return;  // Likely corrupt, these nodes should have names.
      components.emplace_back(*name_);
    }

    // Add the symbol to the index.
    ModuleSymbolIndexNode* cur = root_;
    for (int i = static_cast<int>(components.size()) - 1; i >= 0; i--)
      cur = cur->AddChild(std::move(components[i]));
    cur->AddDie(ModuleSymbolIndexNode::DieRef(
        impl.ref_type, impl.entry->getOffset()));
  }

 private:
  // Fills the name_ member for the given DIE. Returns true if the DIE was
  // decoded properly and name_ was properly filled in.
  bool FillName(const llvm::DWARFDie& die) {
    name_.reset();
    if (!decoder_.Decode(*die.getDebugInfoEntry()) || !name_)
      return false;  // Node with no name, skip this function.
    return true;
  }

  llvm::DWARFUnit* unit_;
  const std::vector<unsigned>& parent_indices_;
  ModuleSymbolIndexNode* root_;

  DwarfDieDecoder decoder_;
  llvm::Optional<const char*> name_;  // Decoder writes into this.
};

}  // namespace

ModuleSymbolIndex::ModuleSymbolIndex() = default;
ModuleSymbolIndex::~ModuleSymbolIndex() = default;

void ModuleSymbolIndex::CreateIndex(llvm::object::ObjectFile* object_file) {
  std::unique_ptr<llvm::DWARFContext> context = llvm::DWARFContext::create(
      *object_file, nullptr, llvm::DWARFContext::defaultErrorHandler);

  llvm::DWARFUnitVector compile_units;
  context->getDWARFObj().forEachInfoSections(
      [&](const llvm::DWARFSection& s) {
        compile_units.addUnitsForSection(*context, s, llvm::DW_SECT_INFO);
      });

  for (unsigned i = 0; i < compile_units.size(); i++) {
    IndexCompileUnit(context.get(), compile_units[i].get(), i);

    // Free all compilation units as we process them. They will hold all of
    // the parsed DIE data that we don't need any more which can be multiple
    // GB's for large programs.
    compile_units[i].reset();
  }

  IndexFileNames();
}

size_t ModuleSymbolIndex::CountSymbolsIndexed() const {
  return RecursiveCountDies(root_);
}

const std::vector<ModuleSymbolIndexNode::DieRef>& ModuleSymbolIndex::FindExact(
    const std::string& input) const {
  // Split the input on "::" which we'll traverse the tree with.
  //
  // TODO(brettw) this doesn't handle a lot of things like templates. By
  // blindly splitting on "::" we'll never find functions like
  // "std::vector<Foo::Bar>::insert".
  std::string separator("::");

  const ModuleSymbolIndexNode* cur = &root_;

  size_t input_index = 0;
  while (input_index < input.size()) {
    size_t next = input.find(separator, input_index);

    std::string cur_name;
    if (next == std::string::npos) {
      cur_name = input.substr(input_index);
      input_index = input.size();
    } else {
      cur_name = input.substr(input_index, next - input_index);
      input_index = next + separator.size();  // Skip over "::".
    }

    auto found = cur->sub().find(cur_name);
    if (found == cur->sub().end()) {
      static std::vector<ModuleSymbolIndexNode::DieRef> empty_vector;
      return empty_vector;
    }

    cur = &found->second;
  }

  return cur->dies();
}

std::vector<std::string> ModuleSymbolIndex::FindFileMatches(
    const std::string& name) const {
  std::string_view name_last_comp = ExtractLastFileComponent(name);

  std::vector<std::string> result;

  // Search all files whose last component matches (the input may contain more
  // than one component).
  FileNameIndex::const_iterator iter =
      file_name_index_.lower_bound(name_last_comp);
  while (iter != file_name_index_.end() && iter->first == name_last_comp) {
    const auto& pair = *iter->second;
    if (StringEndsWith(pair.first, name) &&
        (pair.first.size() == name.size() ||
         pair.first[pair.first.size() - name.size() - 1] == '/')) {
      result.push_back(pair.first);
    }
    ++iter;
  }

  return result;
}

const std::vector<unsigned>* ModuleSymbolIndex::FindFileUnitIndices(
    const std::string& name) const {
  auto found = files_.find(name);
  if (found == files_.end())
    return nullptr;
  return &found->second;
}

void ModuleSymbolIndex::DumpFileIndex(std::ostream& out) const {
  for (const auto& [filename, file_index_entry]: file_name_index_) {
    const auto& [filepath, compilation_units] = *file_index_entry;
    out << filename << " -> " << filepath << " -> "
        << compilation_units.size() << " units\n";
  }
}

void ModuleSymbolIndex::IndexCompileUnit(llvm::DWARFContext* context,
                                         llvm::DWARFUnit* unit,
                                         unsigned unit_index) {
  // Find the things to index.
  std::vector<SymbolStorage> symbol_storage;
  symbol_storage.reserve(256);
  std::vector<unsigned> parent_indices;
  ExtractUnitIndexableEntries(context, unit, &symbol_storage, &parent_indices);

  // Index each one.
  SymbolStorageIndexer indexer(context, unit, parent_indices, &root_);
  for (const SymbolStorage& impl : symbol_storage)
    indexer.AddDIE(impl);

  IndexCompileUnitSourceFiles(context, unit, unit_index);
}

void ModuleSymbolIndex::IndexCompileUnitSourceFiles(llvm::DWARFContext* context,
                                                    llvm::DWARFUnit* unit,
                                                    unsigned unit_index) {
  const llvm::DWARFDebugLine::LineTable* line_table =
      context->getLineTableForUnit(unit);
  if (!line_table) {
    return;  // No line table for this unit.
  }

  const char* compilation_dir = unit->getCompilationDir();

  // This table is the size of the file name table. Entries are set to 1 when
  // we've added them to the index already.
  std::vector<int> added_file;
  added_file.resize(line_table->Prologue.FileNames.size(), 0);

  // We don't want to just add all the files from the line table to the index.
  // The line table will contain entries for every file referenced by the
  // compilation unit, which includes declarations. We want only files that
  // contribute code, which in practice is a tiny fraction of the total.
  //
  // To get this, iterate through the unit's row table and collect all
  // referenced file names.
  std::string file_name;
  for (size_t i = 0; i < line_table->Rows.size(); i++) {
    auto file_id = line_table->Rows[i].File;  // 1-based!
    if (file_id < 1 || file_id > added_file.size())
      continue;
    auto file_index = file_id - 1;

    if (!added_file[file_index]) {
      added_file[file_index] = 1;
      if (line_table->getFileNameByIndex(
              file_id, compilation_dir,
              llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
              file_name)) {
        // The files here can contain relative components like
        // "/foo/bar/../baz". This is OK because we want it to match other
        // places in the symbol code that do a similar computation to get a
        // file name.
        files_[file_name].push_back(unit_index);
      }
    }
  }
}

void ModuleSymbolIndex::IndexFileNames() {
  for (FileIndex::const_iterator iter = files_.begin(); iter != files_.end();
       ++iter) {
    std::string_view name = ExtractLastFileComponent(iter->first);
    file_name_index_.insert(std::make_pair(name, iter));
  }
}

}  // namespace zxdb
