// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/common/adapters.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/dwarf_die_decoder.h"
#include "src/developer/debug/zxdb/symbols/dwarf_die_scanner.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

namespace zxdb {

namespace {

// Don't index more than this number of levels to prevent infinite recursion.
constexpr size_t kMaxParentPath = 16;

// Stores a name with a SymbolRef for later indexing.
class NamedSymbolRef : public IndexNode::SymbolRef {
 public:
  NamedSymbolRef() = default;

  // Creates a SymbolRef we should index. The pointed-to string must outlive this class.
  NamedSymbolRef(SymbolRef::Kind kind, uint64_t offset, IndexNode::Kind k, const char* name,
                 uint64_t decl_offset, bool has_abstract_origin)
      : SymbolRef(kind, offset),
        kind_(k),
        name_(name),
        decl_offset_(decl_offset),
        has_abstract_origin_(has_abstract_origin) {}

  bool should_index() const { return kind_ != IndexNode::Kind::kNone; }

  IndexNode::Kind kind() const { return kind_; }

  // The name associated with the DIE. Could be null.
  //
  // It's also possible for this to be valid for an otherwise !should_index() SymbolRef. In the case
  // of a function with a specification, the implementation will have should_index set, but we'll
  // traverse the specification to fill in the name. This will generate a valid but not indexable
  // item for the specification.
  const char* name() const { return name_; }
  void set_name(const char* n) { name_ = n; }

  // If this DIE has a declaration associated with it (a DW_AT_declaration tag), this indicates the
  // absolute offset of the declaration DIE. Will be 0 if none. It may or may not be inside the
  // current unit (it normally will be though).
  uint64_t decl_offset() const { return decl_offset_; }

  // The indexing layer uses this to cache the node found for a given thing. This allows us to
  // bypass lookup for the common case of things that are all in the same scope.
  IndexNode* index_node() const { return index_node_; }
  void set_index_node(IndexNode* n) { index_node_ = n; }

  // Sometimes we need to know whether an abstract origin is present for parent computations.
  //
  // When walking the dependency path, the abstract origin (if any) encodes the lexical scope.
  // As an example, DW_TAG_inlined_subroutine DIEs are inside of the function they're inlined into
  // (the calling function will be die.getParent()). These will then have an abstract origin of a
  // DIE outside of the function containing the common info for all inlined instances.
  //
  // When there's no separate declaration, this abstract origin will be the scope that the function
  // was declared in where we index from. For:
  //
  //   namespace ns {
  //     inline void InlinedFunction() { ... }
  //   }
  //   void CallingFunction() {
  //     ns::InlinedFunction();
  //   }
  //
  // The DWARF would look like:
  //
  //   1: DW_TAG_namespace (name = "ns")
  //      2: DW_TAG_subprogram (name = "InlinedFunction")
  //   2: DW_TAG_subprogram (name = "CallingFunction")
  //      3: DW_TAG_inlined_subroutine (abstract_origin = 2)
  //
  // When there is a declaration, that declaration will encode the scope:
  //
  //   1: DW_TAG_namespace (name = "ns")
  //      2: DW_TAG_subprogram (name = "InlinedFunction", is_declaration = true)
  //   3: DW_TAG_subprogram (declaration = 2)
  //   2: DW_TAG_subprogram (name = "CallingFunction")
  //      3: DW_TAG_inlined_subroutine (abstract_origin = 3)
  bool has_abstract_origin() const { return has_abstract_origin_; }
  void set_has_abstract_origin(bool b) { has_abstract_origin_ = b; }

