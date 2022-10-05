// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_ITEMS_BOOTFS_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_ITEMS_BOOTFS_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zbitl/storage-traits.h>
#include <zircon/assert.h>
#include <zircon/boot/bootfs.h>

#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <variant>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>

namespace zbitl {

/// BootfsView gives a storage-abstracted, "error-checking view" into a BOOTFS
/// payload. Its semantics are nearly identical to that of View: see
/// <lib/zbitl/view.h> for more detail.
template <typename Storage>
class BootfsView {
 public:
  using Traits = ExtendedStorageTraits<Storage>;
  using storage_type = Storage;

  struct Error {
    /// `storage_error_string` gives a redirect to ErrorTraits's static
    /// `error_string` method for stringifying storage errors; this is used to
    /// stringify the entirety of Error in contexts where the associated traits
    /// are not known or accessible.
    static constexpr auto storage_error_string = &Traits::error_string;

    /// A string constant describing the error.
    std::string_view reason{};

    /// The name of the file associated with the error, empty if the error lies
    /// with the overall BOOTFS directory.
    std::string_view filename{};

    /// This reflects the underlying error from accessing the Storage object,
    /// if any.  If storage_error.has_value() is false, then the error is in
    /// the format of the contents of the BOOTFS, not in accessing the
    /// contents.
    std::optional<typename Traits::error_type> storage_error{};

    /// The offset into storage to the directory entry header at which this
    /// error occurred - or zero when the error lies with the overall BOOTFS
    /// directory.
    uint32_t entry_offset = 0;
  };

  /// Represents a BOOTFS "file" entry.
  struct File {
    /// The name of the file.
    std::string_view name;

    /// The content of the file, as represented by the storage payload type.
    typename Traits::payload_type data;

    /// The offset into storage at which the file content is found.
    uint32_t offset;

    /// The size of the file contents.
    uint32_t size;
  };

  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using reference = BootfsView::File&;
    using value_type = BootfsView::File;
    using pointer = BootfsView::File*;
    using difference_type = ptrdiff_t;

    iterator() = default;

