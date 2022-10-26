// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LAZY_SYMBOL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LAZY_SYMBOL_H_

#include <stdint.h>

#include <memory>

#include "src/developer/debug/zxdb/symbols/symbol_factory.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Symbol;

// Symbols can be complex and in many cases are not required. This class holds enough information to
// construct a type from the symbol file as needed. Once constructed, it will cache the type for
// future use.
//
// It can optionally cache the result of the lookup. The rule is that any references that point
// "up" the tree must be uncached to avoid dependency cycles that will cause memory leaks.
class LazySymbolBase {
 public:
  LazySymbolBase() = default;  // Creates a !is_valid() one.
  LazySymbolBase(const LazySymbolBase& other) = default;
  LazySymbolBase(LazySymbolBase&& other) = default;

  LazySymbolBase(fxl::RefPtr<const SymbolFactory> factory, uint64_t die_offset)
      : factory_(std::move(factory)), die_offset_(die_offset) {}

  ~LazySymbolBase() = default;

  LazySymbolBase& operator=(const LazySymbolBase& other);
  LazySymbolBase& operator=(LazySymbolBase&& other);

  // Returns the DIE offset of the symbol that will be created. This will be 0 for invalid or
  // uninitialized LazySymbols, as well as most synthetic symbols (like built-in types and
  // especially types created in unit tests).
  //
  // Do not compare to 0 for testing validity; use is_valid() instead which will handle the
  // synthetic cases.
  //
  // Otherwise, the main use of this can be for comparing symbol identity without decoding them in
  // cases where you know the symbols aren't synthetic.
  uint64_t die_offset() const { return die_offset_; }

 protected:
  // Validity tests both for the factory and the symbol since non-lazy ones don't need a factory.
  // Not exposed publicly because the derived classes need to add additional conditions.
  bool is_valid() const { return factory_.get(); }

  fxl::RefPtr<Symbol> Construct() const;

  // Returns a cached null symbol for error cases.
  static fxl::RefPtr<Symbol> GetNullSymbol();

  const fxl::RefPtr<const SymbolFactory>& factory() const { return factory_; }

 private:
  // May be null if this contains no type reference.
  fxl::RefPtr<const SymbolFactory> factory_;

  // Offset of the DIE for this symbol.
  uint64_t die_offset_ = 0;
};

// Use for references from a parent symbol object to its children.
class LazySymbol : public LazySymbolBase {
 public:
  LazySymbol() = default;  // Creates a !is_valid() one.
  LazySymbol(const LazySymbol& other) = default;
  LazySymbol(LazySymbol&& other) = default;

  // If the value of the cached object is known at creation time, it can be provided as the
  // pre_cached parameter. Otherwise it is fine to leave this empty (it is an optimization to
  // prevent re-decoding).
  LazySymbol(fxl::RefPtr<const SymbolFactory> factory, uint64_t die_offset,
             fxl::RefPtr<Symbol> pre_cached = {});

  // Implicitly creates a non-lazy one with a pre-cooked object, mostly for tests.
  template <class SymbolType>
  LazySymbol(fxl::RefPtr<SymbolType> symbol) : LazySymbolBase(), symbol_(std::move(symbol)) {}
  LazySymbol(const Symbol* symbol);

  bool is_valid() const { return LazySymbolBase::is_valid() || symbol_.get(); }
  explicit operator bool() const { return is_valid(); }

  LazySymbol& operator=(const LazySymbol& other);
  LazySymbol& operator=(LazySymbol&& other);

  // Returns the type associated with this LazySymbol. If this class is invalid or the symbol fails
  // to resolve this will return an empty one. It will never return null.
  const Symbol* Get() const;

 private:
  mutable fxl::RefPtr<Symbol> symbol_;
};

// Use for references from a child symbol object to its parent.
class UncachedLazySymbol : public LazySymbolBase {
 public:
  UncachedLazySymbol() = default;  // Creates a !is_valid() one.
  UncachedLazySymbol(const UncachedLazySymbol& other) = default;
  UncachedLazySymbol(UncachedLazySymbol&& other) = default;

  UncachedLazySymbol(fxl::RefPtr<const SymbolFactory> factory, uint64_t die_offset);

  ~UncachedLazySymbol();

  bool is_valid() const { return LazySymbolBase::is_valid() || test_symbol_.get(); }
  explicit operator bool() const { return is_valid(); }

  UncachedLazySymbol& operator=(const UncachedLazySymbol& other);
  UncachedLazySymbol& operator=(UncachedLazySymbol&& other);

  // Returns the type associated with this LazySymbol. If this class is invalid or the symbol fails
  // to resolve this will return an empty one. It will never return a null pointer.
  fxl::RefPtr<Symbol> Get() const;

  // Returns a LazySymbol that references the same symbol as this object. This can be used in cases
  // where you need a LazySymbol from an uncached one. This is safe as long as you're not storing it
  // in such a way that it will create a reference cycle.
  //
  // If known in advance, the cached result may be provided as an optimization.
  LazySymbol GetCached(fxl::RefPtr<Symbol> cached_value = {}) const;

  // Makes an object with a static reference to an explicit symbol. Used for tests.
  //
  // Most code should use SymbolParentSetter which is less likely to cause leaks in tests. This
  // should normally only be called with a new object with no references to its children to avoid
  // a cycle. The normal example is the code that sets a mock unit for a test symbol in order to
  // control its language.
  static UncachedLazySymbol MakeUnsafe(fxl::RefPtr<Symbol> symbol);

 private:
  // Creates a non-lazy one with a pre-cooked object.
  UncachedLazySymbol(fxl::RefPtr<Symbol> symbol);

  // Used for injecting mock symbols for tests. See SymbolTestParentSetter.
  fxl::RefPtr<Symbol> test_symbol_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LAZY_SYMBOL_H_