 private:
  IndexNode::Kind kind_ = IndexNode::Kind::kNone;
  const char* name_ = nullptr;
  uint64_t decl_offset_ = 0;
  IndexNode* index_node_ = nullptr;
  bool has_abstract_origin_ = false;
};

// Returns true if the given abbreviation defines a PC range.
bool AbbrevHasCode(const llvm::DWARFAbbreviationDeclaration* abbrev) {
  for (const auto spec : abbrev->attributes()) {
    if (spec.Attr == llvm::dwarf::DW_AT_low_pc || spec.Attr == llvm::dwarf::DW_AT_high_pc ||
        spec.Attr == llvm::dwarf::DW_AT_ranges)
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

size_t RecursiveCountDies(const IndexNode& node) {
  size_t result = node.dies().size();

  for (int i = 0; i < static_cast<int>(IndexNode::Kind::kEndPhysical); i++) {
    for (const auto& pair : node.MapForKind(static_cast<IndexNode::Kind>(i)))
      result += RecursiveCountDies(pair.second);
  }
  return result;
}

// This helper class is used to index the symbols of one unit. It keeps some state to avoid
// reallocating for each call.
//
// Indexing is two passes. In the first pass we scan the DIEs in the unit. We identify which ones
// will need indexing and save information on the nesting. The parent chain information is important
// because we need to go from a DIE to its parent chain.
//
// In the second pass we actually index the items identified, using the saved parent and name
// information from the scan pass.
//
// In the second pass we can encounter some DIEs in the hierarchy chain that were not decoded in
// the first pass. An example is when going to a function declaration. We only identify the
// implementations in the first pass, but need to take the name from the declaration. These missing
// ones are filled in by FillDieInfo().
class UnitIndexer {
 public:
  // All passed-in objects must outlive this class.
  explicit UnitIndexer(llvm::DWARFContext* context, llvm::DWARFUnit* unit)
      : context_(context), unit_(unit), scanner_(unit), name_decoder_(context) {
    // The indexable array is 1:1 with the scanner entries.
    indexable_.resize(scanner_.die_count());

    path_.reserve(8);  // Don't want to reallocate.

    // Set up the name decoder to extract to this local.
    name_decoder_.AddCString(llvm::dwarf::DW_AT_name, &name_decoder_name_);
  }

  // Forces indexing to go through the slow path (AddStandaloneEntryToIndex() instead of
  // AddEntryToIndex()) which can handle cross-unit references. This allows us to test the slow path
  // on the same data as the fast path and make sure they match.
  void set_force_slow_path(bool force) { force_slow_path_ = force; }

  // To use, first call Scan() to populate the indexable_ array, then call Index() to add the
  // items to the given index node root. The Scan pass will additionally add any entrypoint
  // functions it finds to the main_functions vector.
  void Scan(std::vector<IndexNode::SymbolRef>* main_functions);
  void Index(IndexNode* root);

 private:
  // Returns kNone for non-indexable items.
  //
  // The kVar case is also returned for collection members. These need to be treated as variables
  // when they have const data, but not otherwise, and this function does not decode the attributes.
  IndexNode::Kind GetKindForDie(const llvm::DWARFDebugInfoEntry* die) const;

  // Computes in the name and type for a DIE entry that wasn't filled in in the first pass (see
  // class-level comment). Returns empty string if there is no name (this is important for the
  // caller, see that code for more).
  //
  // This requires that the DIE be in the current unit_ (the decoder references the unit).
  const char* GetDieName(uint32_t index);

  // Adds the entry at the given index. If the entry is not in the current compilation unit, this
  // will fall back to the slow path: AddStandaloneEntryToIndex().
  void AddEntryToIndex(uint32_t index_me, IndexNode* root);

  // Slow path for adding an entry.
  //
  // This takes the index of a SymbolRef we want to index and adds it to the index without using
  // anything that references the compilation unit, notably the scanner_ which computes parent
  // information.
  //
  // This is used for the uncommon case of cross-unit references, where the declaration might be
  // in a different unit from the implementation. This means that the scanner's parent tree
  // doesn't cover the object we want. This walks the tree using DWARFDie.getParent() which is
  // conceptually simpler but requires a binary search at each step.
  void AddStandaloneEntryToIndex(uint32_t index_me, IndexNode* root);

  // Given the index of a NamedSymbolRef known to have an abstract origin, fills in the index of the
  // abstract origin to the given output variable if it exists in the same unit and returns true.
  //
  // If it doesn't exist or is in a different unit, returns false. Being in the same unit is
  // required to stay in the index fast path.
  bool GetAbstractOriginIndex(uint32_t source, uint32_t* abstract_origin_index) const;

  llvm::DWARFContext* context_;
  llvm::DWARFUnit* unit_;

  bool force_slow_path_ = false;  // See setter above.

  DwarfDieScanner scanner_;
  std::vector<NamedSymbolRef> indexable_;

  // Variable used for collecting the path of parents in AddDIE. This would make more sense as a
  // local variable but having it here prevents reallocating each time.
  std::vector<NamedSymbolRef*> path_;

  // Used to decode names for DIEs in the second pass when we find one we need that wasn't extracted
  // in the first.
  DwarfDieDecoder name_decoder_;
  llvm::Optional<const char*> name_decoder_name_;
};

// The symbol storage will be filled with the indexable entries.
void UnitIndexer::Scan(std::vector<IndexNode::SymbolRef>* main_functions) {
  DwarfDieDecoder decoder(context_);

  // The offset of the declaration. This can be unit-relative or .debug_info-relative (global).
  llvm::DWARFDie decl_die;
  decoder.AddReference(llvm::dwarf::DW_AT_specification, &decl_die);

  llvm::Optional<bool> is_declaration;
  decoder.AddBool(llvm::dwarf::DW_AT_declaration, &is_declaration);

  bool has_const_value = false;
  decoder.AddPresenceCheck(llvm::dwarf::DW_AT_const_value, &has_const_value);

  llvm::Optional<bool> is_main_subprogram;
  decoder.AddBool(llvm::dwarf::DW_AT_main_subprogram, &is_main_subprogram);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  bool has_abstract_origin = false;
  decoder.AddPresenceCheck(llvm::dwarf::DW_AT_abstract_origin, &has_abstract_origin);

  // IF YOU ADD MORE ATTRIBUTES HERE don't forget to reset() them before Decode().

  for (; !scanner_.done(); scanner_.Advance()) {
    const llvm::DWARFDebugInfoEntry* die = scanner_.Prepare();

    // Check whether we should consider this before decoding since decoding is slow.
    IndexNode::Kind kind = GetKindForDie(die);
    if (kind == IndexNode::Kind::kNone)
      continue;

    // This DIE is of the type we want to index so decode. Must reset all output vars first.
    is_declaration.reset();
    has_const_value = false;
    decl_die = llvm::DWARFDie();
    is_main_subprogram.reset();
    name.reset();
    has_abstract_origin = false;
    if (!decoder.Decode(llvm::DWARFDie(unit_, die)))
      continue;

    // Compute the offset of a separate declaration if this DIE has one.
    uint64_t decl_offset = 0;
    if (decl_die)
      decl_offset = decl_die.getOffset();

    if (kind == IndexNode::Kind::kVar && die->getTag() == llvm::dwarf::DW_TAG_member &&
        !has_const_value) {
      // Don't need to index structure members that don't have const values. This needs to be
      // disambiguated because GetKindForDie doesn't have access to the attributes and we don't
      // want to decode twice.
      //
      // In C++ everything with a const_value will generally also be external (i.e. "static") which
      // are things we want to index. Theoretically the compiler could generated a const_value
      // member if it notices the member is never modified and optimize it. In that case, the user
      // would never expect to reference it outside of a known Collection object and it doesn't need
      // to be in the index. But that requires some extra work checking for the external flag in
      // this time-critical indexing step, and the worse thing is that "print MyClass::kMyConstant"
      // evaluates to a correct value where it might not be allowed in the actual language.
      //
      // As a result, we don't also check DW_AT_external.
      continue;
    }

    FX_DCHECK(scanner_.die_index() < indexable_.size());
    auto ref_kind = is_declaration && *is_declaration ? IndexNode::SymbolRef::kDwarfDeclaration
                                                      : IndexNode::SymbolRef::kDwarf;
    indexable_[scanner_.die_index()] = NamedSymbolRef(
        ref_kind, die->getOffset(), kind, name ? *name : nullptr, decl_offset, has_abstract_origin);

    // Check for "main" function annotation.
    if (kind == IndexNode::Kind::kFunction && is_main_subprogram && *is_main_subprogram)
      main_functions->emplace_back(IndexNode::SymbolRef::kDwarf, die->getOffset());
  }
}

void UnitIndexer::Index(IndexNode* root) {
  if (force_slow_path_) {
    // Index everything with the slow path for testing purposes.
    for (uint32_t i = 0; i < indexable_.size(); i++) {
      if (indexable_[i].should_index())
        AddStandaloneEntryToIndex(i, root);
    }
  } else {
    // Normal fast-path. This is about 6x faster than the slow path for large programs.
    for (uint32_t i = 0; i < indexable_.size(); i++) {
      if (indexable_[i].should_index())
        AddEntryToIndex(i, root);
    }
  }
}

IndexNode::Kind UnitIndexer::GetKindForDie(const llvm::DWARFDebugInfoEntry* die) const {
  const llvm::DWARFAbbreviationDeclaration* abbrev = die->getAbbreviationDeclarationPtr();
  if (!abbrev)
    return IndexNode::Kind::kNone;  // Corrupt.

  switch (static_cast<DwarfTag>(abbrev->getTag())) {
    case DwarfTag::kSubprogram:
    case DwarfTag::kInlinedSubroutine:
      if (AbbrevHasCode(abbrev))
        return IndexNode::Kind::kFunction;
      return IndexNode::Kind::kNone;  // Skip functions with no code.

    case DwarfTag::kNamespace:
      return IndexNode::Kind::kNamespace;

    case DwarfTag::kBaseType:
    case DwarfTag::kClassType:
    case DwarfTag::kEnumerationType:
    case DwarfTag::kPtrToMemberType:
    case DwarfTag::kStringType:
    case DwarfTag::kStructureType:
    case DwarfTag::kSubroutineType:
    case DwarfTag::kTypedef:
    case DwarfTag::kUnionType:
      return IndexNode::Kind::kType;

    case DwarfTag::kVariable:
      if (!scanner_.is_inside_function() && AbbrevHasLocation(abbrev)) {
        // Found variable storage outside of a function (variables inside functions are local so
        // don't get added to the global index).
        // TODO(bug 36671): index function-static variables.
        return IndexNode::Kind::kVar;
      }
      return IndexNode::Kind::kNone;  // Variable with no location.

    case DwarfTag::kMember:
      // Caller needs to check this case (see declaration comment).
      return IndexNode::Kind::kVar;

    default:
      // Don't index anything else.
      return IndexNode::Kind::kNone;
  }
}

const char* UnitIndexer::GetDieName(uint32_t index) {
  name_decoder_name_.reset();
  if (name_decoder_.Decode(unit_->getDIEAtIndex(index)) && name_decoder_name_)
    return *name_decoder_name_;
  return "";
}

// NOTE: Changes in this function may require updates in the slow path: AddStandaloneEntryToIndex().
void UnitIndexer::AddEntryToIndex(uint32_t index_me, IndexNode* root) {
  // The path to index always ends with the last thing being indexed (the path_ is in reverse).
  path_.clear();
  path_.push_back(&indexable_[index_me]);

  uint32_t cur = index_me;
  if (indexable_[index_me].decl_offset()) {
    // When the entry has a decl_offset, that means it's the implementation for e.g. a function.
    // The actual name comes from the declaration so start from that index.
    //
    // 99% of all declarations are within the same unit so look up in the current unit first. If
    // the current unit doesn't cover the offset, getDIEForOffset will return a null DIE.
    llvm::DWARFDie die = unit_->getDIEForOffset(indexable_[index_me].decl_offset());
    if (!die) {
      // DIE not found in this unit, try adding it to the index using the slow path which allows
      // cross-unit references.
      AddStandaloneEntryToIndex(index_me, root);
      return;
    }
    cur = unit_->getDIEIndex(die);

    if (!indexable_[index_me].name()) {
      // When there's no name, take the name from the declaration.
      if (!indexable_[cur].name()) {
        // The declaration has no name because the first pass didn't need to index it. Compute
        // the name now. Caching it on both the declaration and the implementation is useful because
        // many implementation can share the same declaration and this saves multiple name
        // retrievals.
        //
        // Here GetDieName() returns the empty string if there's no name which allows us to cache
        // the lack of a name and not recompute.
        indexable_[cur].set_name(GetDieName(cur));
      }
      indexable_[index_me].set_name(indexable_[cur].name());
    }
  }

  // If at this point we still don't have a name for the thing being indexed, give up trying to
  // index it.
  if (!indexable_[cur].name() || !indexable_[cur].name()[0])
    return;

  // Move to the abstract origin if present to start walking the scopes. The abstract origin (if
  // any) encodes the lexical scope (see has_abstract_origin() declaration above).
  if (indexable_[cur].has_abstract_origin()) {
    if (!GetAbstractOriginIndex(cur, &cur)) {
      // Fall back to the slow path. This will be all error cases as well as when the abstract
      // origin is in a different complication unit (I have not seen this in practice).
      AddStandaloneEntryToIndex(index_me, root);
      return;
    }
  }

  // Goes to the parent. The first item was added above, the loop below will add going up the
  // parent chain from there.
  cur = scanner_.GetParentIndex(cur);

  // Start indexing from here. We may find a cached one that will prevent us from having to
  // go to the root.
  IndexNode* index_from = root;

  // Collect the path from the current item (path_[0]) to its ultimate parent (path_.back()).
  while (cur != DwarfDieScanner::kNoParent && indexable_[cur].should_index()) {
    if (path_.size() > kMaxParentPath)
      return;  // Too many components, consider this item corrupt and don't index.

    if (indexable_[cur].index_node()) {
      // Already indexed this node (for example, this is a namespace that was already referenced).
      // so we can start inserting the path from this node.
      index_from = indexable_[cur].index_node();
      break;
    }
    path_.push_back(&indexable_[cur]);
    cur = scanner_.GetParentIndex(cur);
  }

  // Add the path to the index (walk in reverse to start from the root).
  for (int path_i = static_cast<int>(path_.size()) - 1; path_i >= 0; path_i--) {
    NamedSymbolRef* named_ref = path_[path_i];
    const char* name = named_ref->name() ? named_ref->name() : "";

    // Only save the DIE reference for the thing we're attempting to index (the leaf node at
    // path_[0]). Intermediate things like the namespaces and classes along the path don't need DIE
    // references unless Scan() independently decided those need indexing (should_index()). Not only
    // is adding these DIEs unnecessary, it can create unnamed type entries for things like
    // anonymous enums which we don't want.
    if (path_i == 0)
      index_from = index_from->AddChild(named_ref->kind(), name, *named_ref);
    else
      index_from = index_from->AddChild(named_ref->kind(), name);
    named_ref->set_index_node(index_from);
  }
}

// NOTE: Changes in this function may require updates in the fast path: AddEntryToIndex().
void UnitIndexer::AddStandaloneEntryToIndex(uint32_t index_me, IndexNode* index_root) {
  // This function can not use the unit_, scanner_, or name_decoder_ (and hence GetDieName())
  // because those all reference the current compilation unit. This code path must be able to handle
  // cross-unit references.

  // Thing to add to the index.
  const NamedSymbolRef& named_ref = indexable_[index_me];

  // Compute the name (avoiding GetDieName()) and the DIE to start indexing from.
  const char* name = named_ref.name();
  llvm::DWARFDie die;
  if (named_ref.decl_offset()) {
    // When there's a separate declaration, its parent encodes the scope information.
    die = context_->getDIEForOffset(named_ref.decl_offset());
    if (!die)
      return;  // Invalid decl offset, skip indexing.
    if (!name) {
      // The declaration can fill in the name if the name is not present on the implementation
      // (normally it's not there).
      name = die.getName(llvm::DINameKind::ShortName);
    }
  } else {
    // When there's no declaration, the name will already have been filled in (if present) to the
    // named_ref.
    die = context_->getDIEForOffset(named_ref.offset());
  }

  if (!name)
    return;  // This item has no name, can't index it.

  // When walking the dependency path, the abstract origin (if any) encodes the lexical scope
  // (see has_abstract_origin() declaration above).
  if (llvm::DWARFDie ao = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_abstract_origin))
    die = ao;

  // Stores the reverse path to the node we're inserting. This includes all scopes like namespaces
  // and classes in reverse order, but does not include the thing we're inserting itself.
  // So when insertng std::vector::vector this will be { "vector" (type), "std" (namespace) }.
  struct NameKind {
    NameKind(const char* n, IndexNode::Kind k) : name(n), kind(k) {}

    const char* name = nullptr;
    IndexNode::Kind kind = IndexNode::Kind::kNone;
  };
  std::vector<NameKind> path;

  // Walk the path upward saving the path. Don't include the leaf DIE.
  while ((die = die.getParent())) {
    if (path.size() > kMaxParentPath)
      return;  // Too deep nesting, give up.

    auto kind = GetKindForDie(die.getDebugInfoEntry());
    if (kind == IndexNode::Kind::kNone)
      break;  // Hit the top of what we want to index (like the unit).

    path.emplace_back(die.getName(llvm::DINameKind::ShortName), kind);
  }

  // Insert the containing elements (in reverse order to start from the top level and work inwards).
  IndexNode* cur_index = index_root;
  for (const auto& name_kind : Reversed(path))
    cur_index = cur_index->AddChild(name_kind.kind, name_kind.name ? name_kind.name : "");

  // Add the leaf item (holding the DIE reference) to the index.
  cur_index->AddChild(named_ref.kind(), name, named_ref);
}

bool UnitIndexer::GetAbstractOriginIndex(uint32_t source, uint32_t* abstract_origin_index) const {
  llvm::DWARFDie die = unit_->getDIEForOffset(indexable_[source].offset());
  if (!die)
    return false;  // Internal error, maybe symbols corrupt.

  llvm::DWARFDie ao = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_abstract_origin);
  if (!ao)
    return false;  // Symbols corrupt.
  if (ao.getDwarfUnit() != unit_)
    return false;  // Different compilation unit.

  *abstract_origin_index = unit_->getDIEIndex(ao);
  return true;
}

void RecursiveFindExact(const IndexNode* node, const Identifier& input, size_t input_index,
                        std::vector<IndexNode::SymbolRef>* result) {
  if (input_index == input.components().size()) {
    result->insert(result->end(), node->dies().begin(), node->dies().end());
    return;
  }

  // Recursively search each category in this node.
  for (auto* map : {&node->namespaces(), &node->types(), &node->functions(), &node->vars()}) {
    auto found = map->find(input.components()[input_index].GetName(false));
    if (found != map->end())  // Got a match for this category.
      RecursiveFindExact(&found->second, input, input_index + 1, result);
  }

  // Also implicitly search anonymous namespaces (without advancing the input index).
  auto found = node->namespaces().find(std::string());
  if (found != node->namespaces().end())
    RecursiveFindExact(&found->second, input, input_index, result);
}

}  // namespace

void Index::CreateIndex(llvm::object::ObjectFile* object_file, bool force_slow_path) {
  std::unique_ptr<llvm::DWARFContext> context = llvm::DWARFContext::create(*object_file);

  // Extracts the units to a place where we can destroy them after indexing is complete. This
  // construction order matches that of LLVM's DWARFContext so the indexes into this vector will
  // match the indices into DWARFContext's.
  llvm::DWARFUnitVector compile_units;
  context->getDWARFObj().forEachInfoSections([&](const llvm::DWARFSection& s) {
    compile_units.addUnitsForSection(*context, s, llvm::DW_SECT_INFO);
  });

  for (unsigned i = 0; i < compile_units.size(); i++)
    IndexCompileUnit(context.get(), compile_units[i].get(), i, force_slow_path);

  IndexFileNames();

  // Free compilation units after we process them. They will hold all of the parsed DIE data that we
  // don't need any more which can be multiple GB's for large programs.
  //
  // This must be done after indexing since some internal LLVM functions assume the units exist.
  for (unsigned i = 0; i < compile_units.size(); i++)
    compile_units[i].reset();
}

void Index::DumpFileIndex(std::ostream& out) const {
  for (const auto& [filename, file_index_entry] : file_name_index_) {
    const auto& [filepath, compilation_units] = *file_index_entry;
    out << filename << " -> " << filepath << " -> " << compilation_units.size() << " units\n";
  }
}

std::vector<IndexNode::SymbolRef> Index::FindExact(const Identifier& input) const {
  std::vector<IndexNode::SymbolRef> result;
  RecursiveFindExact(&root_, input, 0, &result);
  return result;
}

std::vector<std::string> Index::FindFileMatches(std::string_view name) const {
  std::string_view name_last_comp = ExtractLastFileComponent(name);

  std::vector<std::string> result;

  // Search all files whose last component matches (the input may contain more than one component).
  FileNameIndex::const_iterator iter = file_name_index_.lower_bound(name_last_comp);
  while (iter != file_name_index_.end() && iter->first == name_last_comp) {
    const auto& pair = *iter->second;
    if (PathEndsWith(pair.first, name))
      result.push_back(pair.first);
    ++iter;
  }

  return result;
}

std::vector<std::string> Index::FindFilePrefixes(const std::string& prefix) const {
  std::vector<std::string> result;

  auto found = file_name_index_.lower_bound(prefix);
  while (found != file_name_index_.end() && StringStartsWith(found->first, prefix)) {
    result.push_back(std::string(found->first));
    ++found;
  }
  return result;
}

const std::vector<unsigned>* Index::FindFileUnitIndices(const std::string& name) const {
  auto found = files_.find(name);
  if (found == files_.end())
    return nullptr;
  return &found->second;
}

size_t Index::CountSymbolsIndexed() const { return RecursiveCountDies(root_); }

void Index::IndexCompileUnit(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                             unsigned unit_index, bool force_slow_path) {
  UnitIndexer indexer(context, unit);
  indexer.set_force_slow_path(force_slow_path);

  indexer.Scan(&main_functions_);
  indexer.Index(&root_);

  IndexCompileUnitSourceFiles(context, unit, unit_index);
}

void Index::IndexCompileUnitSourceFiles(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                                        unsigned unit_index) {
  const llvm::DWARFDebugLine::LineTable* line_table = context->getLineTableForUnit(unit);
  if (!line_table)
    return;  // No line table for this unit.

  // This table is the size of the file name table (which confusingly counts from 0 up to the
  // the "size"). Entries are set to 1 when we've added them to the index already.
  std::vector<int> added_file;
  added_file.resize(line_table->Prologue.FileNames.size() + 1, 0);

  // We don't want to just add all the files from the line table to the index. The line table will
  // contain entries for every file referenced by the compilation unit, which includes declarations.
  // We want only files that contribute code, which in practice is a tiny fraction of the total.
  //
  // To get this, iterate through the unit's row table and collect all referenced file names.
  std::string file_name;
  for (size_t i = 0; i < line_table->Rows.size(); i++) {
    auto file_id = line_table->Rows[i].File;
    if (file_id > added_file.size())
      continue;

    if (!added_file[file_id]) {
      added_file[file_id] = 1;
      if (line_table->getFileNameByIndex(
              file_id, "", llvm::DILineInfoSpecifier::FileLineInfoKind::RelativeFilePath,
              file_name)) {
        // The files here can contain relative components like "/foo/bar/../baz". This is OK because
        // we want it to match other places in the symbol code that do a similar computation to get
        // a file name.
        files_[NormalizePath(file_name)].push_back(unit_index);
      }
    }
  }
}

void Index::IndexFileNames() {
  for (FileIndex::const_iterator iter = files_.begin(); iter != files_.end(); ++iter)
    file_name_index_.insert(std::make_pair(ExtractLastFileComponent(iter->first), iter));
}

}  // namespace zxdb
