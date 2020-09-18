// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_VIEW_H_
#define LIB_ZBITL_VIEW_H_

#include <lib/cksum.h>
#include <lib/fitx/result.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <functional>
#include <optional>
#include <type_traits>
#include <variant>

#include "checking.h"
#include "storage_traits.h"

namespace zbitl {

/// The zbitl::View template class provides an error-checking container view of
/// a ZBI.  It satisfies the C++20 std::forward_range concept; it satisfies the
/// std::view concept if the Storage and Storage::error_type types support
/// constant-time copy/move/assignment.
///
/// The "error-checking view" pattern means that the container/range/view API
/// of begin() and end() iterators is supported, but when begin() or
/// iterator::operator++() encounters an error, it simply returns end() so that
/// loops terminate normally.  Thereafter, take_error() must be called to check
/// whether the loop terminated because it iterated past the last item or
/// because it encountered an error.  Once begin() has been called,
/// take_error() must be called before the View is destroyed, so no error goes
/// undetected.  Since all use of iterators updates the error state, use of any
/// zbitl::View object must be serialized and after begin() or operator++()
/// yields end(), take_error() must be checked before using begin() again.
///
/// Each time begin() is called the underlying storage is examined afresh, so
/// it's safe to reuse a zbitl::View object after changing the data.  Reducing
/// the size of the underlying storage invalidates any iterators that pointed
/// past the new end of the image.  It's simplest just to assume that changing
/// the underlying storage always invalidates all iterators.
///
/// The Storage type is some type that can be abstractly considered to have
/// non-owning "view" semantics: it doesn't hold the storage of the ZBI, it
/// just refers to it somehow.  The zbitl::View:Error type describes errors
/// encountered while iterating.  It uses the Storage::error_type type to
/// propagate errors caused by access to the underlying storage.
///
/// Usually Storage and Storage:error_type types are small and can be copied.
/// zbitl::View is move-only if Storage is move-only or if Storage::error_type
/// is move-only.  Note that copying zbitl::View copies its error-checking
/// state exactly, so if the original View needed to be checked for errors
/// before destruction then both the original and the copy need to be checked
/// before their respective destructions.  A moved-from zbitl::View can always
/// be destroyed without checking.
template <typename Storage, Checking Check = Checking::kStrict>
class View {
 public:
  using storage_type = Storage;

  View() = default;
  View(const View&) = default;
  View& operator=(const View&) = default;

  // This is almost the same as the default move behavior.  But it also
  // explicitly resets the moved-from error state to kUnused so that the
  // moved-from View can be destroyed without checking it.
  View(View&& other)
      : error_(std::move(other.error_)), storage_(std::move(other.storage_)), limit_(other.limit_) {
    other.error_ = Unused{};
    other.limit_ = 0;
  }
  View& operator=(View&& other) {
    error_ = std::move(other.error_);
    other.error_ = Unused{};
    storage_ = std::move(other.storage_);
    limit_ = other.limit_;
    other.limit_ = 0;
    return *this;
  }

  explicit View(storage_type storage) : storage_(std::move(storage)) {}

  ~View() {
    ZX_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                  "zbitl::View destroyed after error without check");
    ZX_ASSERT_MSG(!std::holds_alternative<NoError>(error_),
                  "zbtil::View destroyed after successful iteration without check");
  }

  /// The API details of what Storage means are delegated to the StorageTraits
  /// template; see <lib/zbitl/storage_traits.h>.
  using Traits = StorageTraits<Storage>;
  static_assert(std::is_default_constructible_v<typename Traits::error_type>);
  static_assert(std::is_copy_constructible_v<typename Traits::error_type>);
  static_assert(std::is_copy_assignable_v<typename Traits::error_type>);
  static_assert(std::is_default_constructible_v<typename Traits::payload_type>);
  static_assert(std::is_copy_constructible_v<typename Traits::payload_type>);
  static_assert(std::is_copy_assignable_v<typename Traits::payload_type>);

  /// The header is represented by an opaque type that can be dereferenced as
  /// if it were `const zbi_header_t*`, i.e. `*header` or `header->member`.
  /// Either it stores the `zbi_header_t` directly or it holds a pointer into
  /// someplace owned or viewed by the Storage object.  In the latter case,
  /// i.e. when Storage represents something already in memory, `header_type`
  /// should be no larger than a plain pointer.
  class header_type {
   public:
    header_type() = default;
    header_type(const header_type&) = default;
    header_type(header_type&&) = default;
    header_type& operator=(const header_type&) = default;
    header_type& operator=(header_type&&) = default;