    bool operator==(const iterator& other) const {
      return other.bootfs_ == bootfs_ && other.offset_ == offset_;
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

    iterator& operator++() {  // prefix
      Assert(__func__);
      bootfs_->StartIteration();
      // Add the NUL-terminator back to the name size when calculating dirent
      // size.
      uint32_t next_offset =
          offset_ + static_cast<uint32_t>(ZBI_BOOTFS_DIRENT_SIZE(value_.name.size() + 1));
      Update(next_offset);
      return *this;
    }

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    const BootfsView::File& operator*() const {
      Assert(__func__);
      return value_;
    }

    const BootfsView::File* operator->() const {
      Assert(__func__);
      return &value_;
    }

    BootfsView& view() const {
      ZX_ASSERT_MSG(bootfs_, "%s on default-constructed zbitl::BootfsView::iterator", __func__);
      return *bootfs_;
    }

    uint32_t dirent_offset() const { return offset_; }

   private:
    // BootfsView accesses iterator's private constructor.
    friend BootfsView;

    iterator(BootfsView* bootfs, bool is_end) : bootfs_(bootfs) {
      if (is_end) {
        offset_ = bootfs_->dir_end_offset();
      } else {
        Update(sizeof(zbi_bootfs_header_t));
      }
    }

    // Updates the state of the iterator to point to a new directory entry.
    void Update(uint32_t dirent_offset) {
      using namespace std::literals;

      ZX_DEBUG_ASSERT(dirent_offset >= sizeof(zbi_bootfs_header_t));

      if (dirent_offset == bootfs_->dir_end_offset()) {
        *this = bootfs_->end();
        return;
      }

      constexpr std::string_view kErrEntryExceedsDir = "entry exceeds directory block";
      if (dirent_offset > bootfs_->dir_end_offset() ||
          sizeof(zbi_bootfs_dirent_t) > bootfs_->dir_end_offset() - dirent_offset) {
        Fail(kErrEntryExceedsDir);
        return;
      }

      uint32_t offset_into_dir = dirent_offset - uint32_t{sizeof(zbi_bootfs_header_t)};
      const auto* dirent =
          reinterpret_cast<const zbi_bootfs_dirent_t*>(&(bootfs_->dir_[offset_into_dir]));

      if (ZBI_BOOTFS_DIRENT_SIZE(dirent->name_len) > bootfs_->dir_end_offset() - dirent_offset) {
        Fail(kErrEntryExceedsDir);
        return;
      }

      std::string_view filename{dirent->name, dirent->name_len};
      if (filename.empty()) {
        Fail("no filename is present"sv);
        return;
      }
      if (filename.size() > ZBI_BOOTFS_MAX_NAME_LEN) {
        Fail("filename is too long; exceeds ZBI_BOOTFS_MAX_NAME_LEN"sv);
        return;
      }
      if (filename.front() == '/') {
        Fail("filename cannot begin with \'/\'");
        return;
      }
      if (filename.back() != '\0') {
        Fail("filename must end with a NUL-terminator");
        return;
      }
      filename.remove_suffix(1);  // Eat '\0'.

      if (dirent->data_off % ZBI_BOOTFS_PAGE_SIZE) {
        Fail("file offset is not a multiple of ZBI_BOOTFS_PAGE_SIZE"sv, filename);
        return;
      }

      uint32_t aligned_data_len = ZBI_BOOTFS_PAGE_ALIGN(dirent->data_len);
      if (dirent->data_off > bootfs_->capacity_ || dirent->data_len > aligned_data_len ||
          aligned_data_len > bootfs_->capacity_ - dirent->data_off) {
        Fail("file exceeds storage capacity", filename);
        return;
      }

      if (auto result = Traits::Payload(bootfs_->storage(), dirent->data_off, dirent->data_len);
          result.is_error()) {
        Fail("cannot extract payload view", filename, std::move(result.error_value()));
        return;
      } else {
        value_ = {
            .name = filename,
            .data = std::move(result).value(),
            .offset = dirent->data_off,
            .size = dirent->data_len,
        };
      }

      offset_ = dirent_offset;
    }

    void Fail(std::string_view reason, std::string_view filename = {},
              std::optional<typename Traits::error_type> storage_error = std::nullopt) {
      bootfs_->Fail({
          .reason = reason,
          .filename = filename,
          .storage_error = std::move(storage_error),
          .entry_offset = offset_,
      });
      *this = bootfs_->end();
    }

    void Assert(const char* func) const {
      ZX_ASSERT_MSG(bootfs_, "%s on default-constructed zbitl::BootfsView::iterator", func);
      ZX_ASSERT_MSG(offset_ != bootfs_->dir_end_offset(), "%s on zbitl::BootfsView::end() iterator",
                    func);
    }

    // A pointer to the associated BootfsView, null only if default-
    // constructed.
    BootfsView* bootfs_ = nullptr;

    // The offset into the storage of the associated directory entry.
    uint32_t offset_ = 0;

    // This is left uninitialized until a successful increment sets it.
    // It is only examined by a dereference, which is invalid without a
    // successful increment.
    File value_;
  };

  BootfsView() = default;

  /// Move-only.
  BootfsView(const BootfsView&) = delete;
  BootfsView& operator=(const BootfsView&) = delete;

  /// This is almost the same as the default move behavior.  But it also
  /// explicitly resets the moved-from error state to kUnused so that the
  /// moved-from BootfsView can be destroyed without checking it.
  BootfsView(BootfsView&& other) noexcept { *this = std::move(other); }

  BootfsView& operator=(BootfsView&& other) noexcept {
    storage_ = std::move(other.storage_);
    other.storage_ = storage_type{};
    error_ = std::move(other.error_);
    other.error_ = Unused{};
    dir_ = std::move(other.dir_);
    other.dir_ = {};
    capacity_ = other.capacity_;
    other.capacity_ = 0;
    return *this;
  }

