// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_VIEW_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_VIEW_H_

#include <inttypes.h>
#include <lib/cksum.h>
#include <lib/fit/result.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <type_traits>
#include <variant>

#include "checking.h"
#include "decompress.h"
#include "item.h"
#include "storage-traits.h"

namespace zbitl {

// Forward declaration; defined in image.h.
template <typename Storage>
class Image;

/// The zbitl::View class provides functionality for processing ZBI items in various
/// storage formats.
///
/// For example, the entries in a ZBI present in memory can be enumerated as follows:
///
/// ```
/// void ProcessZbiEntries(std::string_view data) {
///   // Create the view.
///   zbitl::View<std::string_view> view{data};
///
///   // Iterate over entries.
///   for (const auto& entry : view) {
///     printf("Found entry of type %x with payload size %ld.\n",
///            entry.header->type,     // entry.header has type similar to "zbi_header_t *".
///            entry.payload.size());  // entry.payload has type "std::string_view".
///   }
///
///   // Callers are required to check for errors (or call "ignore_error")
///   // prior to object destruction. See "Error checking" below.
///   if (auto error = view.take_error(); error.is_error()) {
///     printf("Error encountered!\n");
///     // ...
///   }
/// }
//  ```
///
/// zbitl::View satisfies the C++20 std::forward_range concept; it satisfies the
/// std::view concept if the Storage and Storage::error_type types support
/// constant-time copy/move/assignment.
///
/// ## Error checking
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
/// ## Iteration
///
/// Each time begin() is called the underlying storage is examined afresh, so
/// it's safe to reuse a zbitl::View object after changing the data.  Reducing
/// the size of the underlying storage invalidates any iterators that pointed
/// past the new end of the image.  It's simplest just to assume that changing
/// the underlying storage always invalidates all iterators.
///
/// ## Storage
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
template <typename Storage>
class View {
 public:
  using Traits = ExtendedStorageTraits<Storage>;
  using storage_type = Storage;

  View() = default;
  View(const View&) = default;
  View& operator=(const View&) = default;

