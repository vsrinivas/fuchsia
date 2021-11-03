// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_ABSTRACT_CHILD_ITERATOR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_ABSTRACT_CHILD_ITERATOR_H_

#include <optional>
#include <vector>

#include "llvm/BinaryFormat/Dwarf.h"

namespace llvm {
class DWARFDie;
}  // namespace llvm

namespace zxdb {

// This file allows iterating over a DWARF "DIE"'s children, taking into account "abstract origin"
// information.
//
// EXAMPLE USAGE
// 🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶
//
//   llvm::DWARFDie die;  // Passed in from somewhere.
//   for (const llvm::DWARFDie& child : DwarfAbstractChildIterator(die)) {
//     DoSomething(child);
//   }
//
// If the caller already knows the abstract origin (or knows there isn't one), pass it (or null) in
// as the second argument to DwarfAbstractChildIterator().
//
// BACKGROUND
// 🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶
// DWARF inline functions are normally split into two parts: the "concrete inlined instance" which
// is a per-inlined-location description of the call, and the "abstract origin" which is a shared
// description of parameters and declaration information common to all inlined instances. This
// prevents unnecessary duplication of information.
//
// When a DIE has a DW_AT_abstract_origin attribute, it indicates the abstract origin that
// corresponds to the current concrete instance. This affects both the attributes and children of
// the current DIE.
//
// ATTRIBUTE HANDLING
// 🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶
// For the attributes, concrete instance attributes shadow and abstract origin attributes (allowing
// the concrete instance to provide more specific information). But any attributes not specified on
// the concrete instance fall back to their values in the abstract origin.
//
// This attribute shadowing logic is transparently handled by the DwarfDieDecoder and does not
// concern this class.
//
// CHILD HANDLING
// 🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶
// This class handles the "child iteration" cases where you want to iterate the children of a DIE
// and also handle any additional children provided by the abstract origin.
//
// The concrete instance can have children that shadow children of the abstract origin. This is used
// to provide things like the precise location of a variable in the inlined instance, while keeping
// the general type and name of the variable common on the abstract origin. When this happens the
// DW_AT_abstract_origin will be set on the child of the concrete instance and the DwarfDieDecoder
// will handle everything.
//
// But there is an additional case where if there are no instance-specific overrides on a child,
// that child can be omitted and the child on the abstract origin should be used. This class allows
// iteration over the children, and will magically add children of the abstract origin that were
// not overridden by the concrete instance.
//
// DWARF EXAMPLE
// 🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶🭶
// Abstract origin that provides the shared information:
//
//   0x00000555:   DW_TAG_subprogram
//                   DW_AT_specification (0x00000535 "_ZN9ForInline15InlinedFunctionEi")
//                   DW_AT_inline (DW_INL_inlined)
//                   DW_AT_object_pointer (0x0000055f)
//
//   0x0000055f:     DW_TAG_formal_parameter                 <=== THIS ONE IS ADDED
//                     DW_AT_name ("this")
//                     DW_AT_type (0x00000574 "ForInline*")
//                     DW_AT_artificial (true)
//
//   0x00000568:     DW_TAG_formal_parameter                 <=== THIS ONE IS NOT ITERATED OVER
//                     DW_AT_name ("param")                       (The attributes will be merged by
//                     DW_AT_decl_file ("type_test.cc")           the DwarfDieDecoder.)
//                     DW_AT_decl_line (84)
//                     DW_AT_type (0x000001a0 "int")
//
//   0x00000573:     NULL
//
// Concrete inlined instance. Note that the "this" parameter is not overridden here so the
// parameter from the abstract origin will "show through":
//
//   0x000005b1:     DW_TAG_inlined_subroutine
//                     DW_AT_abstract_origin (0x00000555 "_ZN9ForInline15InlinedFunctionEi")
//                     DW_AT_low_pc (0x0000000000001150)
//                     DW_AT_high_pc (0x0000000000001158)
//
//   0x000005c5:       DW_TAG_formal_parameter              <=== SHADOWS THE ABSTRACT ORIGIN ONE
//                       DW_AT_location (DW_OP_breg0 W0+1, DW_OP_stack_value)
//                       DW_AT_abstract_origin (0x00000568 "param")
//
//   0x000005ce:       NULL

// llvm::DWARFDies are not easily mockable and this logic is complex. As a result, this
// iterator is templatized so the unit test can specify a different DIE implementation.
template <class Die>
class DwarfAbstractChildIteratorBase {
 public:
  // This would be most naturally expressed as a coroutine with pseudocode that looks like this:
  //
  //   Die cur_die = concrete;
  //   set<Die> seen_origin_dies;
  //   while (cur_die) {
  //     // Go through the children at this level
  //     for (Die child : cur_die.children()) {
  //       if (!seen_origin_dies_.contains(child))
  //         YIELD child;
  //
  //       seen_origin_dies.insert(child.abstract_origin());
  //     }
  //
  //     // Move up one level in the abstract origin hierarchy.
  //     cur_die = cur_die.abstract_origin();
  //   }
  //
  // The logic here represents this loop in "unrolled" for as a single C++ iterator.
  class Iter {
   public:
    // Default-constructed one indicates the end. Computing the true end for this iterator requires
    // going through all of the abstract origins is slow, expecially since this is only used to
    // compute the loop termination. By ensuring that "everything clear" indicates end(), we can
    // save this work.
    Iter() = default;

