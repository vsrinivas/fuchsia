// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_STORAGE_TRAITS_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_STORAGE_TRAITS_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <version>

namespace zbitl {

using ByteView = cpp20::span<const std::byte>;

// The byte alignment that storage backends are expected to have.
constexpr size_t kStorageAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

template <typename T>
constexpr bool is_pod_v = std::is_trivial_v<T> && std::is_standard_layout_v<T>;

template <typename T>
constexpr bool is_uniquely_representable_pod_v =
    is_pod_v<T> && std::has_unique_object_representations_v<T>;

// It is expected that `payload` is `kStorageAlignment`-aligned in the
// following AsSpan methods (see StorageTraits below), along with `T` itself.
// This ensures that `payload` is `alignof(T)`-aligned as well, which in
// particular means that it is safe to reinterpret a `U*` as a `T*`.
template <typename T, typename U>
inline cpp20::span<T> AsSpan(U* payload, size_t len) {
  static_assert(alignof(T) <= kStorageAlignment);
  if constexpr (sizeof(U) % sizeof(T) != 0) {
    ZX_ASSERT(len * sizeof(U) % sizeof(T) == 0);
  }
  return {reinterpret_cast<T*>(payload), (len * sizeof(U)) / sizeof(T)};
}

template <typename T, typename U>
inline cpp20::span<T> AsSpan(const U& payload) {
  static_assert(is_uniquely_representable_pod_v<std::decay_t<T>>);
  if constexpr (is_pod_v<std::decay_t<U>>) {
    return AsSpan<T>(&payload, 1);
  } else {
    return AsSpan<T>(std::data(payload), std::size(payload));
  }
}

inline ByteView AsBytes(const void* payload, size_t len) {
  return {reinterpret_cast<const std::byte*>(payload), len};
}

template <typename T>
inline ByteView AsBytes(const T& payload) {
  return AsSpan<const std::byte>(payload);
}

/// The zbitl::StorageTraits template must be specialized for each type used as
/// the Storage type parameter to zbitl::View (see <lib/zbitl/view.h).  The
/// generic template can only be instantiated with `std::tuple<>` as the
/// Storage type.  This is a stub implementation that always fails with an
/// empty error_type.  It also serves to document the API for StorageTraits
/// specializations.
///
/// The underlying storage memory is expected to be
/// `kStorageAlignment`-aligned.
template <typename Storage>
struct StorageTraits {
  static_assert(std::is_same_v<std::tuple<>, Storage>, "missing StorageTraits specialization");

  /// This represents an error accessing the storage, either to read a header
  /// or to access a payload.
  struct error_type {};

  /// This represents an item payload (does not include the header).  The
  /// corresponding zbi_header_t.length gives its size.  This type is wholly
  /// opaque to zbitl::View but must be copyable.  It might be something as
  /// simple as the offset into the whole ZBI, or for in-memory Storage types a
  /// cpp20::span pointing to the contents.
  struct payload_type {};

  /// This method is expected to return a type convertible to std::string_view
  /// (e.g., std::string or const char*) representing the message associated to
  /// a given error value. The returned object is "owning" and so it is
  /// expected that the caller keep the returned object alive for as long as
  /// they use any string_view converted from it.
  static std::string_view error_string(error_type error) { return {}; }

  /// This returns the upper bound on available space where the ZBI is stored.
  /// The container must fit within this maximum.  Storage past the container's
  /// self-encoded size need not be accessible and will never be accessed.
  /// If the actual upper bound is unknown, this can safely return UINT32_MAX.
  static fit::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fit::error<error_type>{};
  }

  /// A specialization must define this if it also defines Write. This method
  /// ensures that the capacity is at least that of the provided value
  /// (possibly larger), for specializations where such an operation is
  /// sensible.
  static fit::result<error_type> EnsureCapacity(Storage& zbi, uint32_t capacity) {
    return fit::error<error_type>{};
  }