  // This is almost the same as the default move behavior.  But it also
  // explicitly resets the moved-from error state to kUnused so that the
  // moved-from View can be destroyed without checking it.
  View(View&& other)
      : storage_(std::move(other.storage_)), error_(std::move(other.error_)), limit_(other.limit_) {
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
    using TraitsHeader = typename Traits::template LocalizedReadResult<zbi_header_t>;
    static constexpr bool kCopy = std::is_same_v<TraitsHeader, zbi_header_t>;
    static constexpr bool kReference =
        std::is_same_v<TraitsHeader, std::reference_wrapper<const zbi_header_t>>;
    static_assert(kCopy || kReference,
                  "zbitl::StorageTraits specialization's Header function returns wrong type");

    friend View;
    template <typename ImageStorage>
    friend class Image;

    using HeaderStorage = std::conditional_t<kCopy, zbi_header_t, const zbi_header_t*>;
    HeaderStorage stored_;

    // This is only used by begin(), below - and by Image's Append.
    explicit header_type(const TraitsHeader& header)
        : stored_([&header]() {
            if constexpr (kCopy) {
              return header;
            } else {
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
  /// value defined via StorageTraits; see <lib/zbitl/storage-traits.h>.
  struct Error {
    static auto storage_error_string(typename Traits::error_type error) {
      return Traits::error_string(error);
    }

    /// A string constant describing the error.
    std::string_view zbi_error{};

    /// This is the offset into the storage object at which an error occurred.
    /// This is zero for problems with the overall container, which begin()
    /// detects. In iterator operations, it refers to the offset into the image
    /// where the item header was (or should have been).
    uint32_t item_offset = 0;

    /// This reflects the underlying error from accessing the Storage object,
    /// if any.  If storage_error.has_value() is false, then the error is in
    /// the format of the contents of the ZBI, not in accessing the contents.
    std::optional<typename Traits::error_type> storage_error{};
  };

  /// An error type encompassing both read and write failures in accessing the
  /// source and destination storage objects in the context of a copy
  /// operation. In the event of a read error, we expect the write_* fields to
  /// remain unset; in the event of a write error, we expect the read_* fields
  /// to remain unset.
  template <typename CopyStorage>
  struct CopyError {
    using WriteTraits = StorageTraits<std::decay_t<CopyStorage>>;
    using WriteError = typename WriteTraits::error_type;
    using ReadError = typename Traits::error_type;

    static auto read_error_string(ReadError error) { return Traits::error_string(error); }

    static auto write_error_string(WriteError error) { return WriteTraits::error_string(error); }

    /// A string constant describing the error.
    std::string_view zbi_error{};

    /// This is the offset into the storage object at which a read error
    /// occurred. This field is expected to be unset in the case of a write
    /// error.
    uint32_t read_offset = 0;

    /// This reflects the underlying error from accessing the storage object
    /// that from which the copy was attempted. This field is expected to be
    /// std::nullopt in the case of a write error.
    std::optional<typename Traits::error_type> read_error{};

    /// This is the offset into the storage object at which a write error
    /// occurred. This field is expected to be unset in the case of a read
    /// error.
    uint32_t write_offset = 0;

    /// This reflects the underlying error from accessing the storage object
    /// that to which the copy was attempted. This field is expected to be
    /// std::nullopt in the case of a read error.
    std::optional<WriteError> write_error{};
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
  [[nodiscard]] fit::result<Error> take_error() {
    ErrorState result = std::move(error_);
    error_ = Taken{};
    if (std::holds_alternative<Error>(result)) {
      return fit::error{std::move(std::get<Error>(result))};
    }
    ZX_ASSERT_MSG(!std::holds_alternative<Taken>(result),
                  "zbitl::View::take_error() was already called");
    return fit::ok();
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

    bool operator==(const iterator& other) const {
      return other.view_ == view_ && other.offset_ == offset_;
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

    iterator& operator++() {  // prefix
      Assert(__func__);
      view_->StartIteration();
      Update(next_item_offset());
      return *this;
    }

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    const View::value_type& operator*() const {
      Assert(__func__);
      return value_;
    }

    const View::value_type* operator->() const {
      Assert(__func__);
      return &value_;
    }

    uint32_t item_offset() const { return offset_; }

    uint32_t payload_offset() const {
      Assert(__func__);
      return offset_ + sizeof(zbi_header_t);
    }

    View& view() const {
      ZX_ASSERT_MSG(view_, "%s on default-constructed zbitl::View::iterator", __func__);
      return *view_;
    }

    // Iterator traits.
    using iterator_category = std::input_iterator_tag;
    using reference = View::value_type&;
    using value_type = View::value_type;
    using pointer = View::value_type*;
    using difference_type = size_t;

   private:
    // Private fields accessed by Image<Storage>::Append().
    template <typename ImageStorage>
    friend class Image;

    uint32_t next_item_offset() const {
      return offset_ + static_cast<uint32_t>(sizeof(zbi_header_t)) +
             ZBI_ALIGN(value_.header->length);
    }

    bool next_is_end() const { return next_item_offset() == view_->size_bytes(); }

    // The default-constructed state is almost the same as the end() state:
    // nothing but operator==() should ever be called if view_ is nullptr.
    View* view_ = nullptr;

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
    friend class View;
    iterator(View* view, bool is_end) : view_(view) {
      ZX_DEBUG_ASSERT(view_);
      if (is_end) {
        offset_ = kEnd_;
      } else {
        Update(sizeof(zbi_header_t));
      }
    }

    // Updates the state of the iterator to reflect a new offset.
    void Update(uint32_t next_item_offset) {
      ZX_DEBUG_ASSERT(next_item_offset >= sizeof(zbi_header_t));
      ZX_DEBUG_ASSERT_MSG(next_item_offset <= view_->limit_,
                          "zbitl::View::iterator next_item_offset %#" PRIx32 " > limit_ %#" PRIx32,
                          next_item_offset, view_->limit_);
      ZX_DEBUG_ASSERT(next_item_offset % ZBI_ALIGNMENT == 0);

      if (next_item_offset == view_->limit_) {
        // Reached the end.
        *this = view_->end();
        return;
      }
      if (view_->limit_ < next_item_offset ||
          view_->limit_ - next_item_offset < sizeof(zbi_header_t)) {
        Fail("container too short for next item header");
        return;
      }

      if (auto header = ReadItemHeader(view_->storage(), next_item_offset); header.is_error()) {
        // Failed to read the next header.
        Fail("cannot read item header", std::move(header.error_value()));
        return;
      } else if (auto header_error = CheckItemHeader(header.value()); header_error.is_error()) {
        Fail(header_error.error_value());
        return;
      } else {
        value_.header = header_type(header.value());
      }

      const uint32_t payload_offset =
          next_item_offset + static_cast<uint32_t>(sizeof(zbi_header_t));
      const uint32_t payload_size = value_.header->length;
      const uint32_t aligned_payload_size = ZBI_ALIGN(payload_size);
      if (payload_offset > view_->limit_ ||
          aligned_payload_size < payload_size ||  // ensure aligned size didn't overflow
          aligned_payload_size > view_->limit_ - payload_offset) {
        Fail("container too short for next item payload");
        return;
      }

      if (auto payload = Traits::Payload(view_->storage(), payload_offset, value_.header->length);
          payload.is_error()) {
        Fail("cannot extract payload view", std::move(payload.error_value()), payload_offset);
        return;
      } else {
        value_.payload = std::move(payload.value());
      }
      offset_ = next_item_offset;
    }

    void Fail(std::string_view sv,
              std::optional<typename Traits::error_type> storage_error = std::nullopt,
              std::optional<uint32_t> offset = std::nullopt) {
      view_->Fail({sv, offset.value_or(offset_), std::move(storage_error)});
      *this = view_->end();
    }

    void Assert(const char* func) const {
      ZX_ASSERT_MSG(view_, "%s on default-constructed zbitl::View::iterator", func);
      ZX_ASSERT_MSG(offset_ != kEnd_, "%s on zbitl::View::end() iterator", func);
    }
  };

  // This returns its own error state and does not affect the `take_error()`
  // state of the View.
  fit::result<Error, zbi_header_t> container_header() {
    auto capacity_error = Traits::Capacity(storage());
    if (capacity_error.is_error()) {
      return fit::error{
          Error{"cannot determine storage capacity", 0, std::move(capacity_error.error_value())}};
    }
    uint32_t capacity = capacity_error.value();

    // Minimal bounds check before trying to read.
    if (capacity < sizeof(zbi_header_t)) {
      return fit::error(Error{"container header doesn't fit. Truncated?", capacity, {}});
    }

    // Read and validate the container header.
    auto header_error = ReadContainerHeader(storage());
    if (header_error.is_error()) {
      // Failed to read the container header.
      return fit::error{
          Error{"cannot read container header", 0, std::move(header_error.error_value())}};
    }

    const header_type header(std::move(header_error.value()));
    auto check_error = CheckContainerHeader(*header);
    if (check_error.is_error()) {
      return fit::error(Error{check_error.error_value(), 0, {}});
    }

    if (header->length > capacity - sizeof(*header)) {
      return fit::error{Error{"container doesn't fit. Truncated?", 0, {}}};
    }

    return fit::ok(*header);
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
    if (limit_ == 0 && std::holds_alternative<Unused>(error_)) {
      // Taking the size before doing begin() takes extra work.
      if (auto result = container_header(); result.is_ok()) {
        return result->length + sizeof(zbi_header_t);
      }
    }
    return limit_;
  }

  /// Looks up an item by type, returning the iterator pointing to the first
  /// match or else end().
  ///
  /// Like begin(), find() resets the internal error state and it is the
  /// responsibility of the caller to take or ignore that error before calling
  /// this method.
  iterator find(uint32_t type) {
    return std::find_if(begin(), end(), [type = type](const value_type& value) -> bool {
      return value.header->type == type;
    });
  }

  // Replace an item's header with a new one, using an iterator into this
  // view..  This never changes the existing item's length (nor its payload).
  // So the header can be `{.type = XYZ}` alone or whatever fields and flags
  // matter.  Note this returns only the storage error type, not an Error since
  // no ZBI format errors are possible here, only a storage failure to update.
  //
  // This method is not available if zbitl::StorageTraits<storage_type>
  // doesn't support mutation.
  template <typename T = Traits, typename = std::enable_if_t<T::CanWrite()>>
  fit::result<typename Traits::error_type> EditHeader(const iterator& item,
                                                      const zbi_header_t& header) {
    item.Assert(__func__);
    if (auto result = WriteHeader(header, item.item_offset(), item.value_.header->length);
        result.error_value()) {
      return result.take_error();
    }
    return fit::ok();
  }

  // When the iterator is mutable and not a temporary, make the next
  // operator*() consistent with the new header if it worked.  For kReference
  // storage types, the change is reflected intrinsically.
  template <typename T = Traits, typename = std::enable_if_t<T::CanWrite()>>
  fit::result<typename Traits::error_type> EditHeader(iterator& item, const zbi_header_t& header) {
    item.Assert(__func__);
    auto result = WriteHeader(header, item.item_offset(), item.value_.header->length);
    if constexpr (header_type::kCopy) {
      if (result.is_ok()) {
        item.value_.header.stored_ = result.value();
      }
    }
    if (result.is_error()) {
      return result.take_error();
    }
    return fit::ok();
  }

  // Verifies that a given View iterator points to an item with a valid CRC32.
  fit::result<Error, bool> CheckCrc32(iterator it) {
    auto [header, payload] = *it;
    if (!(header->flags & ZBI_FLAG_CRC32)) {
      return fit::ok(true);
    }

    uint32_t item_crc32 = 0;
    auto compute_crc32 = [&item_crc32](ByteView chunk) -> fit::result<fit::failed> {
      // The cumulative value in principle will not be updated by the
      // CRC32 of empty data, so do not bother with computation in
      // this case; doing so, we also sidestep any issues around how
      // `crc32()` handles the corner case of a nullptr.
      if (!chunk.empty()) {
        item_crc32 =
            crc32(item_crc32, reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
      }
      return fit::ok();
    };

    // An item's CRC32 is computed as the hash of its header with its
    // crc32 field set to 0, combined with the hash of its payload.
    zbi_header_t header_without_crc32 = *header;
    header_without_crc32.crc32 = 0;
    static_cast<void>(compute_crc32(
        {reinterpret_cast<std::byte*>(&header_without_crc32), sizeof(header_without_crc32)}));

    auto result = Read(payload, header->length, compute_crc32);
    if (result.is_error()) {
      return fit::error{Error{
          .zbi_error = "cannot compute item CRC32",
          .item_offset = it.item_offset(),
          .storage_error = std::move(result).error_value(),
      }};
    }
    ZX_DEBUG_ASSERT(result.value().is_ok());
    return fit::ok(item_crc32 == header->crc32);
  }

  // Copy a range of the underlying storage into an existing piece of storage,
  // which can be any mutable type with sufficient capacity.  The Error return
  // value is for a read error.  The "success" return value indicates there was
  // no read error.  It's another fit::result<error_type> for the writing side
  // (which may be different than the type used in Error::storage_error).  The
  // optional `to_offset` argument says where in `to` the data is written, as a
  // byte offset that is zero by default.
  template <typename CopyStorage>
  fit::result<CopyError<std::decay_t<CopyStorage>>> Copy(CopyStorage&& to, uint32_t offset,
                                                         uint32_t length, uint32_t to_offset = 0) {
    using CopyTraits = typename View<std::decay_t<CopyStorage>>::Traits;
    using ErrorType = CopyError<std::decay_t<CopyStorage>>;

    if (size_t size = size_bytes(); length > size || offset > size - length) {
      return fit::error{ErrorType{.zbi_error = "offset + length exceeds ZBI size"}};
    } else if (to_offset + length < std::max(to_offset, length)) {
      return fit::error{ErrorType{.zbi_error = "to_offset + length overflows"}};
    }

    if (auto result = CopyTraits::EnsureCapacity(to, to_offset + length); result.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot increase capacity",
          .write_offset = to_offset + length,
          .write_error = std::move(result).error_value(),
      }};
    }

    auto payload = Traits::Payload(storage(), offset, length);
    if (payload.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot translate ZBI offset to storage",
          .read_offset = offset,
          .read_error = std::move(std::move(payload).error_value()),
      }};
    }
    if constexpr (Traits::CanUnbufferedRead() && CopyTraits::CanUnbufferedWrite()) {
      // Combine buffered reading with mapped writing to do it all at once.
      auto mapped = CopyTraits::Write(to, to_offset, length);
      if (mapped.is_error()) {
        // No read error detected because a "write" error was detected first.
        return fit::error{ErrorType{
            .zbi_error = "cannot write to destination storage",
            .write_offset = to_offset,
            .write_error = std::move(mapped).error_value(),
        }};
      }
      auto result = Traits::Read(storage(), payload.value(), mapped.value(), length);
      if (result.is_error()) {
        return fit::error{ErrorType{
            .zbi_error = "cannot read from source storage",
            .read_offset = offset,
            .read_error = std::move(result).error_value(),
        }};
      }
      // No read error, no write error.
      return fit::ok();
    } else {
      auto write = [&to, to_offset](ByteView chunk) mutable  //
          -> fit::result<typename CopyTraits::error_type> {
        if (auto result = CopyTraits::Write(to, to_offset, chunk); result.is_error()) {
          return std::move(result).take_error();
        }
        to_offset += static_cast<uint32_t>(chunk.size());
        return fit::ok();
      };
      auto result = Read(payload.value(), length, write);
      if (result.is_error()) {
        return fit::error{ErrorType{
            .zbi_error = "cannot read from source storage",
            .read_offset = offset,
            .read_error = std::move(std::move(result).error_value()),
        }};
      }
      if (auto write_result = std::move(result).value(); write_result.is_error()) {
        return fit::error{ErrorType{
            .zbi_error = "cannot write to destination storage",
            .write_offset = to_offset,
            .write_error = std::move(write_result).error_value(),
        }};
      }
      return fit::ok();
    }
  }

  // Copy a range of the underlying storage into a freshly-created new piece of
  // storage (whatever that means for this storage type).  The Error return
  // value is for a read error.  The "success" return value indicates there was
  // no read error.  It's another fit::result<read_error_type, T> for some
  // T akin to storage_type, possibly storage_type itself.  For example, all
  // the unowned VMO storage types yield zx::vmo as the owning equivalent
  // storage type.  If the optional `to_offset` argument is nonzero, the new
  // storage starts with that many zero bytes before the copied data.
  template <typename T = Traits,  // SFINAE check for Traits::Create method.
            typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<CopyError<CreateStorage>, CreateStorage> Copy(uint32_t offset, uint32_t length,
                                                            uint32_t to_offset = 0) {
    auto copy = CopyWithSlop(offset, length, to_offset,
                             [to_offset](uint32_t slop) { return slop == to_offset; });
    if (copy.is_error()) {
      return std::move(copy).take_error();
    }
    auto [new_storage, slop] = std::move(copy).value();
    ZX_DEBUG_ASSERT(slop == to_offset);
    return fit::ok(std::move(new_storage));
  }

  // Copy a single item's payload into supplied storage.
  template <typename CopyStorage>
  fit::result<CopyError<std::decay_t<CopyStorage>>> CopyRawItem(CopyStorage&& to,
                                                                const iterator& it) {
    return Copy(std::forward<CopyStorage>(to), it.payload_offset(), (*it).header->length);
  }

  // Copy a single item's payload into newly-created storage.
  template <  // SFINAE check for Traits::Create method.
      typename T = Traits,
      typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<CopyError<CreateStorage>, CreateStorage> CopyRawItem(const iterator& it) {
    return Copy(it.payload_offset(), (*it).header->length);
  }

  // Copy a single item's header and payload into supplied storage.
  template <typename CopyStorage>
  fit::result<CopyError<std::decay_t<CopyStorage>>> CopyRawItemWithHeader(CopyStorage&& to,
                                                                          const iterator& it) {
    return Copy(std::forward<CopyStorage>(to), it.item_offset(),
                sizeof(zbi_header_t) + (*it).header->length);
  }

  // Copy a single item's header and payload into newly-created storage.
  template <  // SFINAE check for Traits::Create method.
      typename T = Traits,
      typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<CopyError<CreateStorage>, CreateStorage> CopyRawItemWithHeader(const iterator& it) {
    return Copy(it.item_offset(), sizeof(zbi_header_t) + (*it).header->length);
  }

  // Copy a single item's payload into supplied storage, including
  // decompressing a ZBI_TYPE_STORAGE_* item if necessary.  This statically
  // determines based on the input and output storage types whether it has to
  // use streaming decompression or can use the one-shot mode (which is more
  // efficient and requires less scratch memory).  So the unused part of the
  // decompression library can be elided at link time.
  //
  // If decompression is necessary, then this calls `scratch(size_t{bytes})` to
  // allocate scratch memory for the decompression engine.  This returns
  // `fit::result<std::string_view, T>` where T is any movable object that has
  // a `get()` method returning a pointer (of any type implicitly converted to
  // `void*`) to the scratch memory.  The returned object is destroyed after
  // decompression is finished and the scratch memory is no longer needed.
  //
  // zbitl::decompress:DefaultAllocator is a default-constructible class that
  // can serve as `scratch`.  The overloads below with fewer arguments use it.
  template <typename CopyStorage, typename ScratchAllocator>
  fit::result<CopyError<std::decay_t<CopyStorage>>> CopyStorageItem(CopyStorage&& to,
                                                                    const iterator& it,
                                                                    ScratchAllocator&& scratch) {
    if (auto compressed = IsCompressedStorage(*(*it).header)) {
      return DecompressStorage(std::forward<CopyStorage>(to), it,
                               std::forward<ScratchAllocator>(scratch));
    }
    return CopyRawItem(std::forward<CopyStorage>(to), it);
  }

  template <typename ScratchAllocator, typename T = Traits,
            typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<CopyError<CreateStorage>, CreateStorage> CopyStorageItem(const iterator& it,
                                                                       ScratchAllocator&& scratch) {
    using ErrorType = CopyError<CreateStorage>;

    if (auto compressed = IsCompressedStorage(*(*it).header)) {
      // Create new storage to decompress the payload into.
      auto to = Traits::Create(storage(), *compressed, 0);
      if (to.is_error()) {
        // No read error because a "write" error happened first.
        return fit::error{ErrorType{
            .zbi_error = "cannot create storage",
            .write_offset = 0,
            .write_error = std::move(to).error_value(),
        }};
      }
      auto to_storage = std::move(to).value();
      if (auto result = DecompressStorage(to_storage, it, std::forward<ScratchAllocator>(scratch));
          result.is_error()) {
        return result.take_error();
      }
      return fit::ok(std::move(to_storage));
    }
    return CopyRawItem(it);
  }

  // These overloads have the effect of default arguments for the allocator
  // arguments to the general versions above, but template argument deduction
  // doesn't work with default arguments.

  template <typename CopyStorage>
  fit::result<CopyError<std::decay_t<CopyStorage>>> CopyStorageItem(CopyStorage&& to,
                                                                    const iterator& it) {
    return CopyStorageItem(std::forward<CopyStorage>(to), it, decompress::DefaultAllocator);
  }

  template <typename T = Traits, typename = std::enable_if_t<T::CanCreate()>>
  auto CopyStorageItem(const iterator& it) {
    return CopyStorageItem(it, decompress::DefaultAllocator);
  }

  // Copy the subrange `[first,last)` of the ZBI into supplied storage.
  // The storage will contain a new ZBI container with only those items.
  template <typename CopyStorage>
  fit::result<CopyError<std::decay_t<CopyStorage>>> Copy(CopyStorage&& to, const iterator& first,
                                                         const iterator& last) {
    using CopyTraits = StorageTraits<std::decay_t<CopyStorage>>;
    using ErrorType = CopyError<std::decay_t<CopyStorage>>;

    auto [offset, length] = RangeBounds(first, last);
    if (auto result = Copy(to, offset, length, sizeof(zbi_header_t)); result.is_error()) {
      return std::move(result).take_error();
    }
    const zbi_header_t header = ZBI_CONTAINER_HEADER(length);
    ByteView out{reinterpret_cast<const std::byte*>(&header), sizeof(header)};
    if (auto result = CopyTraits::Write(to, 0, out); result.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot write container header",
          .write_offset = 0,
          .write_error = std::move(result).error_value(),
      }};
    }
    return fit::ok();
  }

  // Copy the subrange `[first,last)` of the ZBI into newly-created storage.
  // The storage will contain a new ZBI container with only those items.
  template <typename T = Traits,
            typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<CopyError<CreateStorage>, CreateStorage> Copy(const iterator& first,
                                                            const iterator& last) {
    using CopyTraits = StorageTraits<CreateStorage>;
    using ErrorType = CopyError<CreateStorage>;

    auto [offset, length] = RangeBounds(first, last);

    // We allow the copy to leave padding ("slop") prior to the copied objects
    // if desired. This lets some storage backends to be more efficient (e.g.,
    // VMOs can clone pages instead of copying them).
    //
    // The amount of slop must be large enough for us to insert a container
    // header and possibly an additional discard item.
    constexpr auto slopcheck = [](uint32_t slop) {
      return slop == sizeof(zbi_header_t) ||
             (slop >= 2 * sizeof(zbi_header_t) && slop % ZBI_ALIGNMENT == 0);
    };
    auto copy = CopyWithSlop(offset, length, sizeof(zbi_header_t), slopcheck);
    if (copy.is_error()) {
      return std::move(copy).take_error();
    }
    auto [new_storage, slop] = std::move(copy).value();

    if (slop > sizeof(zbi_header_t)) {
      // Write out a discarded item header to take up all the slop left over
      // after the container header.
      ZX_DEBUG_ASSERT(slop >= 2 * sizeof(zbi_header_t));

      zbi_header_t hdr{};
      hdr.type = ZBI_TYPE_DISCARD;
      hdr.length = slop - (2 * sizeof(zbi_header_t));
      hdr = SanitizeHeader(hdr);
      ByteView out{reinterpret_cast<const std::byte*>(&hdr), sizeof(hdr)};
      uint32_t to_offset = sizeof(zbi_header_t);
      if (auto result = CopyTraits::Write(new_storage, to_offset, out); result.is_error()) {
        return fit::error{ErrorType{
            .zbi_error = "cannot write discard item",
            .write_offset = to_offset,
            .write_error = std::move(result).error_value(),
        }};
      }
      length += sizeof(zbi_header_t) + hdr.length;
    }

    // Write the new container header.
    const zbi_header_t hdr = ZBI_CONTAINER_HEADER(length);
    ByteView out{reinterpret_cast<const std::byte*>(&hdr), sizeof(hdr)};
    if (auto result = CopyTraits::Write(new_storage, 0, out); result.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot write container header",
          .write_offset = 0,
          .write_error = std::move(result).error_value(),
      }};
    }

