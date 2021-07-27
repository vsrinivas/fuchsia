// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_INTERNAL_CONTAINER_H_
#define LIB_ZBITL_INTERNAL_CONTAINER_H_

#include <inttypes.h>
#include <lib/fitx/result.h>
#include <lib/zbitl/checking.h>
#include <lib/zbitl/storage_traits.h>
#include <zircon/assert.h>

#include <functional>
#include <optional>
#include <type_traits>
#include <variant>

namespace zbitl {

// TODO(joshuaseaton): The following forward-declarations exist for friend
// statements that could otherwise be avoided; let's remove them for better
// layering hygiene.

// Forward-declared; defined below.
template <typename Storage>
class View;

// Forward-declared; defined in image.h.
template <typename Storage>
class Image;

namespace internal {

///
/// ExampleContainerTraits serves as an definitional examplar for how
/// "container traits" should be structured. Container traits provide types,
/// and static constants and methods that abstract how to parse and navigate
/// a particular container format (e.g., ZBI or BOOTFS).
///
/// An "item" is an entry within the container, which is expected to be encoded
/// by an ("item header", "payload") pair. The payload is the raw binary
/// content of the item, while the item header provides its metadata, most
/// important of which is the payload's size and its location in the container.
/// When parsing, the traits should provide a means of navigating from an item
/// header to either its payload or to the next item header.
///
/// The container is expected to have a special header at offset 0, its
/// "container header", giving metadata on the container itself, including its
/// total size. The first item header is expected to immediately follow the
/// container header.
struct ExampleContainerTraits {
  /// The type of a container header, expected to be POD.
  struct container_header_type {};

  /// The type of an item header, expected to be POD.
  struct item_header_type {};

  /// The user-facing representation of an item header, which wraps the
  /// format's raw item_header_type. Being a C-style struct with fields
  /// possibly only relevant to a parser, the raw item header type may not be a
  /// relatively useful type to expose to the user.
  ///
  /// In practice, the wrapper either stores the `item_header_type` directly
  /// or it holds a pointer into someplace owned or viewed by an associated
  /// Storage object.  In the latter case, i.e. when Storage represents
  /// something already in memory, `item_header_wrapper` should be no larger
  /// than a plain pointer.
  template <typename StorageTraits>
  class item_header_wrapper {
   private:
    using TraitsHeader = typename StorageTraits::template LocalizedReadResult<item_header_type>;

   public:
    /// Constructible from an item header, as it would result from a localized
    /// read.
    explicit item_header_wrapper(const TraitsHeader& header) {}

    /// Default constructible, copyable, movable, copy-assignable, and move-
    /// assignable.
    item_header_wrapper() = default;
    item_header_wrapper(const item_header_wrapper&) = default;
    item_header_wrapper(item_header_wrapper&&) noexcept = default;
    item_header_wrapper& operator=(const item_header_wrapper&) = default;
    item_header_wrapper& operator=(item_header_wrapper&&) noexcept = default;

    /// The header can be dereferenced as if the type were
    /// `const item_header_t*` (i.e. `*header` or `header->member`).
    const item_header_type& operator*() const { return std::declval<const item_header_wrapper&>(); }
    const item_header_type* operator->() const { return nullptr; }
  };

  /// Error encapsulates errors encountered in navigating the container, either
  /// those coming from the storage backend or from structural issues with the
  /// container itself. ErrorTraits corresponds to the `ErrorTraits` member
  /// type of a StorageTraits specialization; it serves as a template parameter
  /// so that Error may be defined in terms of the associated storage error
  /// type (e.g., as a member).
  template <typename ErrorTraits>
  struct Error {};

  /// The name of the associated C++ container type. This is given as a C-style
  /// string (as opposed to a std::string_view) as the constant is only meant
  /// to provide context within printf() statements.
  static constexpr const char* kContainerType = "zbitl::ExampleContainer";

  /// The expected alignment - within the container - of an item header. Must
  /// be a power of two.
  static constexpr uint32_t kItemAlignment = 1;

  /// Payloads are expected to be followed by padding up to a multiple of this
  /// value. This quantity is unrelated to the size of the payload itself.
  static constexpr uint32_t kPayloadPaddingAlignment = 1;

  /// Whether the payloads lie within the container. A container format may not
  /// include them properly and instead point to the data elsewhere in the
  /// storage (as is the case with BOOTFS).
  static constexpr bool kPayloadsAreContained = false;