  /// This fetches the item payload view object, whatever that means for this
  /// Storage type.  This is not expected to read the contents, just transfer a
  /// pointer or offset around so they can be explicitly read later.
  static fit::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                       uint32_t length) {
    return fit::error<error_type>{};
  }

  /// Referred to as the "buffered read".
  ///
  /// This reads the payload indicated by a payload_type as returned by Payload
  /// and feeds it to the callback in chunks sized for the convenience of the
  /// storage backend.  The length is guaranteed to match that passed to
  /// Payload to fetch this payload_type value.
  ///
  /// The callback returns some type fit::result<E>.  Read returns
  /// fit::result<error_type, fit::result<E>>>, yielding a storage error or
  /// the result of the callback.  If a callback returns an error, its return
  /// value is used immediately.  If a callback returns success, another
  /// callback may be made for another chunk of the payload.  If the payload is
  /// empty (`length` == 0), there will always be a single callback made with
  /// an empty data argument.
  template <typename Callback>
  static auto Read(Storage& zbi, payload_type payload, uint32_t length, Callback&& callback)
      -> fit::result<error_type, decltype(callback(ByteView{}))> {
    return fit::error<error_type>{};
  }

  // Referred to as the "unbuffered read".
  //
  // A specialization provides this overload if the payload can be read
  // directly into a provided buffer for zero-copy operation.
  static fit::result<error_type> Read(Storage& zbi, payload_type payload, void* buffer,
                                      uint32_t length) {
    return fit::error<error_type>{};
  }

  // Referred to as the "one-shot read".
  //
  // A specialization only provides this overload if the payload can be
  // accessed directly in memory.  If this overload is provided, then the other
  // overloads need not be provided.  The returned view is only guaranteed
  // valid until the next use of the same Storage object.  So it could
  // e.g. point into a cache that's repurposed by this or other calls made
  // later using the same object.
  //
  // One may attempt to read the data out in any particular form, parameterized
  // by `T`, provided that `alignof(T) <= kStorageAlignment`. The offset
  // associated with `payload` is expected to be `alignof(T)`-aligned, though
  // that is an invariant that the caller must keep track of.
  //
  // `LowLocality` gives whether there is an expectation that adjacent data
  // will subsequently be read; if true, the amortized cost of the read might
  // be determined to be too high and storage backends might decide to perform
  // the read differently or not implement the method at all in this case.
  template <typename T, bool LowLocality>
  static std::enable_if_t<(alignof(T) <= kStorageAlignment),
                          fit::result<error_type, cpp20::span<const T>>>
  Read(Storage& zbi, payload_type payload, uint32_t length) {
    return fit::error<error_type>{};
  }

  // Referred to as the "buffered write".
  //
  // A specialization defines this only if it supports mutation.  It might be
  // called to write whole or partial headers and/or payloads, but it will
  // never be called with an offset and size that would exceed the capacity
  // previously reported by Capacity (above).  It returns success only if it
  // wrote the whole chunk specified.  If it returns an error, any subset of
  // the chunk that failed to write might be corrupted in the image and the
  // container will always revalidate everything.
  static fit::result<error_type> Write(Storage& zbi, uint32_t offset, ByteView data) {
    return fit::error<error_type>{};
  }

  // Referred to as the "unbuffered write".
  //
  // A specialization may define this if it also defines Write.  It returns a
  // pointer where the data can be mutated directly in memory.  That pointer is
  // only guaranteed valid until the next use of the same Storage object.  So
  // it could e.g. point into a cache that's repurposed by this or other calls
  // made later using the same object.
  static fit::result<error_type, void*> Write(Storage& zbi, uint32_t offset, uint32_t length) {
    return fit::error<error_type>{};
  }

  // A specialization defines this only if it supports mutation and if creating
  // new storage from whole cloth makes sense for the storage type somehow.
  // Its successful return value is whatever makes sense for returning a new,
  // owning object of a type akin to Storage (possibly Storage itself, possibly
  // another type).  The new object refers to new storage of at least the given
  // capacity (in bytes) with a provided zero-fill header size.  The old
  // storage object might be used as a prototype in some sense, but the new
  // object is distinct storage.
  static fit::result<error_type, Storage> Create(Storage& zbi, uint32_t capacity,
                                                 uint32_t initial_zero_size) {
    return fit::error<error_type>{};
  }

  // A specialization defines this only if it defines Create, and if Clone adds
  // any value.  The new object is new storage that doesn't mutate the original
  // storage, whose capacity is at least `to_offset + length`, and whose
  // contents are the subrange of the original storage starting at `offset`,
  // with zero-fill from the beginning of the storage up to `to_offset` bytes.
  // The successful return value is `std::optional<std::pair<T, uint32_t>>`
  // where T is what a successful Create call returns and the uint32_t is the
  // actual offset into the new storage, aka the "slop" (see below).  If this
  // doesn't have something more efficient to do than just allocating storage
  // space for and copying all `length` bytes of data (using Create and Write),
  // then it can just return std::nullopt.  If the method would *always* return
  // std::nullopt then it can just be omitted entirely.  The "slop" refers to
  // some number of bytes at the beginning of the storage that will read as
  // zero before the requested range of the original storage begins.  The
  // storage backend will endeavor to make this match `to_offset`, but might
  // deliver a different result due to factors like page-rounding.  The
  // `slopcheck` parameter is a `(uint32_t) -> bool` predicate function object
  // that says whether a given byte count is acceptable as slop for this clone.
  // If `slopcheck(slop)` returns false, Clone *must* return std::nullopt
  // rather than yielding storage with a rejected slop byte count.
  template <typename SlopCheck>
  static fit::result<error_type, std::optional<std::pair<Storage, uint32_t>>> Clone(
      Storage& zbi, uint32_t offset, uint32_t length, uint32_t to_offset, SlopCheck&& slopcheck) {
    static_assert(std::is_invocable_r_v<bool, SlopCheck, uint32_t>);
    return fit::error<error_type>{};
  }
};