    return fit::ok(std::move(new_storage));
  }

  // This is public mostly just for tests to assert on it.
  template <typename CopyStorage = Storage>
  static constexpr bool CanZeroCopy() {
    // Reading directly into buffer has no extra copies for a receiver that can
    // do unbuffered writes.
    using CopyTraits = typename View<std::decay_t<CopyStorage>>::Traits;
    return Traits::template CanOneShotRead<std::byte, /*LowLocality=*/false>() ||
           (Traits::CanUnbufferedRead() && CopyTraits::CanUnbufferedWrite());
  }

 protected:
  // Fetches the container header.
  static fit::result<typename Traits::error_type,
                     typename Traits::template LocalizedReadResult<zbi_header_t>>
  ReadContainerHeader(Storage& storage) {
    return Traits::template LocalizedRead<zbi_header_t>(storage, 0);
  }

  // Fetches an item header at a given offset.
  static fit::result<typename Traits::error_type,
                     typename Traits::template LocalizedReadResult<zbi_header_t>>
  ReadItemHeader(Storage& storage, uint32_t offset) {
    return Traits::template LocalizedRead<zbi_header_t>(storage, offset);
  }

  // WriteHeader sanitizes and optionally updates the length of a provided
  // header, writes it to the provided offset, and returns the modified header
  // on success.
  fit::result<typename Traits::error_type, zbi_header_t> WriteHeader(
      zbi_header_t header, uint32_t offset, std::optional<uint32_t> new_length = std::nullopt) {
    header = SanitizeHeader(header);
    if (new_length.has_value()) {
      header.length = new_length.value();
    }
    if (auto result = Traits::Write(storage(), offset, AsBytes(header)); result.is_error()) {
      return fit::error{std::move(result.error_value())};
    }
    return fit::ok(header);
  }