    /// `*header` always copies, so lifetime of `this` doesn't matter.
    zbi_header_t operator*() const {
      if constexpr (kCopy) {
        return stored_;
      } else {
        return *stored_;
      }
    }

    /// `header->member` refers to the header in place, so never do
    /// `&header->member` but always dereference a member directly.
    const zbi_header_t* operator->() const {
      if constexpr (kCopy) {
        return &stored_;
      } else {
        return stored_;
      }
    }

   private:
    using TraitsHeader = decltype(Traits::Header(std::declval<View>().storage(), 0));
    static constexpr bool kCopy =
        std::is_same_v<TraitsHeader, fitx::result<typename Traits::error_type, zbi_header_t>>;
    static constexpr bool kReference =
        std::is_same_v<TraitsHeader, fitx::result<typename Traits::error_type,
                                                  std::reference_wrapper<const zbi_header_t>>>;
    static_assert(kCopy || kReference,
                  "zbitl::StorageTraits specialization's Header function returns wrong type");

    friend View;
    using HeaderStorage = std::conditional_t<kCopy, zbi_header_t, const zbi_header_t*>;
    HeaderStorage stored_;

    // This can only be used by begin(), below.
    template <typename T>
    explicit header_type(const T& header)
        : stored_([&header]() {
            if constexpr (kCopy) {
              static_assert(std::is_same_v<zbi_header_t, T>);
              return header;
            } else {
              static_assert(std::is_same_v<std::reference_wrapper<const zbi_header_t>, T>);
              return &(header.get());
            }
          }()) {}
  };

  /// The payload type is provided by the StorageTraits specialization.  It's
  /// opaque to View, but must be default-constructible, copy-constructible,
  /// and copy-assignable.  It's expected to have "view"-style semantics,
  /// i.e. be small and not own any storage itself but only refer to storage
  /// owned by the Storage object.
  using payload_type = typename Traits::payload_type;

  /// The element type is a trivial struct morally equivalent to
  /// std::pair<header_type, payload_type>.  Both member types are
  /// default-constructible, copy-constructible, and copy-assignable, so
  /// value_type as a whole is as well.
  struct value_type {
    header_type header;
    payload_type payload;
  };

  /// The Error type is returned by take_error() after begin() or an iterator
  /// operator encountered an error.  There is always a string description of
  /// the error.  Errors arising from Storage access also provide an error
  /// value defined via StorageTraits; see <lib/zbitl/storage_traits.h>.
  struct Error {
    /// A string constant describing the error.
    std::string_view zbi_error{};

    /// This is the offset into the ZBI of the item (header) at fault.  This is
    /// zero for problems with the overall container, which begin() detects.
    /// In iterator operations, it refers to the offset into the image where
    /// the item was (or should have been).
    uint32_t item_offset = 0;

    /// This reflects the underlying error from accessing the Storage object,
    /// if any.  If storage_error.has_value() is false, then the error is in
    /// the format of the contents of the ZBI, not in accessing the contents.
    std::optional<typename Traits::error_type> storage_error{};
  };

  /// Check the container for errors after using iterators.  When begin() or
  /// iterator::operator++() encounters an error, it simply returns end() so
  /// that loops terminate normally.  Thereafter, take_error() must be called
  /// to check whether the loop terminated because it iterated past the last
  /// item or because it encountered an error.  Once begin() has been called,
  /// take_error() must be called before the View is destroyed, so no error
  /// goes undetected.  After take_error() is called the error state is
  /// consumed and take_error() cannot be called again until another begin() or
  /// iterator::operator++() call has been made.
  [[nodiscard]] fitx::result<Error> take_error() {
    ErrorState result = std::move(error_);
    error_ = Taken{};
    if (std::holds_alternative<Error>(result)) {
      return fitx::error{std::move(std::get<Error>(result))};
    }
    ZX_ASSERT_MSG(!std::holds_alternative<Taken>(result),
                  "zbitl::View::take_error() was already called");
    return fitx::ok();
  }

  /// If you explicitly don't care about any error that might have terminated
  /// the last loop early, then call ignore_error() instead of take_error().
  void ignore_error() { static_cast<void>(take_error()); }

  /// Trivial accessor for the underlying Storage (view) object.
  storage_type& storage() { return storage_; }