/// ExtendedStorageTraits is a thin wrapper that provides static, constexpr,
/// convenience accessors for determining whether the traits support a
/// particular optional method. This permits the library (and similarly its
/// users) to write things like
///
/// ```
/// template<typename = std::enable_if_t<Traits::CanWrite()>, ...>
/// ```
/// template parameters to functions that only make sense in the context
/// of writable storage.
template <typename Storage>
class ExtendedStorageTraits : public StorageTraits<Storage> {
 public:
  using Base = StorageTraits<Storage>;
  using typename Base::error_type;
  using typename Base::payload_type;

  static_assert(std::is_default_constructible_v<error_type>);
  static_assert(std::is_copy_constructible_v<error_type>);
  static_assert(std::is_copy_assignable_v<error_type>);
  static_assert(std::is_default_constructible_v<payload_type>);
  static_assert(std::is_copy_constructible_v<payload_type>);
  static_assert(std::is_copy_assignable_v<payload_type>);

  // Whether Traits::Write() is defined.
  static constexpr bool CanWrite() { return SfinaeWrite(0); }

  // Whether Traits::Create() is defined.
  static constexpr bool CanCreate() { return SfinaeCreate(0); }

  // Whether the one-shot variation of Traits::Read() is defined.
  template <typename U, bool LowLocality>
  static constexpr bool CanOneShotRead() {
    return SfinaeOneShotRead<U, LowLocality>(0);
  }

  // Whether the unbuffered variation of Traits::Read() is defined.
  static constexpr bool CanUnbufferedRead() { return SfinaeUnbufferedRead(0); }

  // Whether the unbuffered variation of Traits::Write() is defined.
  static constexpr bool CanUnbufferedWrite() { return SfinaeUnbufferedWrite(0); }

  // LocalizedRead fetches a POD struct at the given offset byte offset.
  // The fetch is assumed to have low locality: that is, a small, random
  // access into the storage.  The return type can use either plain `Data` or
  // it can use `std::reference_wrapper<const Data>`.  The former case is for
  // remote access to storage, where fetching the header has to copy it.  The
  // latter case is for in-memory storage, where the header can just be
  // accessed in place via a direct pointer.
  template <typename Data, typename T = ExtendedStorageTraits>
  using LocalizedReadResult =
      std::conditional_t<T::template CanOneShotRead<Data, /*LowLocality=*/true>(),
                         std::reference_wrapper<const Data>, Data>;

