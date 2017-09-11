// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_MAP_H_
#define LIB_FIDL_CPP_BINDINGS_MAP_H_

#include <map>
#include <type_traits>

#include "lib/fidl/cpp/bindings/internal/map_internal.h"
#include "lib/fidl/cpp/bindings/internal/template_util.h"
#include "lib/fidl/cpp/bindings/macros.h"

namespace fidl {

// A move-only map that can handle move-only values. Map has the following
// characteristics:
//   - The map itself can be null, and this is distinct from empty.
//   - Keys must not be move-only.
//   - The Key-type's "<" operator is used to sort the entries, and also is
//     used to determine equality of the key values.
//   - There can only be one entry per unique key.
//   - Values of move-only types will be moved into the Map when they are added
//     using the insert() method.
template <typename Key, typename Value>
class Map {
 public:
  // Map keys cannot be move only classes.
  static_assert(!internal::IsMoveOnlyType<Key>::value,
                "Map keys cannot be move only types.");

  using KeyType = Key;
  using ValueType = Value;

  using Traits = internal::
      MapTraits<KeyType, ValueType, internal::IsMoveOnlyType<ValueType>::value>;
  using ValueForwardType = typename Traits::ValueForwardType;

  using Data_ =
      internal::Map_Data<typename internal::WrapperTraits<KeyType>::DataType,
                         typename internal::WrapperTraits<ValueType>::DataType>;

  Map() : is_null_(true) {}

  // Constructs a non-null Map containing the specified |keys| mapped to the
  // corresponding |values|.
  Map(fidl::Array<KeyType> keys, fidl::Array<ValueType> values)
      : is_null_(false) {
    FXL_DCHECK(keys.size() == values.size());
    for (size_t i = 0; i < keys.size(); ++i)
      Traits::Insert(&map_, keys[i], values[i]);
  }

  ~Map() {}

  Map(Map&& other) : is_null_(true) { Take(&other); }
  Map& operator=(Map&& other) {
    Take(&other);
    return *this;
  }

  // Copies the contents of some other type of map into a new Map using a
  // TypeConverter. A TypeConverter for std::map to Map is defined below.
  template <typename U>
  static Map From(const U& other) {
    return TypeConverter<Map, U>::Convert(other);
  }

  // Copies the contents of the Map into some other type of map. A TypeConverter
  // for Map to std::map is defined below.
  template <typename U>
  U To() const {
    return TypeConverter<U, Map>::Convert(*this);
  }

  // Destroys the contents of the Map and leaves it in the null state.
  void reset() {
    map_.clear();
    is_null_ = true;
  }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null_; }

  bool is_null() const { return is_null_; }

  // Indicates the number of keys in the map.
  size_t size() const { return map_.size(); }

  void mark_non_null() { is_null_ = false; }

  // Inserts a key-value pair into the map, moving the value by calling its
  // move constructor if it is a move-only type. Like std::map, this does not
  // insert |value| if |key| is already a member of the map.
  void insert(const KeyType& key, ValueForwardType value) {
    is_null_ = false;
    Traits::Insert(&map_, key, value);
  }

  // Returns a reference to the value associated with the specified key,
  // crashing the process if the key is not present in the map.
  ValueType& at(const KeyType& key) { return map_.at(key); }
  const ValueType& at(const KeyType& key) const { return map_.at(key); }

  // Returns a reference to the value associated with the specified key,
  // creating a new entry if the key is not already present in the map. A
  // newly-created value will be value-initialized (meaning that it will be
  // initialized by the default constructor of the value type, if any, or else
  // will be zero-initialized).
  ValueType& operator[](const KeyType& key) {
    is_null_ = false;
    return map_[key];
  }

  // Swaps the contents of this Map with another Map of the same type (including
  // nullness).
  void Swap(Map<KeyType, ValueType>* other) {
    std::swap(is_null_, other->is_null_);
    map_.swap(other->map_);
  }

  // Swaps the contents of this Map with an std::map containing keys and values
  // of the same type. Since std::map cannot represent the null state, the
  // std::map will be empty if Map is null. The Map will always be left in a
  // non-null state.
  void Swap(std::map<KeyType, ValueType>* other) {
    is_null_ = false;
    map_.swap(*other);
  }

  // Returns a new Map that contains a copy of the contents of this map.  If the
  // values are of a type that is designated move-only, they will be cloned
  // using the Clone() method of the type. Please note that calling this method
  // will fail compilation if the value type cannot be cloned (which usually
  // means that it is a Mojo handle type or a type that contains Mojo handles).
  Map Clone() const {
    Map result;
    result.is_null_ = is_null_;
    Traits::Clone(map_, &result.map_);
    return result;
  }