  /// Returns the size of a container, as it is encoded in the header. The size
  /// includes that of the header. It is the responsibility of the caller to
  /// validate the returned size against the actual storage capacity.
  static uint32_t ContainerSize(const container_header_type& header) { return sizeof(header); }

  /// Returns the exact size of an item's payload (excluding padding).
  static uint32_t PayloadSize(const item_header_type& header) { return 0; }

  /// Returns the offset at which a payload is to be found, given the
  /// associated item header and that header's offset into the container.
  static uint32_t PayloadOffset(const item_header_type& header, uint32_t item_offset) { return 0; }

  /// Returns the offset of the next item header, given a current item header
  /// and its offset into the container.
  ///
  /// TODO(joshuaseaton): in general, a container header may affect navigation
  static uint32_t NextItemOffset(const item_header_type& header, uint32_t item_offset) { return 0; }

  /// Validates item and container headers, returning a description of the
  /// failure in that event. The check is agnostic of storage capacity; for
  /// example, whether any encoded lengths are sensible are left to the caller
  /// to validate against the actual storage capacity.
  static fitx::result<std::string_view> CheckContainerHeader(const container_header_type& header) {
    return fitx::error{"unimplemented"};
  }

  static fitx::result<std::string_view> CheckItemHeader(const item_header_type& header) {
    return fitx::error{"unimplemented"};
  }

  /// Converts the context of an iteration failure into an Error.
  template <typename StorageTraits>
  static Error<typename StorageTraits::ErrorTraits> ToError(
      typename StorageTraits::storage_type& storage,  //
      std::string_view reason,                        //
      /// If the error occurred within the context of a particular item, this
      /// is its offset; else, for problems with the overall container, this is
      /// zero.
      uint32_t item_offset,
      /// Offset at which the error occurred.
      uint32_t error_offset,
      /// If the error occurred within the context of a particular item, this
      /// is a pointer to its header; else, for problems with the overall
      /// container, this is nullptr. In particular, we expect `header` to be
      /// null iff `item_offset` is zero. When `header` is obtained through an
      /// iterator, the former's lifetime is expected to be tied to the
      /// latter's.
      ///
      /// std::optional<item_header_type> is not used here to account for any
      /// flexible array members, which std::optional forbids.
      const item_header_type* header = nullptr,
      std::optional<typename StorageTraits::ErrorTraits::error_type> storage_error = std::nullopt) {
    return {};
  }
};

/// Container provides the main container business logic for iterating over,
/// error-checking, and generally inspecting supported container formats. À la
/// Curious Recurring Template Pattern, it is expected that concrete container
/// class implementations (i.e., `Derived` types) inherit from this class and
/// supply a `ContainerTraits` implementation that meets the specification of
/// `ExampleContainerTraits` above.
template <typename Derived, typename Storage, typename ContainerTraits>
class Container {
 private:
  using container_header_type = typename ContainerTraits::container_header_type;
  using item_header_type = typename ContainerTraits::item_header_type;

 public:
  using storage_type = Storage;
  using Traits = ExtendedStorageTraits<storage_type>;
  using storage_error_type = typename Traits::ErrorTraits::error_type;
  using Error = typename ContainerTraits::template Error<typename Traits::ErrorTraits>;
  using item_header_wrapper = typename ContainerTraits::template item_header_wrapper<Traits>;

  Container() = default;
  Container(const Container&) = default;
  Container& operator=(const Container&) = default;

  // This is almost the same as the default move behavior.  But it also
  // explicitly resets the moved-from error state to kUnused so that the
  // moved-from Container can be destroyed without checking it.
  Container(Container&& other)
      : storage_(std::move(other.storage_)), error_(std::move(other.error_)), limit_(other.limit_) {
    other.error_ = Unused{};
    other.limit_ = 0;
  }
  Container& operator=(Container&& other) {
    error_ = std::move(other.error_);
    other.error_ = Unused{};
    storage_ = std::move(other.storage_);
    limit_ = other.limit_;
    other.limit_ = 0;
    return *this;
  }

  explicit Container(storage_type storage) : storage_(std::move(storage)) {}

  ~Container() {
    ZX_ASSERT_MSG(!std::holds_alternative<Error>(error_), "%s destroyed after error without check",
                  ContainerTraits::kContainerType);
    ZX_ASSERT_MSG(!std::holds_alternative<NoError>(error_),
                  "%s destroyed after successful iteration without check",
                  ContainerTraits::kContainerType);
  }