  void set_limit(uint32_t limit) { limit_ = limit; }

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

  template <typename Callback>
  auto Read(payload_type payload, uint32_t length, Callback&& callback)
      -> fit::result<typename Traits::error_type, decltype(callback(ByteView{}))> {
    if constexpr (Traits::template CanOneShotRead<std::byte, /*LowLocality=*/false>()) {
      if (auto result = Traits::template Read<std::byte, false>(storage(), payload, length);
          result.is_error()) {
        return result.take_error();
      } else {
        return fit::ok(callback(result.value()));
      }
    } else {
      return Traits::Read(storage(), payload, length, std::forward<Callback>(callback));
    }
  }

  template <typename SlopCheck,
            // SFINAE check for Traits::Create method.
            typename T = Traits,
            typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<CopyError<CreateStorage>, std::pair<CreateStorage, uint32_t>> CopyWithSlop(
      uint32_t offset, uint32_t length, uint32_t to_offset, SlopCheck&& slopcheck) {
    using ErrorType = CopyError<CreateStorage>;

    if (size_t size = size_bytes(); length > size || offset > size - length) {
      return fit::error{ErrorType{.zbi_error = "offset + length exceeds ZBI size"}};
    } else if (to_offset + length < std::max(to_offset, length)) {
      return fit::error{ErrorType{.zbi_error = "to_offset + length overflows"}};
    }

    if (auto result = Clone(offset, length, to_offset, std::forward<SlopCheck>(slopcheck));
        result.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot read from storage",
          .read_offset = offset,
          .read_error = std::move(result).error_value(),
      }};
    } else if (result.value()) {
      // Clone did the job!
      return fit::ok(std::move(*std::move(result).value()));
    }

    // Fall back to Create and copy via Read and Write.
    if (auto result = Traits::Create(storage(), to_offset + length, to_offset); result.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot create storage",
          .read_offset = offset,
          .read_error = std::move(result).error_value(),
      }};
    } else {
      auto copy = std::move(result).value();
      static_assert(std::is_convertible_v<typename StorageTraits<decltype(copy)>::error_type,
                                          typename Traits::error_type>,
                    "StorageTraits::Create yields type with incompatible error_type");
      auto copy_result = Copy(copy, offset, length, to_offset);
      if (copy_result.is_error()) {
        return std::move(copy_result).take_error();
      }
      return fit::ok(std::make_pair(std::move(copy), uint32_t{to_offset}));
    }
  }

  template <typename SlopCheck, typename T = Traits>
  fit::result<typename Traits::error_type, typename T::template CloneResult<>> Clone(
      uint32_t offset, uint32_t length, uint32_t to_offset, SlopCheck&& slopcheck) {
    return Traits::Clone(storage(), offset, length, to_offset, std::forward<SlopCheck>(slopcheck));
  }

  // This overload is only used if SFINAE detected no Traits::Clone method.
  template <typename T = Traits,  // SFINAE check for Traits::Create method.
            typename CreateStorage = std::decay_t<typename T::template CreateResult<>>>
  fit::result<typename Traits::error_type, std::optional<std::pair<CreateStorage, uint32_t>>> Clone(
      ...) {
    return fit::ok(std::nullopt);  // Can't do it.
  }

  // Returns [offset, length] in the storage to cover the given item range.
  auto RangeBounds(const iterator& first, const iterator& last) {
    uint32_t offset = limit_;
    uint32_t limit = limit_;
    if (first != end()) {
      offset = first.item_offset();
      if (last != end()) {
        limit = last.item_offset();
      }
    }
    return std::make_pair(offset, limit - offset);
  }

  static constexpr std::optional<uint32_t> IsCompressedStorage(const zbi_header_t& header) {
    const bool compressible = TypeIsStorage(header.type);
    const bool compressed = header.flags & ZBI_FLAG_STORAGE_COMPRESSED;
    if (compressible && compressed) {
      return header.extra;
    }
    return std::nullopt;
  }

  template <typename CopyStorage, typename ScratchAllocator>
  fit::result<CopyError<std::decay_t<CopyStorage>>> DecompressStorage(CopyStorage&& to,
                                                                      const iterator& it,
                                                                      ScratchAllocator&& scratch) {
    using ErrorType = CopyError<std::decay_t<CopyStorage>>;
    using ToTraits = typename View<std::decay_t<CopyStorage>>::Traits;
    constexpr bool bufferless_output = ToTraits::CanUnbufferedWrite();

    const auto [header, payload] = *it;
    const uint32_t compressed_size = header->length;
    const uint32_t uncompressed_size = header->extra;

    if (auto result = ToTraits::EnsureCapacity(to, uncompressed_size); result.is_error()) {
      return fit::error{ErrorType{
          .zbi_error = "cannot increase capacity",
          .write_offset = uncompressed_size,
          .write_error = std::move(result).error_value(),
      }};
    }

    auto decompress_error = [&](auto&& result) {
      return fit::error{ErrorType{
          .zbi_error = result.error_value(),
          .read_offset = it.item_offset(),
      }};
    };

    constexpr std::string_view kZbiErrorCorruptedOrBadData =
        "bad or corrupted data: uncompressed length not as expected";

    if constexpr (Traits::template CanOneShotRead<std::byte, /*LowLocality=*/false>()) {
      // All the data is on hand in one shot.  Fetch it first.
      ByteView compressed_data;
      if (auto result =
              Traits::template Read<std::byte, false>(storage(), payload, compressed_size);
          result.is_error()) {
        return fit::error{ErrorType{
            .zbi_error = "cannot read compressed payload",
            .read_offset = it.item_offset(),
            .read_error = std::move(result).error_value(),
        }};
      } else {
        compressed_data = result.value();
      }

      if constexpr (bufferless_output) {
        // Decompression can write directly into the output storage in memory.
        // So this can use one-shot decompression.

        auto mapped = ToTraits::Write(to, 0, uncompressed_size);
        if (mapped.is_error()) {
          // Read succeeded, but write failed.
          return fit::error{ErrorType{
              .zbi_error = "cannot write to storage in-place",
              .write_offset = 0,
              .write_error = std::move(mapped).error_value(),
          }};
        }
        const auto uncompressed_data = static_cast<std::byte*>(mapped.value());

        auto result =
            decompress::OneShot::Decompress({uncompressed_data, uncompressed_size}, compressed_data,
                                            std::forward<ScratchAllocator>(scratch));
        if (result.is_error()) {
          return decompress_error(result);
        }
      } else {
        // Writing to the output storage requires a temporary buffer.
        auto create_result = decompress::Streaming::Create<true>(
            compressed_data, std::forward<ScratchAllocator>(scratch));
        if (create_result.is_error()) {
          return decompress_error(create_result);
        }
        auto& decompress = create_result.value();
        uint32_t outoffset = 0;
        while (!compressed_data.empty()) {
          // Decompress as much data as the decompressor wants to.
          // It updates compressed_data to remove what it's consumed.
          ByteView out;
          if (auto result = decompress(compressed_data); result.is_error()) {
            return decompress_error(result);
          } else {
            out = {result.value().data(), result.value().size()};
          }
          if (!out.empty()) {
            // Flush the output buffer to the storage.
            if (auto write = ToTraits::Write(to, outoffset, out); write.is_error()) {
              // Read succeeded, but write failed.
              return fit::error{ErrorType{
                  .zbi_error = "cannot write to storage",
                  .write_offset = outoffset,
                  .write_error = std::move(write).error_value(),
              }};
            }
            outoffset += static_cast<uint32_t>(out.size());
          }
        }

        if (outoffset != uncompressed_size) {
          return fit::error{ErrorType{.zbi_error = kZbiErrorCorruptedOrBadData}};
        }
      }
    } else {
      std::byte* outptr = nullptr;
      size_t outlen = 0;
      uint32_t outoffset = 0;
      if constexpr (bufferless_output) {
        // Decompression can write directly into the output storage in memory.
        auto mapped = ToTraits::Write(to, 0, uncompressed_size);
        if (mapped.is_error()) {
          // Read succeeded, but write failed.
          return fit::error{ErrorType{
              .zbi_error = "cannot write to storage in-place",
              .write_offset = 0,
              .write_error = std::move(mapped).error_value(),
          }};
        }

        outptr = static_cast<std::byte*>(mapped.value());
        outlen = uncompressed_size;
      }

      auto create = [&](ByteView probe) {
        return decompress::Streaming::Create<!bufferless_output>(
            probe, std::forward<ScratchAllocator>(scratch));
      };
      std::optional<std::decay_t<decltype(create({}).value())>> decompressor;

      // We have to read the first chunk just to decode its compression header.
      auto read_chunk = [&](ByteView chunk) -> fit::result<ErrorType> {
        using ChunkError = fit::error<ErrorType>;

        if (!decompressor) {
          // First chunk.  Set up the decompressor.
          if (auto result = create(chunk); result.is_error()) {
            return decompress_error(result);
          } else {
            decompressor.emplace(std::move(result).value());
          }
        }

        // Decompress the chunk.
        while (!chunk.empty()) {
          if constexpr (bufferless_output) {
            auto result = (*decompressor)({outptr, outlen}, chunk);
            if (result.is_error()) {
              return ChunkError(decompress_error(result));
            }
            outptr = result.value().data();
            outlen = result.value().size();
            outoffset += uncompressed_size - static_cast<uint32_t>(outlen);
          } else {
            ByteView out;
            if (auto result = (*decompressor)(chunk); result.is_error()) {
              return ChunkError(decompress_error(result));
            } else {
              out = {result.value().data(), result.value().size()};
            }
            if (!out.empty()) {
              // Flush the output buffer to the storage.
              auto write = ToTraits::Write(to, outoffset, out);
              if (write.is_error()) {
                // Read succeeded, but write failed.
                return ChunkError(ErrorType{
                    .zbi_error = "cannot write to storage",
                    .write_offset = outoffset,
                    .write_error = std::move(write).error_value(),
                });
              }
              outoffset += static_cast<uint32_t>(out.size());
            }
          }
        }
        return fit::ok();
      };

      auto result = Traits::Read(storage(), payload, compressed_size, read_chunk);
      if (result.is_error()) {
        return fit::error{ErrorType{
            .zbi_error = "cannot read compressed payload",
            .read_offset = it.item_offset(),
            .read_error = std::move(result).error_value(),
        }};
      }

      auto read_chunk_result = std::move(result).value();
      if (read_chunk_result.is_error()) {
        return read_chunk_result.take_error();
      }

      if (outoffset != uncompressed_size) {
        return fit::error{ErrorType{.zbi_error = kZbiErrorCorruptedOrBadData}};
      }
    }

    return fit::ok();
  }

  storage_type storage_;
  ErrorState error_;
  uint32_t limit_ = 0;
};