  template <typename Data, typename T = ExtendedStorageTraits>
  [[gnu::always_inline]] static fit::result<error_type, LocalizedReadResult<Data, T>> LocalizedRead(
      Storage& storage, uint32_t offset) {
    static_assert(is_uniquely_representable_pod_v<Data>);

    payload_type payload;
    if (auto result = Base::Payload(storage, offset, sizeof(Data)); result.is_error()) {
      return result.take_error();
    } else {
      payload = std::move(result).value();
    }

    // `LowLocality = true`, as we are only reading a single header here.
    if constexpr (T::template CanOneShotRead<Data, /*LowLocality=*/true>()) {
      auto result = Base::template Read<Data, true>(storage, payload, sizeof(Data));
      if (result.is_error()) {
        return result.take_error();
      }
      auto data = std::move(result).value();
      ZX_DEBUG_ASSERT(data.size() == 1);  // We expect a span of one `Data`.
      return fit::ok(std::ref(data.front()));
    } else if constexpr (T::CanUnbufferedRead()) {
      Data datum;
      if (auto result = Base::Read(storage, payload, &datum, sizeof(datum)); result.is_error()) {
        return result.take_error();
      }
      return fit::ok(datum);
    } else {
      Data datum;
      size_t bytes_read = 0;
      auto read = [datum_bytes = AsSpan<std::byte>(datum)](auto bytes) mutable {
        memcpy(datum_bytes.data(), bytes.data(), bytes.size());
        datum_bytes = datum_bytes.subspan(bytes.size());
      };
      if (auto result = Base::Read(storage, payload, sizeof(datum), read); result.is_error()) {
        return result.take_error();
      }
      ZX_DEBUG_ASSERT(bytes_read == sizeof(Data));
      return fit::ok(datum);
    }
  }

  // Gives an 'example' value of a type convertible to storage& that can be
  // used within a decltype context.
  static Storage& storage_declval() {
    return std::declval<std::reference_wrapper<Storage>>().get();
  }

  static inline bool NoSlop(uint32_t slop) { return slop == 0; }

  // This is the actual object returned by a successful Traits::Create call.
  // This can be use as `typename Traits::template CreateResult<>` both to
  // get the right return type and to form a SFINAE check when used in a
  // SFINAE context like a return value, argument type, or template parameter
  // default type.
  template <typename T = Base>
  using CreateResult = std::decay_t<decltype(T::Create(storage_declval(), 0, 0).value())>;

  // This is the actual object returned by a successful Traits::Clone call.
  // This can be use as `typename Traits::template CreateResult<>` both to
  // get the right return type and to form a SFINAE check when used in a
  // SFINAE context like a return value, argument type, or template parameter
  // default type.
  template <typename T = Base>
  using CloneResult = std::decay_t<decltype(T::Clone(storage_declval(), 0, 0, 0, NoSlop).value())>;