  /// The payload type is provided by the StorageTraits specialization.  It's
  /// opaque to Container, but must be default-constructible, copy-
  /// constructible, and copy-assignable.  It's expected to have "view"-style
  /// semantics, i.e. be small and not own any storage itself but only refer to
  /// storage owned by the Storage object.
  using payload_type = typename Traits::payload_type;

  /// The element type is a trivial struct morally equivalent to
  /// std::pair<item_header_wrapper, payload_type>.  Both member types are
  /// default-constructible, copy-constructible, and copy-assignable, so
  /// value_type as a whole is as well.
  struct value_type {
    item_header_wrapper header;
    payload_type payload;
  };

  /// Check the container for errors after using iterators.  When begin() or
  /// iterator::operator++() encounters an error, it simply returns end() so
  /// that loops terminate normally.  Thereafter, take_error() must be called
  /// to check whether the loop terminated because it iterated past the last
  /// item or because it encountered an error.  Once begin() has been called,
  /// take_error() must be called before the Container is destroyed, so no
  /// error goes undetected.  After take_error() is called the error state is
  /// consumed and take_error() cannot be called again until another begin() or
  /// iterator::operator++() call has been made.
  [[nodiscard]] fitx::result<Error> take_error() {
    ErrorState result = std::move(error_);
    error_ = Taken{};
    if (std::holds_alternative<Error>(result)) {
      return fitx::error{std::move(std::get<Error>(result))};
    }
    ZX_ASSERT_MSG(!std::holds_alternative<Taken>(result), "%s::take_error() was already called",
                  ContainerTraits::kContainerType);
    return fitx::ok();
  }

  /// If you explicitly don't care about any error that might have terminated
  /// the last loop early, then call ignore_error() instead of take_error().
  void ignore_error() { static_cast<void>(take_error()); }

  /// Trivial accessors for the underlying Storage (view) object.
  storage_type& storage() { return storage_; }
  const storage_type& storage() const { return storage_; }

  class iterator {
   public:
    /// The default-constructed iterator is invalid for all uses except
    /// equality comparison.
    iterator() = default;

    iterator& operator=(const iterator&) = default;