// Deduction guide: View v(T{}) instantiates View<T>.
template <typename Storage>
explicit View(Storage) -> View<Storage>;

// Convert a pointer to an in-memory ZBI into a Storage type.
//
// We require that `zbi` is a pointer to a valid ZBI container header followed
// by its payload. Basic magic checks on the header are performed; if they
// fail, we return a Storage spanning just the header but no payload under the
// assumption that the "length" field of the header is invalid.
//
// The template parameter `Storage` may be any storage type that can be
// constructed with arguments the arguments (const std::byte*, size_t),
// representing the start and length of the in-memory ZBI.
template <typename Storage = ByteView>
Storage StorageFromRawHeader(std::conditional_t<std::is_const_v<typename Storage::element_type>,
                                                const zbi_header_t, zbi_header_t>* zbi) {
  if (zbi->magic != ZBI_ITEM_MAGIC || zbi->type != ZBI_TYPE_CONTAINER ||
      zbi->extra != ZBI_CONTAINER_MAGIC) {
    // Invalid header. Don't trust the `length` field.
    return Storage(reinterpret_cast<typename Storage::pointer>(zbi), sizeof(zbi_header_t));
  }

  // Return Storage covering the entire header and payload.
  return Storage(reinterpret_cast<typename Storage::pointer>(zbi),
                 sizeof(zbi_header_t) + zbi->length);
}

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_VIEW_H_
