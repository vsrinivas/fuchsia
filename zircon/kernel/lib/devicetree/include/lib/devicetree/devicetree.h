// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_DEVICETREE_H_
#define ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_DEVICETREE_H_

#include <zircon/assert.h>
#include <zircon/types.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <fbl/intrusive_container_utils.h>
#include <fbl/intrusive_double_list.h>

// This library provides abstractions and utilities for dealing with
// 'devicetree's in their flattened, binary form (.dtb). Although not used in
// Fuchsia-compliant bootloaders (which use the ZBI protocol), dealing with
// devicetrees is necessary for our boot shims.
//
// We follow the v0.3 spec available at
// https://devicetree-specification.readthedocs.io/en/v0.3

namespace devicetree {

using ByteView = std::basic_string_view<uint8_t>;

// Represents the node name of a devicetree. This has the same API as
// std::string_view and is meant to be used the same way. It requires its own
// type solely to enable use of the fbl intrusive containers with on-stack
// elements to avoid dynamic allocation.
struct Node : public std::string_view,
              fbl::DoublyLinkedListable<Node*, fbl::NodeOptions::AllowCopy> {
  Node(std::string_view name) : std::string_view(name) {}
};

// See
// https://devicetree-specification.readthedocs.io/en/v0.3/devicetree-basics.html#node-name-requirements
// for specification and definition of name and unit address.
struct NodeNameTokens {
  std::string_view name;
  std::string_view unit_addr;
};

// Splits a node's name into the tokens of interest.
inline NodeNameTokens SplitNodeName(std::string_view node) {
  size_t ind = node.find_first_of('@');
  return {
      (ind == std::string_view::npos) ? node : node.substr(0, ind),
      (ind == std::string_view::npos || ind + 1 >= node.size()) ? std::string_view{}
                                                                : node.substr(ind + 1),
  };
}

// Represents a rooted path of nodes in a devicetree.
// This can be used interchangeably with `const std::list<std::string_view>`
// to iterate over the elements in a path with implied `/` separators.
using NodePath = fbl::DoublyLinkedList<Node*>;

// Some property values encode a list of NUL-terminated strings.
// This is also useful for separating path strings at '/' characters.
template <char Separator = '\0'>
class StringList {
 public:
  class iterator {
   public:
    constexpr iterator() = default;
    constexpr iterator(const iterator&) = default;
    constexpr iterator& operator=(const iterator&) = default;

    constexpr bool operator==(const iterator& other) const {
      return len_ == other.len_ && rest_.size() == other.rest_.size();
    }
    constexpr bool operator!=(const iterator& other) const { return !(*this == other); }

    constexpr iterator& operator++() {  // prefix
      if (len_ == std::string_view::npos) {
        // This was the last word.
        rest_ = {};
      } else if (rest_.empty()) {
        // This was the last word and it was empty.
        len_ = std::string_view::npos;
      } else {
        // Move to the next word.  If it's empty, record len_ = 0.
        // Otherwise len_ = npos if it's the last word and not empty.
        rest_ = rest_.substr(len_ + 1);
        len_ = rest_.empty() ? 0 : rest_.find_first_of(Separator);
      }
      return *this;
    }

    constexpr iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    constexpr std::string_view operator*() const { return rest_.substr(0, len_); }

   private:
    friend StringList;

    std::string_view rest_;
    size_t len_ = std::string_view::npos;
  };
  using const_iterator = iterator;

  constexpr explicit StringList(std::string_view str) : data_(str) {}

  constexpr iterator begin() const {
    iterator it;
    it.rest_ = data_;
    it.len_ = data_.find_first_of(Separator);
    return it;
  }

  constexpr iterator end() const { return iterator{}; }

 private:
  std::string_view data_;
};

// See
// https://devicetree-specification.readthedocs.io/en/v0.3/devicetree-basics.html#property-values
// for the types and representations of possible property values.
class PropertyValue {
 public:
  PropertyValue(ByteView bytes) : bytes_(bytes) {}

  ByteView AsBytes() const { return bytes_; }

  // Note that the spec requires this to be a NUL-terminated string.
  std::string_view AsString() const {
    ZX_ASSERT(!bytes_.empty());
    ZX_ASSERT(bytes_.back() == '\0');
    // Exclude NUL terminator from factoring into the string_view's size.
    return {reinterpret_cast<const char*>(bytes_.data()), bytes_.size() - 1};
  }

  StringList<> AsStringList() const { return StringList<>{AsString()}; }

  uint32_t AsUint32() const;

  uint64_t AsUint64() const;

  // A value without size represents a Boolean property whose truthiness is a
  // function of the nature of the property's name and its presence in the tree.
  bool AsBool() const {
    ZX_ASSERT(bytes_.empty());
    return true;
  }

 private:
  ByteView bytes_;
};

struct Property {
  std::string_view name;
  PropertyValue value;
};

// A view-like object representing a set of properties given by a range within the
// structure block of a flattened devicetree.
// https://devicetree-specification.readthedocs.io/en/v0.3/flattened-format.html#structure-block
class Properties {
 public:
  // Constructed from a byte span beginning just after the first internal::FDT_PROP token
  // in a flattened block of properties (or an otherwise empty one), along with
  // a string block for property name look-up.
  Properties(ByteView property_block, std::string_view string_block)
      : property_block_(property_block), string_block_(string_block) {}

  // A property iterator is identified with the position in a block of
  // properties at which a property is encoded. Incrementing the iterator
  // amounts to seeking in the range for the offset of the next property.
  //
  // It implements std's LegacyInputIterator.
  // https://en.cppreference.com/w/cpp/named_req/InputIterator
  class iterator {
   public:
    using value_type = Property;
    using difference_type = ptrdiff_t;  // std::next complains without it.
    using pointer = Property*;
    using reference = Property&;
    using iterator_category = std::input_iterator_tag;