    // Takes the concrete DIE as a parameter to iterate over its children.
    //
    // See DwarfAbstractChildIteratorBase constructor below for the parameters;
    explicit Iter(const Die& concrete);

    const Die& operator*() const { return **cur_iter_; }
    const Die* operator->() const { return (*cur_iter_).operator->(); }
    bool operator==(const Iter& other) const { return cur_iter_ == other.cur_iter_; }
    bool operator!=(const Iter& other) const { return !operator==(other); }
    const Iter& operator++() {
      ++(*cur_iter_);
      UpdateAfterIteratorChange();
      return *this;
    }

   private:
    // When not set, there's no other DIE to fall back to.
    bool has_next_abstract_origin() const { return next_abstract_origin_.isValid(); }

    // Indicates we hit the end of the *current* DIE's children (there could be further abstract
    // origins to fall back to, though).
    bool cur_iter_at_end() const { return *cur_iter_ == cur_die_.end(); }

    // Adds the current iterator's (if any) abstract origin (if any) to the seen_origin_dies_ list.
    // This will allow the current DIE to shadow its abstract origins and we'll skip those DIEs if
    // we get to them.
    void AddCurIterAbstractOrigin() {
      if (has_next_abstract_origin() && !cur_iter_at_end()) {
        if (Die origin =
                (*cur_iter_)->getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_abstract_origin);
            origin.isValid())
          seen_origin_dies_.push_back(origin.getOffset());
      }
    }

    // Returns true if the given child's offset is in the seen_origin_dies_. This means we've either
    // visited this DIE already or have seen a child that's shadowed it and it should not be
    // returned.
    bool HasSeenAbstractOriginChild(const Die& child) const;

    // Fixes up the iterators after setting or advancing the iterators.
    void UpdateAfterIteratorChange();

    Die cur_die_;  // !isValid() for end().

    // Set during the "concrete children" iteration loop in the coroutine pseudocode above. This
    // is nullopt when we reached the end.
    typename std::optional<typename Die::iterator> cur_iter_;

    // The abstract origin of the cur_die_ is computed whenever we change cur_die_. !isValid()
    // indicates no next abstract origin.
    //
    // Computing this in advance rather than when we switch to this DIE allows an optimization where
    // we can avoid tracking seen children when there's no next abstract origin. This is useful in
    // the common case where there's no abstract origin and we can short circuit all the special
    // logic.
    Die next_abstract_origin_;

