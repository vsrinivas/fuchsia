// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INHERITANCE_PATH_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INHERITANCE_PATH_H_

#include <initializer_list>
#include <vector>

#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

// Represents a path of inheritance from one class to another.
//
// When one class derives from another, the base classes become effectively a member of the derived
// class. This represents a chain of such inheritance.
//
// Virtual inheritance makes things more complicated. When there is virtual inheritance, a base
// class doesn't live at a predefined offset but rather the compiler stores some way to find the
// base class. This allows the offset to vary according to what the current object hierarchy looks
// like. In this case, there is a DWARF express that must be evaluated that reads class memory to
// compute the offset.
//
// Virtual inheritance is uncommon so most hierarchies can be represented by a simple offset of one
// class within another.
class InheritancePath {
 public:
  struct Step {
    // Use for the 0th entry which has no "from".
    Step(fxl::RefPtr<Collection> c) : from(), collection(std::move(c)) {}

    // Use for normal steps.
    Step(fxl::RefPtr<InheritedFrom> f, fxl::RefPtr<Collection> c)
        : from(std::move(f)), collection(std::move(c)) {}

    // How to get to the current Step in the vector from the previous (n-1) item in the vector.
    // This will be null for path()[0] because it's the start of the inheritance path.
    fxl::RefPtr<InheritedFrom> from;

    // The collection at this step of the hierarchy.
    fxl::RefPtr<Collection> collection;

    // Comparison, based on object pointer equality. This is primarily for unit tests.
    bool operator==(const Step& other) const {
      return from.get() == other.from.get() && collection.get() == other.collection.get();
    }
    bool operator!=(const Step& other) const { return !operator==(other); }
  };
  using PathVector = std::vector<Step>;

  InheritancePath() = default;

  // To just supply one class and not inhertance information.
  explicit InheritancePath(fxl::RefPtr<Collection> collection) { path_.emplace_back(collection); }

  // Encodes a single level of inheritance from "derived" to "base".
  InheritancePath(fxl::RefPtr<Collection> derived, fxl::RefPtr<InheritedFrom> from,
                  fxl::RefPtr<Collection> base);

  // For a full path.
  InheritancePath(std::initializer_list<Step> steps) : path_(steps) {}

  // If possible, returns the offset of the oldest base class "path().back()" from the derived class
  // "path()[0]". As described in the class-level comment above, this will work as long as there is
  // no virtual inheritance. If there is virtual inheritance, this will return nullopt.
  std::optional<uint32_t> BaseOffsetInDerived() const;

  // The inheritance path. The derived class will be at path().front() and the derived class will be
  // at path().back(). The intermediate classes to get from one to the other will be sequenced
  // in-between:
  //
  //   ( Derived class = path[0].collection ) ----( path[1].from )----
  //       ( Intermediate class = path[1].collection ) ----( path[2].from )----
  //           ( Base class = path[2].collection )
  PathVector& path() { return path_; }
  const PathVector& path() const { return path_; }

  // Extracts a subset of the inheritance path.
  static constexpr size_t kToEnd = static_cast<size_t>(-1);
  InheritancePath SubPath(size_t begin_index, size_t len = kToEnd) const;

  // The "derived" is the more specific end (the one deriving from the other classes).
  const Collection* derived() const { return path_.front().collection.get(); }
  const fxl::RefPtr<Collection> derived_ref() const { return path_.front().collection; }

  // The "base" is the base class of derived that this path represents.
  const Collection* base() const { return path_.back().collection.get(); }
  const fxl::RefPtr<Collection> base_ref() const { return path_.back().collection; }

  bool operator==(const InheritancePath& other) const { return path_ == other.path_; }
  bool operator!=(const InheritancePath& other) const { return !operator==(other); }

 public:
  PathVector path_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INHERITANCE_PATH_H_