  class iterator {
   public:
    using difference_type = ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::input_iterator_tag;

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
      ZX_DEBUG_ASSERT(offset_ >= sizeof(zbi_header_t));
      ZX_DEBUG_ASSERT(offset_ <= view_->limit_);
      ZX_DEBUG_ASSERT(offset_ % ZBI_ALIGNMENT == 0);
      if (view_->limit_ - offset_ < sizeof(zbi_header_t)) {
        // Reached the end of the container.
        if constexpr (Check != Checking::kPermissive) {
          if (offset_ != view_->limit_) {
            Fail("container too short for next item header");
          }
        }
        *this = view_->end();
      } else if (auto header = Traits::Header(view_->storage(), offset_); header.is_error()) {
        // Failed to read the next header.
        Fail("cannot read item header", std::move(header.error_value()));
      } else if (auto header_error = CheckHeader<Check>(header.value(), view_->limit_ - offset_);
                 header_error.is_error()) {
        Fail(header_error.error_value());
      } else {
        header_ = header_type(header.value());
        offset_ += static_cast<uint32_t>(sizeof(zbi_header_t));
        if (auto payload = Traits::Payload(view_->storage(), offset_, header_->length);
            payload.is_error()) {
          Fail("cannot extract payload view", std::move(payload.error_value()));
        } else {
          offset_ += ZBI_ALIGN(header_->length);
          payload_ = std::move(payload.value());
          if constexpr (Check == Checking::kCrc) {
            if (header_->flags & ZBI_FLAG_CRC32) {
              uint32_t item_crc32 = 0;
              auto compute_crc32 = [&item_crc32](ByteView chunk) -> fitx::result<fitx::failed> {
                item_crc32 =
                    crc32(item_crc32, reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
                return fitx::ok();
              };

              // An item's CRC32 is computed as the hash of its header with its
              // crc32 field set to 0, combined with the hash of its payload.
              zbi_header_t header_without_crc32 = *header_;
              header_without_crc32.crc32 = 0;
              static_cast<void>(compute_crc32({reinterpret_cast<std::byte*>(&header_without_crc32),
                                               sizeof(header_without_crc32)}));

              if (auto result =
                      Traits::Read(view_->storage(), payload_, header_->length, compute_crc32);
                  result.is_error()) {
                Fail("cannot compute item CRC32", std::move(result.error_value()));
              } else {
                ZX_DEBUG_ASSERT(result.value().is_ok());
                if (item_crc32 != header_->crc32) {
                  Fail("item CRC32 mismatch");
                }
              }
            }
          }
        }
      }
      return *this;
    }

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    value_type operator*() const {
      Assert(__func__);
      return {header_, payload_};
    }

    uint32_t item_offset() const {
      return payload_offset() - static_cast<uint32_t>(sizeof(zbi_header_t));
    }

    uint32_t payload_offset() const {
      Assert(__func__);
      return offset_ - ZBI_ALIGN(header_->length);
    }

   private:
    // The default-constructed state is almost the same as the end() state:
    // nothing but operator==() should ever be called if view_ is nullptr.
    View* view_ = nullptr;

    // The offset into the ZBI of the next item's header.  This is 0 in
    // default-constructed iterators and kEnd_ in end() iterators, where
    // operator*() can never be called.  A valid non-end() iterator holds the
    // header and payload (references) of the "current" item for operator*() to
    // return, and its offset_ always looks past to the horizon.  If offset_ as
    // at the end of the container, then operator++() will yield end().
    uint32_t offset_ = 0;

    // end() uses a different offset_ value to distinguish a true end iterator
    // from a particular view from a default-constructed iterator from nowhere.
    static constexpr uint32_t kEnd_ = std::numeric_limits<uint32_t>::max();

    // These are left uninitialized until a successful increment sets them.
    // They are only examined by a dereference, which is invalid without
    // a successful increment.
    header_type header_{};
    payload_type payload_;

    // This is called only by begin() and end().
    friend class View;
    iterator(View* view, bool is_end)
        : view_(view), offset_(is_end ? kEnd_ : sizeof(zbi_header_t)) {
      ZX_DEBUG_ASSERT(view_);
      if (!is_end) {
        // The initial offset_ points past the container header, to the first
        // item. The first increment reaches end() or makes the iterator valid.
        ++*this;
      }
    }