 private:
  // SFINAE check for a Traits::Write method.
  template <typename T = Base, typename = decltype(T::Write(storage_declval(), 0, ByteView{}))>
  static constexpr bool SfinaeWrite(int ignored) {
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing Write method.
  static constexpr bool SfinaeWrite(...) { return false; }

  // SFINAE check for a Traits::Create method.
  template <typename T = Base, typename = decltype(T::Create(storage_declval(), 0, 0))>
  static constexpr bool SfinaeCreate(int ignored) {
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing Create method.
  static constexpr bool SfinaeCreate(...) { return false; }

  // SFINAE check for one-shot Traits::Read method.
  template <typename U, bool LowLocality, typename T = ExtendedStorageTraits,
            typename = decltype(T::template Read<U, LowLocality>(storage_declval(),
                                                                 std::declval<payload_type>(), 0))>
  static constexpr bool SfinaeOneShotRead(int ignored) {
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing a one-shot Read method.
  template <typename U, bool LowLocality>
  static constexpr bool SfinaeOneShotRead(...) {
    return false;
  }

  // SFINAE check for unbuffered Traits::Read method.
  template <typename T = ExtendedStorageTraits,
            typename = decltype(T::Read(storage_declval(), std::declval<payload_type>(), nullptr,
                                        0))>
  static constexpr bool SfinaeUnbufferedRead(int ignored) {
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing an unbuffered
  // Read method.
  static constexpr bool SfinaeUnbufferedRead(...) { return false; }

  // SFINAE check for an unbuffered Traits::Write method.
  template <typename T = ExtendedStorageTraits,
            typename Result = decltype(T::Write(storage_declval(), 0, 0).value())>
  static constexpr bool SfinaeUnbufferedWrite(int ignored) {
    static_assert(std::is_convertible_v<Result, void*>,
                  "Unbuffered StorageTraits::Write has the wrong signature?");
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing an unbuffered
  // Write method.
  static constexpr bool SfinaeUnbufferedWrite(...) { return false; }

  // SFINAE check for a Traits::EnsureCapacity method.
  template <typename T = Base, typename = decltype(T::EnsureCapacity(storage_declval(), 0))>
  static constexpr bool SfinaeEnsureCapacity(int ignored) {
    return true;
  }

  // This overload is chosen only if SFINAE detected a missing EnsureCapacity method.
  static constexpr bool SfinaeEnsureCapacity(...) { return false; }

  // Whether Traits::EnsureCapacity() is defined.
  static constexpr bool CanEnsureCapacity() { return SfinaeEnsureCapacity(0); }

  static_assert(!CanUnbufferedWrite() || CanWrite(),
                "If an unbuffered Write() is implemented, so too must a buffered Write() be");
  static_assert(CanEnsureCapacity() == CanWrite(),
                "Both Write() and EnsureCapacity() are expected to be implemented together");
};

// The first chunk StorageTraits<...>::Read passes to its callback must be at
// least as long as the minimum of kReadMinimum and header.length.
inline constexpr uint32_t kReadMinimum = 32;

// Specialization for std::basic_string_view<byte-size type> as Storage.  Its
// payload_type is the same type as Storage, just yielding the substring of the
// original whole-ZBI string_view.
template <typename T>
struct StorageTraits<std::basic_string_view<T>> {
  using Storage = std::basic_string_view<T>;

  static_assert(sizeof(T) == sizeof(uint8_t));

  struct error_type {};

  using payload_type = Storage;

  static std::string_view error_string(error_type error) { return {}; }

  static fit::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fit::ok(static_cast<uint32_t>(
        std::min(zbi.size(),
                 static_cast<typename Storage::size_type>(std::numeric_limits<uint32_t>::max()))));
  }

  static fit::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                       uint32_t length) {
    auto payload = zbi.substr(offset, length);
    ZX_DEBUG_ASSERT(payload.size() == length);
    return fit::ok(std::move(payload));
  }

  template <typename U, bool LowLocality>
  static std::enable_if_t<(alignof(U) <= kStorageAlignment),
                          fit::result<error_type, cpp20::span<const U>>>
  Read(Storage& zbi, payload_type payload, uint32_t length) {
    ZX_DEBUG_ASSERT(payload.size() == length);
    return fit::ok(AsSpan<const U>(payload));
  }
};

template <typename T, size_t Extent>
struct StorageTraits<cpp20::span<T, Extent>> {
  using Storage = cpp20::span<T, Extent>;

  struct error_type {};

  using payload_type = Storage;

  static std::string_view error_string(error_type error) { return {}; }

  static fit::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fit::ok(static_cast<uint32_t>(
        std::min(zbi.size_bytes(), static_cast<size_t>(std::numeric_limits<uint32_t>::max()))));
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fit::result<error_type> EnsureCapacity(Storage& zbi, uint32_t capacity_bytes) {
    if (capacity_bytes > zbi.size()) {
      return fit::error{error_type{}};
    }
    return fit::ok();
  }

  static fit::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                       uint32_t length) {
    auto payload = [&]() {
      if constexpr (std::is_const_v<T>) {
        return cpp20::as_bytes(zbi).subspan(offset, length);
      } else {
        return cpp20::as_writable_bytes(zbi).subspan(offset, length);
      }
    }();
    ZX_DEBUG_ASSERT(payload.size() == length);
    ZX_ASSERT_MSG(payload.size() % sizeof(T) == 0,
                  "payload size not a multiple of storage span element_type size");
    return fit::ok(payload_type{reinterpret_cast<T*>(payload.data()), payload.size() / sizeof(T)});
  }

  template <typename U, bool LowLocality>
  static std::enable_if_t<(alignof(U) <= kStorageAlignment),
                          fit::result<error_type, cpp20::span<const U>>>
  Read(Storage& zbi, payload_type payload, uint32_t length) {
    ZX_DEBUG_ASSERT(cpp20::as_bytes(payload).size() == length);
    return fit::ok(AsSpan<const U>(payload));
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fit::result<error_type> Write(Storage& zbi, uint32_t offset, ByteView data) {
    if (!data.empty()) {
      memcpy(Write(zbi, offset, static_cast<uint32_t>(data.size())).value(), data.data(),
             data.size());
    }
    return fit::ok();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fit::result<error_type, void*> Write(Storage& zbi, uint32_t offset, uint32_t length) {
    // The caller is supposed to maintain these invariants.
    ZX_DEBUG_ASSERT(offset <= zbi.size_bytes());
    ZX_DEBUG_ASSERT(length <= zbi.size_bytes() - offset);
    return fit::ok(cpp20::as_writable_bytes(zbi).data() + offset);
  }
};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_STORAGE_TRAITS_H_