    iterator() = default;

    iterator(const iterator&) = default;

    iterator& operator=(const iterator&) = default;

    bool operator==(const iterator& it) const { return position_ == it.position_; }

    bool operator!=(const iterator& it) const { return !(*this == it); }

    Property operator*() const;

    iterator& operator++();  // prefix incrementing.

    iterator operator++(int) {  // postfix incrementing.
      auto prev = *this;
      ++(*this);
      return prev;
    }

   private:
    // Only to be called by Properties::begin and Properties::end().
    iterator(ByteView position, std::string_view string_block)
        : position_(position), string_block_(string_block) {}
    friend class Properties;

    // Pointer into the block of properties along with remaining size from that
    // point onward.
    ByteView position_;
    std::string_view string_block_;
  };

  iterator begin() const { return iterator{property_block_, string_block_}; }

  iterator end() const { return iterator{{property_block_.end(), 0}, string_block_}; }

 private:
  const ByteView property_block_;
  const std::string_view string_block_;
};

class MemoryReservations {
 public:
  struct value_type {
    uint64_t start, size;
  };

  class iterator {
   public:
    iterator() = default;
    iterator(const iterator&) = default;
    iterator& operator=(const iterator&) = default;

    bool operator==(const iterator& other) const {
      return mem_rsvmap_.size() == other.mem_rsvmap_.size();
    }
    bool operator!=(const iterator& other) const { return !(*this == other); }

    iterator& operator++();  // prefix

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    value_type operator*() const;

   private:
    friend MemoryReservations;

    void Normalize();

    ByteView mem_rsvmap_;
  };
  using const_iterator = iterator;

  iterator begin() const;

  iterator end() const { return iterator{}; }

 private:
  friend class Devicetree;

  ByteView mem_rsvmap_;
};

// Represents a devicetree. This class does not dynamically allocate
// memory and is appropriate for use in all low-level environments.
class Devicetree {
 public:
  // Consumes a view representing the range of memory the flattened devicetree
  // is expected to take up, its beginning pointing to that of the binary data
  // and its size giving an upper bound on the size that the data is permitted
  // to occupy.
  //
  // It is okay to pass a view size of SIZE_MAX if an upper bound on the size
  // is not known; only up to the size encoded in the devicetree header will
  // be dereferenced.
  explicit Devicetree(ByteView fdt);

  // The size in bytes of the flattened devicetree blob.
  size_t size_bytes() const { return fdt_.size(); }

  // Walk provides a means of walking a devicetree. It purposefully avoids
  // reliance on specifying a 'walker' by means of inheritance so as to avoid
  // vtables, which are not permitted in the phys environment. A WalkCallback
  // here is an object callable with the signature of
  // `(const NodePath&, Properties) -> bool`
  // and is called depth-first at every node for which no ancestor node
  // returned false. That is, if a walker returns false at a given node, then
  // the subtree rooted there and its member nodes are said to be "pruned" and
  // the walker will not be called on them.
  //
  // This method will only have one instantiation in practice (in any
  // conceivable context), so the templating should not result in undue bloat.
  //
  // TODO: If boot shims start using multiple walks, refactor this to move the
  // logic into a non-template function and use a function pointer callback,
  // with this templated wrapper calling that with a captureless lambda to call
  // the templated walker.
  template <typename F>
  void Walk(F&& walker) {
    static_assert(std::is_invocable_r_v<bool, F, const NodePath&, Properties>,
                  "wrong callback signature");
    if constexpr (std::is_rvalue_reference_v<F>) {
      // An rvalue reference argument has to be moved into a local copy to be
      // passed by reference.
      F moved_walker = std::move(walker);
      WalkTree(moved_walker);
    } else {
      // An lvalue reference or an argument passed by value is just forwarded.
      WalkTree(walker);
    }
  }

  MemoryReservations memory_reservations() const {
    MemoryReservations result;
    result.mem_rsvmap_ = mem_rsvmap_;
    return result;
  }

 private:
  using WalkerCallback = bool(void*, const NodePath&, Properties);

  Devicetree(ByteView fdt, ByteView struct_block, std::string_view string_block)
      : fdt_(fdt), struct_block_(struct_block), string_block_(string_block) {}

  // Given a byte span that starts at a flattened property block, returns the
  // iterator in that span pointing to the 4-byte aligned end of that block.
  ByteView EndOfPropertyBlock(ByteView bytes);

  template <typename Walker>
  void WalkTree(Walker& walker) {
    WalkerCallback* callback = [](void* callback_arg, const NodePath& path,
                                  Properties props) -> bool {
      return (*static_cast<Walker*>(callback_arg))(path, props);
    };
    WalkTree(callback, &walker);
  }

  void WalkTree(WalkerCallback* callback, void* callback_arg);

  ByteView WalkSubtree(ByteView subtree, NodePath* path, WalkerCallback* callback,
                       void* callback_arg, bool visit);

  ByteView fdt_;
  // https://devicetree-specification.readthedocs.io/en/v0.3/flattened-format.html#structure-block
  ByteView struct_block_;
  // https://devicetree-specification.readthedocs.io/en/v0.3/flattened-format.html#strings-block
  std::string_view string_block_;
  // https://devicetree-specification.readthedocs.io/en/v0.3/flattened-format.html#memory-reservation-block
  ByteView mem_rsvmap_;
};

}  // namespace devicetree

#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_DEVICETREE_H_