    void Fail(std::string_view sv,
              std::optional<typename Traits::error_type> storage_error = std::nullopt) {
      view_->Fail({sv, offset_, std::move(storage_error)});
      *this = view_->end();
    }

    void Assert(const char* func) const {
      ZX_ASSERT_MSG(view_, "%s on default-constructed zbitl::View::iterator", func);
      ZX_ASSERT_MSG(offset_ != kEnd_, "%s on zbitl::View::end() iterator", func);
    }
  };

  // This returns its own error state and does not affect the `take_error()`
  // state of the View.
  fitx::result<Error, zbi_header_t> container_header() {
    auto capacity_error = Traits::Capacity(storage());
    if (capacity_error.is_error()) {
      return fitx::error{
          Error{"cannot determine storage capacity", 0, std::move(capacity_error.error_value())}};
    }
    uint32_t capacity = capacity_error.value();

    // Minimal bounds check before trying to read.
    if (capacity < sizeof(zbi_header_t)) {
      return fitx::error(
          Error{"storage capacity too small for ZBI container header", capacity, {}});
    }

    // Read and validate the container header.
    auto header_error = Traits::Header(storage(), 0);
    if (header_error.is_error()) {
      // Failed to read the container header.
      return fitx::error{
          Error{"cannot read container header", 0, std::move(header_error.error_value())}};
    }

    const header_type header(std::move(header_error.value()));

    auto check_error = CheckHeader<Check>(*header, capacity);
    if (check_error.is_error()) {
      return fitx::error(Error{check_error.error_value(), 0, {}});
    }

    if constexpr (Check != Checking::kPermissive) {
      if (header->flags & ZBI_FLAG_CRC32) {
        return fitx::error(Error{"container header has CRC32 flag", 0, {}});
      }
    }

    if (header->length % ZBI_ALIGNMENT != 0) {
      return fitx::error(Error{"container header has misaligned length", 0, {}});
    }

    return fitx::ok(*header);
  }

  /// After calling begin(), it's mandatory to call take_error() before
  /// destroying the View object.  An iteration that encounters an error will
  /// simply end early, i.e. begin() or operator++() will yield an iterator
  /// that equals end().  At the end of a loop, call take_error() to check for
  /// errors.  It's also acceptable to call take_error() during an iteration
  /// that hasn't reached end() yet, but it cannot be called again before the
  /// next begin() or operator++() call.