  // Indicates whether the contents of this map are equal to those of another
  // Map (including nullness). Keys are compared by the != operator. Values are
  // compared as follows:
  //   - Map, Array, Struct, or StructPtr values are compared by their Equals()
  //     method.
  //   - ScopedHandleBase-derived types are compared by their handles.
  //   - Values of other types are compared by their "==" operator.
  bool Equals(const Map& other) const {
    if (is_null() != other.is_null())
      return false;
    if (size() != other.size())
      return false;
    auto i = cbegin();
    auto j = other.cbegin();
    while (i != cend()) {
      if (i.GetKey() != j.GetKey())
        return false;
      if (!internal::ValueTraits<ValueType>::Equals(i.GetValue(), j.GetValue()))
        return false;
      ++i;
      ++j;
    }
    return true;
  }

 private:
  // A Map Iterator, templated for mutable and const iterator behaviour.
  // If |IsConstIterator| is true, the iterator behaves like a const-iterator.
  //
  // TODO(vardhan):  Make this adhere to the BidirectionalIterator concept.
  enum class IteratorMutability { kConst, kMutable };
  template <IteratorMutability MutabilityType = IteratorMutability::kMutable>
  class InternalIterator {
    using InternalIteratorType = typename std::conditional<
        MutabilityType == IteratorMutability::kConst,
        typename std::map<KeyType, ValueType>::const_iterator,
        typename std::map<KeyType, ValueType>::iterator>::type;

    using ReturnValueType =
        typename std::conditional<MutabilityType == IteratorMutability::kConst,
                                  const ValueType&,
                                  ValueType&>::type;

   public:
    InternalIterator() : it_() {}
    InternalIterator(InternalIteratorType it) : it_(it) {}

    // The key is always a const reference, but the value is conditional on
    // whether this is a const iterator or not.
    const KeyType& GetKey() const { return it_->first; }
    ReturnValueType GetValue() const { return it_->second; }

    InternalIterator& operator++() {
      ++it_;
      return *this;
    }
    InternalIterator<MutabilityType> operator++(int) {
      InternalIterator<MutabilityType> original(*this);
      ++it_;
      return original;
    }
    InternalIterator& operator--() {
      --it_;
      return *this;
    }
    InternalIterator<MutabilityType> operator--(int) {
      InternalIterator<MutabilityType> original(*this);
      --it_;
      return original;
    }
    InternalIterator<MutabilityType>& operator*() {
      return *this;
    }
    bool operator!=(const InternalIterator& rhs) const {
      return it_ != rhs.it_;
    }
    bool operator==(const InternalIterator& rhs) const {
      return it_ == rhs.it_;
    }

   private:
    InternalIteratorType it_;
  };

 public:
  using MapIterator = InternalIterator<IteratorMutability::kMutable>;
  using ConstMapIterator = InternalIterator<IteratorMutability::kConst>;

  // Provide read-only and mutable iteration over map members in a way similar
  // to STL collections.
  ConstMapIterator cbegin() const { return ConstMapIterator(map_.cbegin()); }
  ConstMapIterator cend() const { return ConstMapIterator(map_.cend()); }
  MapIterator begin() { return MapIterator(map_.begin()); }
  MapIterator end() { return MapIterator(map_.end()); }

  // Returns the iterator pointing to the entry for |key|, if present, or else
  // returns |cend()| or |end()|, respectively.
  ConstMapIterator find(const KeyType& key) const {
    return ConstMapIterator(map_.find(key));
  }
  MapIterator find(const KeyType& key) { return MapIterator(map_.find(key)); }

 private:
  void Take(Map* other) {
    reset();
    Swap(other);
  }

  std::map<KeyType, ValueType> map_;
  bool is_null_;

  FIDL_MOVE_ONLY_TYPE(Map);
};

// Copies the contents of an std::map to a new Map, optionally changing the
// types of the keys and values along the way using TypeConverter.
template <typename FidlKey,
          typename FidlValue,
          typename STLKey,
          typename STLValue>
struct TypeConverter<Map<FidlKey, FidlValue>, std::map<STLKey, STLValue>> {
  static Map<FidlKey, FidlValue> Convert(
      const std::map<STLKey, STLValue>& input) {
    Map<FidlKey, FidlValue> result;
    result.mark_non_null();
    for (auto& pair : input) {
      result.insert(TypeConverter<FidlKey, STLKey>::Convert(pair.first),
                    TypeConverter<FidlValue, STLValue>::Convert(pair.second));
    }
    return result;
  }
};

// Copies the contents of a Map to an std::map, optionally changing the types of
// the keys and values along the way using TypeConverter.
template <typename FidlKey,
          typename FidlValue,
          typename STLKey,
          typename STLValue>
struct TypeConverter<std::map<STLKey, STLValue>, Map<FidlKey, FidlValue>> {
  static std::map<STLKey, STLValue> Convert(
      const Map<FidlKey, FidlValue>& input) {
    std::map<STLKey, STLValue> result;
    if (!input.is_null()) {
      for (auto it = input.cbegin(); it != input.cend(); ++it) {
        result.insert(std::make_pair(
            TypeConverter<STLKey, FidlKey>::Convert(it.GetKey()),
            TypeConverter<STLValue, FidlValue>::Convert(it.GetValue())));
      }
    }
    return result;
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_MAP_H_