  /// Initializes the BootfsView. This method must be called only once and done
  /// so before calling other methods.
  static fit::result<Error, BootfsView> Create(storage_type storage) {
    using namespace std::literals;

    constexpr auto to_error = [](std::string_view reason,
                                 std::optional<typename Traits::error_type> storage_error =
                                     std::nullopt) -> Error {
      return {.reason = reason, .storage_error = std::move(storage_error)};
    };

    uint32_t capacity = 0;
    if (auto result = Traits::Capacity(storage); result.is_error()) {
      return fit::error{
          to_error("cannot determine storage capacity"sv, std::move(result.error_value()))};
    } else {
      capacity = std::move(result).value();
    }

    if (capacity < sizeof(zbi_bootfs_header_t)) {
      return fit::error{to_error("storage smaller than BOOTFS header size (truncated?)"sv)};
    }

    uint32_t dirsize = 0;
    if (auto result = Traits::template LocalizedRead<zbi_bootfs_header_t>(storage, 0);
        result.is_error()) {
      return fit::error{
          to_error("failed to read BOOTFS dirsize"sv, std::move(result.error_value()))};
    } else {
      const zbi_bootfs_header_t& header = result.value();
      if (header.magic != ZBI_BOOTFS_MAGIC) {
        return fit::error{to_error("bad BOOTFS header"sv)};
      }
      dirsize = header.dirsize;
    }

    if (capacity < dirsize || capacity - dirsize < sizeof(zbi_bootfs_header_t)) {
      return fit::error{to_error("directory exceeds capacity (truncated?)")};
    }

    typename Traits::payload_type dir_payload;
    if (auto result = Traits::Payload(storage, sizeof(zbi_bootfs_header_t), dirsize);
        result.is_error()) {
      return fit::error{to_error("failed to create payload object for BOOTFS directory"sv,
                                 std::move(result.error_value()))};
    } else {
      dir_payload = std::move(result).value();
    }

    constexpr std::string_view kErrDirectoryRead = "failed to read BOOTFS directory";
    Directory dir;
    if constexpr (kCanOneShotRead) {
      auto result = Traits::template Read<std::byte, false>(storage, dir_payload, dirsize);
      if (result.is_error()) {
        return fit::error{to_error(kErrDirectoryRead, std::move(result.error_value()))};
      }
      dir = std::move(result).value();
      ZX_DEBUG_ASSERT(dir.size() == dirsize);
    } else {
      fbl::AllocChecker ac;
      dir = fbl::MakeArray<std::byte>(&ac, dirsize);
      if (!ac.check()) {
        return fit::error{to_error("failed to allocate directory: out of memory"sv)};
      }
      if constexpr (Traits::CanUnbufferedRead()) {
        if (auto result = Traits::Read(storage, dir_payload, dir.data(), dirsize);
            result.is_error()) {
          return fit::error{to_error(kErrDirectoryRead, std::move(result.error_value()))};
        }
      } else {
        size_t bytes_read = 0;
        auto read = [unwritten = AsSpan<std::byte>(storage)](auto bytes) mutable {
          memcpy(unwritten.data(), bytes.data(), bytes.size());
          unwritten = unwritten.subspan(bytes.size());
        };
        if (auto result = Traits::Read(storage, dir_payload, dirsize, read); result.is_error()) {
          return fit::error{to_error(kErrDirectoryRead, std::move(result.error_value()))};
        }
        ZX_DEBUG_ASSERT(bytes_read == dirsize);
      }
    }
    return fit::ok(BootfsView(std::move(storage), std::move(dir), capacity));
  }

  /// Check the container for errors after using iterators.  When begin() or
  /// iterator::operator++() encounters an error, it simply returns end() so
  /// that loops terminate normally.  Thereafter, take_error() must be called
  /// to check whether the loop terminated because it iterated past the last
  /// item or because it encountered an error.  Once begin() has been called,
  /// take_error() must be called before the BootfsView is destroyed, so no
  /// error goes undetected.  After take_error() is called the error state is
  /// consumed and take_error() cannot be called again until another begin() or
  /// iterator::operator++() call has been made.
  [[nodiscard]] fit::result<Error> take_error() {
    ErrorState result = std::move(error_);
    error_ = Taken{};
    if (std::holds_alternative<Error>(result)) {
      return fit::error{std::move(std::get<Error>(result))};
    }
    ZX_ASSERT_MSG(!std::holds_alternative<Taken>(result),
                  "zbitl::BootfsView::take_error() was already called");
    return fit::ok();
  }