    bool operator==(const iterator& other) const {
      return other.view_ == view_ && other.offset_ == offset_;
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

    iterator& operator++() {  // prefix
      Assert(__func__);
      view_->StartIteration();
      const uint32_t next_item_offset = ContainerTraits::NextItemOffset(*value_.header, offset_);
      Update(next_item_offset);
      return *this;
    }

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    const Container::value_type& operator*() const {
      Assert(__func__);
      return value_;
    }

    const Container::value_type* operator->() const {
      Assert(__func__);
      return &value_;
    }

    uint32_t item_offset() const { return offset_; }

    uint32_t payload_offset() const {
      Assert(__func__);
      return ContainerTraits::PayloadOffset(*(value_.header), offset_);
    }

    Derived& view() const {
      ZX_ASSERT_MSG(view_, "%s on default-constructed %s::iterator", __func__,
                    ContainerTraits::kContainerType);
      return *static_cast<Derived*>(view_);
    }

    // Iterator traits.
    using iterator_category = std::input_iterator_tag;
    using reference = Container::value_type&;
    using value_type = Container::value_type;
    using pointer = Container::value_type*;
    using difference_type = size_t;

   private:
    // Assert accessed by View<StorageType>::EditHeader().
    template <typename StorageType>
    friend class ::zbitl::View;

    // Private fields accessed by Image<StorageType>::Append().
    template <typename StorageType>
    friend class ::zbitl::Image;

    // The default-constructed state is almost the same as the end() state:
    // nothing but operator==() should ever be called if view_ is nullptr.
    Container* view_ = nullptr;

    // The offset into the ZBI of the current item's header.  This is 0 in
    // default-constructed iterators and kEnd_ in end() iterators, where
    // operator*() can never be called.  A valid non-end() iterator holds the
    // header and payload (references) of the current item for operator*() to
    // return. If offset_ is at the end of the container, then operator++()
    // will yield end().
    uint32_t offset_ = 0;

    // end() uses a different offset_ value to distinguish a true end iterator
    // from a particular view from a default-constructed iterator from nowhere.
    static constexpr uint32_t kEnd_ = std::numeric_limits<uint32_t>::max();

    // This is left uninitialized until a successful increment sets it.
    // It is only examined by a dereference, which is invalid without
    // a successful increment.
    value_type value_{};

    // This is called only by begin() and end().
    friend class Container;
    iterator(Container* view, bool is_end) : view_(view) {
      ZX_DEBUG_ASSERT(view_);
      if (is_end) {
        offset_ = kEnd_;
      } else {
        Update(sizeof(container_header_type));
      }
    }

    // Updates the state of the iterator to reflect a new offset.
    void Update(uint32_t next_item_offset) {
      ZX_DEBUG_ASSERT(next_item_offset >= sizeof(container_header_type));
      ZX_DEBUG_ASSERT_MSG(next_item_offset <= view_->limit_,
                          "%s::iterator next_item_offset %#" PRIx32 " > limit_ %#" PRIx32,
                          ContainerTraits::kContainerType, next_item_offset, view_->limit_);
      ZX_DEBUG_ASSERT(next_item_offset % ContainerTraits::kItemAlignment == 0);

      if (next_item_offset == view_->limit_) {
        // Reached the end.
        *this = view_->end();
        return;
      }
      if (view_->limit_ < next_item_offset ||
          view_->limit_ - next_item_offset < sizeof(item_header_type)) {
        Fail("container too short for next item header");
        return;
      }

      if (auto header = view_->ItemHeader(next_item_offset); header.is_error()) {
        // Failed to read the next header.
        Fail("cannot read item header", std::move(header.error_value()));
        return;
      } else if (auto header_error = ContainerTraits::CheckItemHeader(header.value());
                 header_error.is_error()) {
        Fail(header_error.error_value());
        return;
      } else {
        value_.header = item_header_wrapper(header.value());
      }

      // If payloads lie within the container, we validate that this particular
      // payload does indeed fit within; else, we can only check that it fits
      // within the storage itself.
      uint32_t payload_limit = view_->limit_;
      if constexpr (!ContainerTraits::kPayloadsAreContained) {
        auto result = Traits::Capacity(view_->storage());
        if (result.is_error()) {
          Fail("cannot determine storage capacity", std::move(result).error_value(), 0);
          return;
        }
        payload_limit = std::move(result).value();
      }

      const uint32_t payload_offset =
          ContainerTraits::PayloadOffset(*value_.header, next_item_offset);
      const uint32_t payload_size = ContainerTraits::PayloadSize(*value_.header);
      const uint32_t padded_payload_size =
          (payload_size + ContainerTraits::kPayloadPaddingAlignment - 1) &
          -ContainerTraits::kPayloadPaddingAlignment;
      if (payload_offset > payload_limit ||
          padded_payload_size < payload_size ||  // ensure aligned size didn't overflow
          padded_payload_size > payload_limit - payload_offset) {
        if constexpr (ContainerTraits::kPayloadsAreContained) {
          Fail("container too short for next item payload");
        } else {
          Fail("storage too small for next item payload");
        }
        return;
      }

      if (auto payload = Traits::Payload(view_->storage(), payload_offset, payload_size);
          payload.is_error()) {
        Fail("cannot extract payload view", std::move(payload.error_value()), payload_offset);
        return;
      } else {
        value_.payload = std::move(payload.value());
      }
      offset_ = next_item_offset;
    }

    void Fail(std::string_view sv, std::optional<storage_error_type> storage_error = std::nullopt,
              std::optional<uint32_t> error_offset = std::nullopt) {
      view_->Fail(ContainerTraits::template ToError<Traits>(
          view_->storage(), sv, offset_, error_offset.value_or(offset_), &(*value_.header),
          std::move(storage_error)));
      *this = view_->end();
    }

    void Assert(const char* func) const {
      ZX_ASSERT_MSG(view_, "%s on default-constructed %s::iterator", func,
                    ContainerTraits::kContainerType);
      ZX_ASSERT_MSG(offset_ != kEnd_, "%s on %s::end() iterator", func,
                    ContainerTraits::kContainerType);
    }
  };