    // A list of all references to all DIEs and abstract origins of those DIEs we've seen.
    // When we visited a child that itself has an abstract origin, that abstract origin should not
    // be revisited.
    //
    // In the example at the top, this corresponds to skipping the "param" DIE on the abstract
    // origin because we already visited it on the concrete instance. The parameters of the
    // "param" abstract origin will have been read automatically when not shadowed by the
    // DwarfDieDecoder when decoding the concrete instance.
    //
    // This is conceptually a set but there are typically only a couple of children and the DWARF
    // decoding can be performance critical. Doing brute-force in this case is normally faster than
    // doing heap allocations.
    std::vector<uint64_t> seen_origin_dies_;
  };

  // Takes the die whose children to iterate over.
  explicit DwarfAbstractChildIteratorBase(const Die& die) : die_(die) {}

  Iter begin() { return Iter(die_); }
  const Iter& end() const { return end_; }

 private:
  const Die& die_;

  // Empty iterator to be efficiently returned from end().
  const Iter end_;
};

template <class Die>
DwarfAbstractChildIteratorBase<Die>::Iter::Iter(const Die& concrete) : cur_die_(concrete) {
  next_abstract_origin_ =
      cur_die_.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_abstract_origin);

  // Only need to track seen DIEs if there's an abstract origin. In the common case where will be
  // no abstract origin.
  if (has_next_abstract_origin()) {
    seen_origin_dies_.reserve(16);
    seen_origin_dies_.push_back(cur_die_.getOffset());
  }

  cur_iter_ = cur_die_.begin();
  UpdateAfterIteratorChange();  // In case the current DIE has no children.
}

// This function does several things:
//  - Transparently skips nodes we don't need to visit. These are the ones tracked in
//    seen_origin_dies_ which were the ones shadowed by DIEs we already visited.
//  - Advances to the next deeper abstract origin when we reach the end of the current one.
//  - Clears the iterators when we hit the end. This ensures that it mathches tne end() Iter
//    object which has null iterators.
template <class Die>
void DwarfAbstractChildIteratorBase<Die>::Iter::UpdateAfterIteratorChange() {
  // Skip over any unnecessary DIEs in the current child.
  while (!cur_iter_at_end() && HasSeenAbstractOriginChild(**cur_iter_)) {
    // All DIEs we iterate over have to add their abstract origins added, even if we skip those DIEs
    // for returned children. This is because if there are multiple levels of abstract origins, the
    // one shadowed child could tiself shadow another abstract origin at a deeper level.
    AddCurIterAbstractOrigin();
    ++(*cur_iter_);
  }

  // This needs to be a loop to account for abstract origins with no children (probably this won't
  // appear in practice but can theoretically happen).
  while (has_next_abstract_origin() && cur_iter_at_end()) {
    // Got to the end of this abstract origin, advance to the next one.
    cur_die_ = next_abstract_origin_;
    cur_iter_ = cur_die_.begin();
    next_abstract_origin_ =
        cur_die_.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_abstract_origin);

    if (has_next_abstract_origin())
      seen_origin_dies_.push_back(cur_die_.getOffset());

    // Skip any unnecessary DIEs in the current abstract origin.
    while (!cur_iter_at_end() && HasSeenAbstractOriginChild(**cur_iter_)) {
      AddCurIterAbstractOrigin();
      ++(*cur_iter_);
    }
  }

  if (cur_iter_at_end()) {
    cur_iter_ = std::nullopt;  // Hit end, clear the iterator.
  } else {
    AddCurIterAbstractOrigin();
  }
}

template <class Die>
bool DwarfAbstractChildIteratorBase<Die>::Iter::HasSeenAbstractOriginChild(const Die& child) const {
  uint64_t child_offset = child.getOffset();
  return std::find(seen_origin_dies_.begin(), seen_origin_dies_.end(), child_offset) !=
         seen_origin_dies_.end();
}

using DwarfAbstractChildIterator = DwarfAbstractChildIteratorBase<llvm::DWARFDie>;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_ABSTRACT_CHILD_ITERATOR_H_