  /// If you explicitly don't care about any error that might have terminated
  /// the last loop early, then call ignore_error() instead of take_error().
  void ignore_error() { static_cast<void>(take_error()); }

  /// Trivial accessors for the underlying Storage object.
  storage_type& storage() { return storage_; }
  const storage_type& storage() const { return storage_; }

  iterator begin() {
    StartIteration();
    // begin() == end() if there are no directory entries.
    return {this, /*is_end=*/dir_.empty()};
  }

  iterator end() { return {this, /*is_end=*/true}; }

  /// Looks up a file by a decomposition of its path. If joining the parts with
  /// separators (i.e., '/') matches the path of an entry, an iterator pointing
  /// to that entry is returned; else, end() is. The path parts are expected to
  /// be given according to directory hierarchy (so that parent directories are
  /// given first). Individual parts must be nonempty, and may contain
  /// separators themselves, but not at the beginning or end.
  ///
  /// Like begin(), find() resets the internal error state and it is the
  /// responsibility of the caller to take or ignore that error before calling
  /// this method. end() is returned if there is no match or an error occurred
  /// during iteration.
  iterator find(std::initializer_list<std::string_view> path_parts) {
    cpp20::span<const std::string_view> parts(path_parts.begin(), path_parts.end());
    for (auto it = begin(); it != end(); ++it) {
      if (HasPathParts(it->name, parts)) {
        return it;
      }
    }
    return end();
  }

  // Similar to the above, though with the whole path provided.
  iterator find(std::string_view filename) { return find({filename}); }

 private:
  static constexpr bool kCanOneShotRead = Traits::template CanOneShotRead<std::byte, false>();

  using Directory =
      std::conditional_t<kCanOneShotRead, cpp20::span<const std::byte>, fbl::Array<std::byte>>;

  struct Unused {};
  struct NoError {};
  struct Taken {};
  using ErrorState = std::variant<Unused, NoError, Error, Taken>;

  BootfsView(storage_type storage, Directory dir, uint32_t capacity)
      : storage_(std::move(storage)), dir_(std::move(dir)), capacity_(capacity) {}

  void StartIteration() {
    ZX_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                  "zbitl:BootfsView iterators used without taking prior error");
    error_ = NoError{};
  }

  void Fail(Error error) {
    ZX_DEBUG_ASSERT_MSG(!std::holds_alternative<Error>(error_),
                        "Fail in error state: missing zbitl::BootfsView::StartIteration() call?");
    ZX_DEBUG_ASSERT_MSG(!std::holds_alternative<Unused>(error_),
                        "Fail in Unused: missing zbitl::BootfsView::BootfsStartIteration() call?");
    error_ = std::move(error);
  }

  bool HasPathParts(std::string_view path, cpp20::span<const std::string_view> parts) {
    for (size_t i = 0; i < parts.size(); ++i) {
      std::string_view part = parts[i];

      ZX_ASSERT_MSG(!part.empty(), "path part may not be empty");
      ZX_ASSERT_MSG(!cpp20::starts_with(part, '/'), "path part %.*s may not begin with a '/'",
                    static_cast<int>(part.size()), part.data());
      ZX_ASSERT_MSG(!cpp20::ends_with(part, '/'), "path part %.*s may not end with a '/'",
                    static_cast<int>(part.size()), part.data());

      if (!cpp20::starts_with(path, part)) {
        return false;
      }
      path.remove_prefix(part.size());

      // Unless this is the last file part, a separator should follow.
      if (i < parts.size() - 1) {
        if (!cpp20::starts_with(path, '/')) {
          return false;
        }
        path.remove_prefix(1);
      }
    }
    return path.empty();
  }

  uint32_t dir_end_offset() const {
    return static_cast<uint32_t>(sizeof(zbi_bootfs_header_t) + dir_.size());
  }

  storage_type storage_;
  ErrorState error_;
  Directory dir_;
  uint32_t capacity_ = 0;
};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_ITEMS_BOOTFS_H_