  // This returns its own error state and does not affect the `take_error()`
  // state of the Container.
  fitx::result<Error, container_header_type> container_header() {
    auto to_error = [this](
                        std::string_view reason, uint32_t error_offset = 0,
                        std::optional<storage_error_type> storage_error = std::nullopt) -> Error {
      return ContainerTraits::template ToError<Traits>(storage(), reason, 0, error_offset, nullptr,
                                                       std::move(storage_error));
    };
    auto capacity_error = Traits::Capacity(storage());
    if (capacity_error.is_error()) {
      return fitx::error{to_error("cannot determine storage capacity", 0,
                                  std::move(capacity_error).error_value())};
    }
    uint32_t capacity = capacity_error.value();

    // Minimal bounds check before trying to read.
    if (capacity < sizeof(container_header_type)) {
      return fitx::error(to_error("container header doesn't fit. Truncated?", capacity));
    }

    // Read and validate the container header.
    auto header_error = ContainerHeader();
    if (header_error.is_error()) {
      // Failed to read the container header.
      return fitx::error{
          to_error("cannot read container header", 0, std::move(header_error).error_value())};
    }

    container_header_type header = std::move(header_error).value();

    auto check_error = ContainerTraits::CheckContainerHeader(header);
    if (check_error.is_error()) {
      return fitx::error{to_error(check_error.error_value())};
    }
    const uint32_t size = ContainerTraits::ContainerSize(header);
    if (size < sizeof(header) || size > capacity) {
      return fitx::error{to_error("container doesn't fit. Truncated?")};
    }

    return fitx::ok(header);
  }

  /// After calling begin(), it's mandatory to call take_error() before
  /// destroying the Container object.  An iteration that encounters an error
  /// will simply end early, i.e. begin() or operator++() will yield an
  /// iterator that equals end().  At the end of a loop, call take_error() to
  /// check for errors.  It's also acceptable to call take_error() during an
  /// iteration that hasn't reached end() yet, but it cannot be called again
  /// before the next begin() or operator++() call.
  iterator begin() {
    StartIteration();
    auto header = container_header();
    if (header.is_error()) {
      Fail(header.error_value());
      limit_ = 0;  // Reset from past uses.
      return end();
    }
    // The container's "payload" is all the items.  Don't scan past it.
    limit_ = ContainerTraits::ContainerSize(header.value());
    return {this, false};
  }

  iterator end() { return {this, true}; }

  size_t size_bytes() {
    if (std::holds_alternative<Unused>(error_)) {
      ZX_ASSERT(limit_ == 0);

      // Taking the size before doing begin() takes extra work.
      auto capacity_error = Traits::Capacity(storage());
      if (capacity_error.is_ok()) {
        uint32_t capacity = capacity_error.value();
        if (capacity >= sizeof(container_header_type)) {
          auto header_error = ContainerHeader();
          if (header_error.is_ok()) {
            container_header_type header = std::move(header_error).value();
            const uint32_t size = ContainerTraits::ContainerSize(header);
            if (sizeof(header) <= size && size <= capacity) {
              return size;
            }
          }
        }
      }
    }
    return limit_;
  }

 protected:
  // Fetches the container header.
  fitx::result<storage_error_type,
               typename Traits::template LocalizedReadResult<container_header_type>>
  ContainerHeader() {
    return Traits::template LocalizedRead<container_header_type>(storage(), 0);
  }

  // Fetches an item header at a given offset.
  fitx::result<storage_error_type, typename Traits::template LocalizedReadResult<item_header_type>>
  ItemHeader(uint32_t offset) {
    return Traits::template LocalizedRead<item_header_type>(storage(), offset);
  }

  // Returns [offset, length] in the storage to cover the given item range.
  auto RangeBounds(const iterator& first, const iterator& last) {
    uint32_t offset = first.item_offset();
    uint32_t limit = limit_;
    if (last != end()) {
      limit = last.item_offset();
    }
    return std::make_pair(offset, limit - offset);
  }

 private:
  struct Unused {};
  struct NoError {};
  struct Taken {};
  using ErrorState = std::variant<Unused, NoError, Error, Taken>;

  void StartIteration() {
    ZX_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                  "%s iterators used without taking prior error", ContainerTraits::kContainerType);
    error_ = NoError{};
  }

  void Fail(Error error) {
    ZX_DEBUG_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                        "Fail in error state: missing %s::StartIteration() call?",
                        ContainerTraits::kContainerType);
    ZX_DEBUG_ASSERT_MSG(!std::holds_alternative<Unused>(error_),
                        "Fail in Unused: missing %s::StartIteration() call?",
                        ContainerTraits::kContainerType);
    error_ = std::move(error);
  }

  storage_type storage_;
  ErrorState error_;
  uint32_t limit_ = 0;
};

}  // namespace internal
}  // namespace zbitl

#endif  // LIB_ZBITL_INTERNAL_CONTAINER_H_