  iterator begin() {
    StartIteration();
    auto header = container_header();
    if (header.is_error()) {
      Fail(header.error_value());
      limit_ = 0;  // Reset from past uses.
      return end();
    }
    // The container's "payload" is all the items.  Don't scan past it.
    limit_ = static_cast<uint32_t>(sizeof(zbi_header_t) + header->length);
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
        if (capacity >= sizeof(zbi_header_t)) {
          auto header_error = Traits::Header(storage(), 0);
          if (header_error.is_ok()) {
            const header_type header(header_error.value());
            if (header->length <= capacity - sizeof(zbi_header_t)) {
              return sizeof(zbi_header_t) + header->length;
            }
          }
        }
      }
    }
    return limit_;
  }

  // Replace an item's header with a new one, using an iterator into this
  // view..  This never changes the existing item's length (nor its payload),
  // and always writes a header that passes Checking::kStrict.  So the header
  // can be `{.type = XYZ}` alone or whatever fields and flags matter.  Note
  // this returns only the storage error type, not an Error since no ZBI
  // format errors are possible here, only a storage failure to update.
  //
  // This method is not available if zbitl::StorageTraits<storage_type>
  // doesn't support mutation.
  template <  // SFINAE check for Traits::Write method.
      typename T = Traits,
      typename = std::void_t<decltype(
          T::Write(std::declval<std::reference_wrapper<storage_type>>().get(), 0, ByteView{}))>>
  fitx::result<typename Traits::error_type> EditHeader(const iterator& item, zbi_header_t header) {
    item.Assert(__func__);
    header = SanitizeHeader(header);
    header.length = item.header_->length;
    return Traits::Write(storage(), item.item_offset(), AsBytes(header));
  }

  // When the iterator is mutable and not a temporary, make the next
  // operator*() consistent with the new header if it worked.  For kReference
  // storage types, the change is reflected intrinsically.
  template <  // SFINAE check for Traits::Write method.
      typename T = Traits,
      typename = std::void_t<decltype(
          T::Write(std::declval<std::reference_wrapper<storage_type>>().get(), 0, ByteView{}))>>
  fitx::result<typename Traits::error_type> EditHeader(iterator& item, zbi_header_t header) {
    auto result = EditHeader(const_cast<const iterator&>(item), header);
    if constexpr (header_type::kCopy) {
      if (result.is_ok()) {
        item.header_.stored_ = header;
      }
    }
    return result;
  }

  // The does a SFINAE check for the StorageTraits<CopyStorage>::Write method
  // and yields the return type of Copy<CopyStorage>.
  template <typename CopyStorage, typename T = std::decay_t<CopyStorage>,
            typename = decltype(StorageTraits<T>::Write(
                std::declval<std::reference_wrapper<T>>().get(), 0, ByteView{}))>
  using CopyResult = fitx::result<Error, fitx::result<typename StorageTraits<T>::error_type>>;

  // Copy a range of the underlying storage into an existing piece of storage,
  // which can be any mutable type with sufficient capacity.  The Error return
  // value is for a read error.  The "success" return value indicates there was
  // no read error.  It's another fitx::result<error_type> for the writing side
  // (which may be different than the type used in Error::storage_error).  The
  // optional `to_offset` argument says where in `to` the data is written, as a
  // byte offset that is zero by default.
  template <typename CopyStorage>
  CopyResult<CopyStorage> Copy(CopyStorage&& to, uint32_t offset, uint32_t length,
                               uint32_t to_offset = 0) {
    using CopyTraits = StorageTraits<std::decay_t<CopyStorage>>;
    // TODO(mcgrathr): Support SetCapacity, check for error here.
    ZX_DEBUG_ASSERT(CopyTraits::Capacity(to) >= to_offset + length);
    auto write = [&to, to_offset](ByteView chunk) mutable  //
        -> fitx::result<typename CopyTraits::error_type> {
      if (auto result = CopyTraits::Write(to, to_offset, chunk); result.is_error()) {
        return std::move(result).take_error();
      }
      to_offset += static_cast<uint32_t>(chunk.size());
      return fitx::ok();
    };
    if (auto payload = Traits::Payload(storage(), offset, length); payload.is_error()) {
      return fitx::error{Error{
          "cannot translate ZBI offset to storage",
          offset,
          std::move(std::move(payload).error_value()),
      }};
    } else {
      auto result = Traits::Read(storage(), payload.value(), length, write);
      if (result.is_error()) {
        return fitx::error{Error{
            "cannot read from storage",
            offset,
            std::move(result).error_value(),
        }};
      }
      return std::move(result).take_value();
    }
  }

  // Returns true if "copy to new storage" overloads below are available.
  static constexpr bool CanCopyCreate() { return kCanCopyCreate; }

  // Copy a range of the underlying storage into a freshly-created new piece of
  // storage (whatever that means for this storage type).  The Error return
  // value is for a read error.  The "success" return value indicates there was
  // no read error.  It's another fitx::result<storage_error_type, T> for some
  // T akin to storage_type, possibly storage_type itself.  For example, all
  // the unowned VMO storage types yield zx::vmo as the owning equivalent
  // storage type.  If the optional `to_offset` argument is nonzero, the new
  // storage starts with that many zero bytes before the copied data.
  template <
      // SFINAE check for Traits::Create method.
      typename T = Traits, typename CreateResult = decltype(T::Create(
                               std::declval<std::reference_wrapper<storage_type>>().get(), 0))>
  fitx::result<Error, CreateResult> Copy(uint32_t offset, uint32_t length, uint32_t to_offset = 0) {
    auto copy = CopyWithSlop(offset, length, to_offset,
                             [to_offset](uint32_t slop) { return slop == to_offset; });
    if (copy.is_error()) {
      return std::move(copy).take_error();
    }
    if (copy.value().is_error()) {
      return fitx::ok(std::move(copy).value().take_error());
    }
    auto [new_storage, slop] = std::move(copy).value().value();
    ZX_DEBUG_ASSERT(slop == to_offset);
    return fitx::ok(fitx::ok(std::move(new_storage)));
  }

  // Copy a single item's payload into supplied storage.
  template <typename CopyStorage>
  CopyResult<CopyStorage> CopyRawItem(CopyStorage&& to, const iterator& it) {
    return Copy(std::forward<CopyStorage>(to), it.payload_offset(), (*it).header->length);
  }

  // Copy a single item's payload into newly-created storage.
  template <  // SFINAE check for Traits::Create method.
      typename T = Traits, typename CreateResult = decltype(T::Create(
                               std::declval<std::reference_wrapper<storage_type>>().get(), 0))>
  fitx::result<Error, CreateResult> CopyRawItem(const iterator& it) {
    return Copy(it.payload_offset(), (*it).header->length);
  }

  // Copy a single item's header and payload into supplied storage.
  template <typename CopyStorage>
  CopyResult<CopyStorage> CopyRawItemWithHeader(CopyStorage&& to, const iterator& it) {
    return Copy(std::forward<CopyStorage>(to), it.item_offset(),
                sizeof(zbi_header_t) + (*it).header->length);
  }

  // Copy a single item's header and payload into newly-created storage.
  template <  // SFINAE check for Traits::Create method.
      typename T = Traits, typename CreateResult = decltype(T::Create(
                               std::declval<std::reference_wrapper<storage_type>>().get(), 0))>
  fitx::result<Error, CreateResult> CopyRawItemWithHeader(const iterator& it) {
    return Copy(it.item_offset(), sizeof(zbi_header_t) + (*it).header->length);
  }

  // Copy the subrange `[first,last)` of the ZBI into supplied storage.
  // The storage will contain a new ZBI container with only those items.
  template <typename CopyStorage>
  CopyResult<CopyStorage> Copy(CopyStorage&& to, const iterator& first, const iterator& last) {
    using CopyTraits = StorageTraits<std::decay_t<CopyStorage>>;

    auto [offset, length] = RangeBounds(first, last);
    auto copy_result = Copy(to, offset, length, sizeof(zbi_header_t));
    if (copy_result.is_error()) {
      return std::move(copy_result).take_error();
    }
    if (copy_result.value().is_error()) {
      return std::move(copy_result).take_value();
    }
    const zbi_header_t header = ZBI_CONTAINER_HEADER(length);
    ByteView out{reinterpret_cast<const std::byte*>(&header), sizeof(header)};
    return fitx::ok(CopyTraits::Write(to, 0, out));
  }

  // Copy the subrange `[first,last)` of the ZBI into newly-created storage.
  // The storage will contain a new ZBI container with only those items.
  template <  // SFINAE check for Traits::Create method.
      typename T = Traits, typename CreateResult = decltype(T::Create(
                               std::declval<std::reference_wrapper<storage_type>>().get(), 0))>
  fitx::result<Error, CreateResult> Copy(const iterator& first, const iterator& last) {
    using CopyTraits = StorageTraits<std::decay_t<decltype(std::declval<CreateResult>().value())>>;

    auto [offset, length] = RangeBounds(first, last);
    constexpr auto slopcheck = [](uint32_t slop) {
      return slop == sizeof(zbi_header_t) || slop >= 2 * sizeof(zbi_header_t);
    };
    auto copy = CopyWithSlop(offset, length, sizeof(zbi_header_t), slopcheck);
    if (copy.is_error()) {
      return std::move(copy).take_error();
    }
    if (copy.value().is_error()) {
      return fitx::ok(std::move(copy).value().take_error());
    }
    auto [new_storage, slop] = std::move(copy).value().value();

    if (slop > sizeof(zbi_header_t)) {
      // Write out a discarded item header to take up all the slop left over
      // after the container header.
      ZX_DEBUG_ASSERT(slop >= 2 * sizeof(zbi_header_t));

      zbi_header_t hdr{};
      hdr.type = ZBI_TYPE_DISCARD;
      hdr.length = slop - (2 * sizeof(zbi_header_t));
      hdr = SanitizeHeader(hdr);
      ByteView out{reinterpret_cast<const std::byte*>(&hdr), sizeof(hdr)};
      if (auto result = CopyTraits::Write(new_storage, sizeof(zbi_header_t), out);
          result.is_error()) {
        return fitx::ok(std::move(result).take_error());
      }
      length += sizeof(zbi_header_t) + hdr.length;
    }

    // Write the new container header.
    const zbi_header_t hdr = ZBI_CONTAINER_HEADER(length);
    ByteView out{reinterpret_cast<const std::byte*>(&hdr), sizeof(hdr)};
    if (auto result = CopyTraits::Write(new_storage, 0, out); result.is_error()) {
      return fitx::ok(std::move(result).take_error());
    }

    return fitx::ok(fitx::ok(std::move(new_storage)));
  }

 private:
  struct Unused {};
  struct NoError {};
  struct Taken {};
  using ErrorState = std::variant<Unused, NoError, Error, Taken>;

  void StartIteration() {
    ZX_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                  "zbitl:View iterators used without taking prior error");
    error_ = NoError{};
  }

  void Fail(Error error) {
    ZX_DEBUG_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                        "Fail in error state: missing zbitl::View::StartIteration() call?");
    ZX_DEBUG_ASSERT_MSG(!std::holds_alternative<Unused>(error_),
                        "Fail in Unused: missing zbitl::View::StartIteration() call?");
    error_ = std::move(error);
  }

  template <
      // SFINAE check for Traits::Create method.
      typename T = Traits, typename CreateResult = decltype(T::Create(
                               std::declval<std::reference_wrapper<storage_type>>().get(), 0))>
  static constexpr bool SfinaeCreate(Unused ignored) {
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing Create method.
  static constexpr bool SfinaeCreate(...) { return false; }

  static constexpr bool kCanCopyCreate = SfinaeCreate(Unused{});

  template <
      typename SlopCheck,
      // SFINAE check for Traits::Create method.
      typename T = Traits,
      typename Result = fitx::result<
          typename T::error_type,
          std::pair<std::decay_t<decltype(
                        T::Create(std::declval<std::reference_wrapper<storage_type>>().get(), 0)
                            .value())>,
                    uint32_t>>>
  fitx::result<Error, Result> CopyWithSlop(uint32_t offset, uint32_t length, uint32_t to_offset,
                                           SlopCheck&& slopcheck) {
    if (auto result = Clone(offset, length, to_offset, std::forward<SlopCheck>(slopcheck));
        result.is_error()) {
      return fitx::error{Error{
          "cannot read from storage",
          offset,
          std::move(result).error_value(),
      }};
    } else if (result.value()) {
      // Clone did the job!
      return fitx::ok(fitx::ok(std::move(*std::move(result).value())));
    }

    // Fall back to Create and copy via Read and Write.
    if (auto result = Traits::Create(storage(), to_offset + length); result.is_error()) {
      return fitx::ok(std::move(result).take_error());
    } else {
      auto copy = std::move(result).value();
      static_assert(std::is_convertible_v<typename StorageTraits<decltype(copy)>::error_type,
                                          typename Traits::error_type>,
                    "StorageTraits::Create yields type with incompatible error_type");
      auto copy_result = Copy(copy, offset, length, to_offset);
      if (copy_result.is_error()) {  // Read error.
        return std::move(copy_result).take_error();
      }
      if (copy_result.value().is_error()) {  // Write error.
        return fitx::ok(std::move(copy_result).value().take_error());
      }
      return fitx::ok(fitx::ok(std::make_pair(std::move(copy), uint32_t{0})));
    }
  }

  template <typename SlopCheck,
            // SFINAE check for Traits::Clone method.
            typename T = Traits,
            typename Result = std::decay_t<
                decltype(T::Clone(std::declval<std::reference_wrapper<storage_type>>().get(), 0, 0,
                                  0, std::declval<SlopCheck>())
                             .value())>>
  fitx::result<typename Traits::error_type, Result> Clone(uint32_t offset, uint32_t length,
                                                          uint32_t to_offset,
                                                          SlopCheck&& slopcheck) {
    return Traits::Clone(storage(), offset, length, to_offset, std::forward<SlopCheck>(slopcheck));
  }

  // This overload is only used if SFINAE detected no Traits::Clone method.
  template <typename T = Traits,  // SFINAE check for Traits::Create method.
            typename CreateResult = std::decay_t<decltype(
                T::Create(std::declval<std::reference_wrapper<storage_type>>().get(), 0).value())>>
  fitx::result<typename Traits::error_type, std::optional<std::pair<CreateResult, uint32_t>>> Clone(
      ...) {
    return fitx::ok(std::nullopt);  // Can't do it.
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

  storage_type storage_;
  ErrorState error_;
  uint32_t limit_ = 0;
};

// Deduction guide: View v(T{}) instantiates View<T>.
template <typename Storage>
explicit View(Storage) -> View<Storage>;

// A shorthand for permissive checking.
template <typename Storage>
using PermissiveView = View<Storage, Checking::kPermissive>;

// A shorthand for CRC checking.
template <typename Storage>
using CrcCheckingView = View<Storage, Checking::kCrc>;

}  // namespace zbitl

#endif  // LIB_ZBITL_VIEW_H_
