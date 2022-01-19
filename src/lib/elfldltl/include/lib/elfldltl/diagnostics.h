// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_DIAGNOSTICS_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_DIAGNOSTICS_H_

#include <zircon/assert.h>

#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace elfldltl {

// Various template APIs use a polymorphic "diagnostics object" argument.
//
// This object is responsible for reporting errors and for the policy on when
// to bail out of processing ELF data early.  All processing using this object
// is implicitly related to a single ELF file, so error details and locations
// always refer to that file.
//
// A diagnostics object must implement a few simple methods:
//
// * `bool FormatError(std::string_view error, ...)`
//
//   This is called to report a fatal error in the ELF data.  The return value
//   tells the caller whether to continue processing to the extent safely
//   possible after the error.
//
//   The first argument is a string constant with permanent extent which
//   describes the error and can be referred to indefinitely.  If there are
//   additional arguments they have one of three types:
//
//    * `size_type`, the address-sized unsigned integral type for the file.
//      This argument is a value from the file that the error complains about.
//
//    * `elfldltl::FileOffset<size_type>`
//      This argument is the offset in the ELF file, where the bad data is.
//
//    * `elfldltl::FileAddress<size_type>`
//      This argument is the address relative to the load bias of the ELF file,
//      where the bad data is.
//
//   Essentially this is an input-dependent assertion failure.  FormatError is
//   called exclusively for anomalies that can be explained only by a corrupted
//   ELF file or memory image or by a linker bug.  Processing cannot succeed
//   and no code or data from this file should be used.  The diagnostics object
//   should return true only for the purpose of logging additional errors from
//   the same file before abandoning it.  The processor may attempt additional
//   work but will only do what it can do safely without assertion failures or
//   other risks of crashing.  The bad data it has already encountered could
//   lead to a cascade of additional errors with entirely bogus details, but it
//   might be possible to get coherent reports of multiple independent errors.
//
// * `bool FormatWarning(std::string_view error, ...)`
//
//   This is like FormatError, but for issues that are less problematic.  These
//   are anomalies that probably constitute bugs in the ELF file, but plausibly
//   could be the result of build-time errors or dubious practices by the
//   programmer rather than a bug in the tools or corrupted data per se.  It's
//   probably safe enough to ignore these issues and use the file regardless.
//
// * `bool extra_checking()`
//
//   If this returns true, the processor may do some extra work that is not
//   necessary for its correct operation but just offers an opportunity to
//   notice anomalies in the ELF data and report errors or warnings that might
//   otherwise go unnoticed.  Extra checking can be avoided if the use case is
//   optimized for performance over maximal format strictness, or if the
//   diagnostics object is ignoring warnings, etc.
//

// This wraps an unsigned integral type to represent an offset in the ELF file.
template <typename size_type>
struct FileOffset {
  using value_type = size_type;

  value_type operator*() const { return offset; }

  static constexpr std::string_view kDescription = "file offset";

  value_type offset;
};

// Helper to discover if T is a FileOffset type.
template <typename T>
inline constexpr bool kIsFileOffset = false;

template <typename size_type>
inline constexpr bool kIsFileOffset<FileOffset<size_type>> = true;

// This wraps an unsigned integral type to represent an address in the ELF
// file's load image, i.e. such that the p_vaddr of the first PT_LOAD segment
// corresponds to that segment's p_offset in the file.
template <typename size_type>
struct FileAddress {
  using value_type = size_type;

  value_type operator*() const { return address; }

  static constexpr std::string_view kDescription = "file-relative address";

  value_type address;
};

// Helper to discover if T is a FileAddress type.
template <typename T>
inline constexpr bool kIsFileAddress = false;

template <typename size_type>
inline constexpr bool kIsFileAddress<FileAddress<size_type>> = true;

// These flags are used by the elfldltl::Diagnostics template implementation.
// This is the default for its template parameter.  Any other class can be used
// as long as it provides members that are contextually convertible to bool
// with these names.
struct DiagnosticsFlags {
  // If true, keep going after errors so more errors can be diagnosed.
  bool multiple_errors = false;

  // If true, then warnings are treated like errors and obey the multiple_errors
  // setting too.  If false, then always keep going after a warning.
  bool warnings_are_errors = true;

  // If true, do extra work to diagnose more errors that could be ignored.
  bool extra_checking = false;
};

// An alternative Flags type can be defined like this one to make one or more
// of the values fixed, or to change the default value of a mutable flag.
struct DiagnosticsPanicFlags {
  [[no_unique_address]] std::false_type multiple_errors;
  [[no_unique_address]] std::true_type warnings_are_errors;
  [[no_unique_address]] std::false_type extra_checking;
};

// elfldltl::Diagnostics provides a canonical implementation of a diagnostics
// object.  It wraps any callable object that takes the std::string_view and
// other arguments passed to FormatError.
//
// The Flags type can be DiagnosticsFlags or any type with those three member
// names having types convertible to bool.  The Flags object passed to the
// constructor (or default-constructed) determines the behavior.  The flags()
// method returns the Flags copy in the diagnostics object, which can then be
// changed in place.  The diagnostics object tracks the numbers of errors and
// warnings reported, unless Flags::multiple_errors is std::false_type.
//
// Convenience functions below return some canonical specializations of this.
//
template <typename Report, class Flags = DiagnosticsFlags>
class Diagnostics {
 public:
  constexpr Diagnostics(const Diagnostics&) = default;
  constexpr Diagnostics(Diagnostics&&) noexcept = default;

  explicit constexpr Diagnostics(Report report) : report_(std::move(report)) {}

  constexpr Diagnostics(Report report, Flags flags)
      : report_(std::move(report)), flags_(std::move(flags)) {}

  constexpr const Flags& flags() const { return flags_; }

  constexpr Flags& flags() { return flags_; }

  constexpr unsigned int errors() const { return errors_; }

  constexpr unsigned int warnings() const { return warnings_; }

  template <typename... Args>
  constexpr bool FormatError(std::string_view error, Args&&... args) {
    ++errors_;
    return report_(error, std::forward<Args>(args)...) && flags_.multiple_errors;
  }

  template <typename... Args>
  constexpr bool FormatWarning(std::string_view error, Args&&... args) {
    ++warnings_;
    return report_(error, std::forward<Args>(args)...) &&
           (flags_.multiple_errors || !flags_.warnings_are_errors);
  }

  // Reset the counters.
  // This doesn't do anything to the state of the Report object.
  constexpr void reset() {
    errors_ = {};
    warnings_ = {};
  }

 private:
  // This is either a wrapper around an integer, or is an empty object.
  // The tag is unused but makes the two Count types always distinct so
  // that adjacent empty members with [[no_unique_address]] can be elided.
  template <bool Counting, auto Tag>
  struct Count;

  // When counting, a trivial wrapper around an integer.
  template <auto Tag>
  struct Count<true, Tag> {
    constexpr Count& operator++() {
      ++value_;
      return *this;
    }

    constexpr operator unsigned int() const noexcept { return value_; }

    unsigned int value_ = 0;
  };

  // When not counting, increments are no-ops and the count is always one.
  template <auto Tag>
  struct Count<false, Tag> {
    constexpr Count& operator++() { return *this; }

    constexpr operator unsigned int() const noexcept { return 1; }
  };

  // If multiple_errors is actually std::false_type, then use the empty objects
  // so we don't bother to keep counts at all.
  static constexpr bool kCount =
      !std::is_same_v<decltype(std::declval<Flags>().multiple_errors), std::false_type>;

  [[no_unique_address]] Report report_;
  [[no_unique_address]] Flags flags_;
  [[no_unique_address]] Count<kCount, &Flags::multiple_errors> errors_;
  [[no_unique_address]] Count<kCount, &Flags::warnings_are_errors> warnings_;
};

// This returns a Diagnostics object that crashes immediately for any error or
// warning.  There are no library dependencies of any kind.  This behavior is
// appropriate only for self-relocation and bootstrapping cases where if there
// is anything wrong in the ELF data then something went wrong in building this
// program itself and it shouldn't be running at all.
constexpr auto TrapDiagnostics() {
  constexpr auto trap = [](auto&&... args) -> bool {
    __builtin_trap();
    return false;
  };
  return Diagnostics(trap, DiagnosticsPanicFlags());
}

// This is similar to TrapDiagnostics but it uses the <zircon/assert.h>
// ZX_PANIC call to write the message and crash, with an optional fixed prefix.
// So it has some library dependencies but might be able to generate some error
// output beofre crashing.  The argument is stored in the diagnostics object;
// it can be any type convertible to std::string_view, such as std::string.
// It's forwarded perfectly, so if passed as an lvalue reference, the reference
// will be stored rather than its referent copied.
template <typename T = std::string_view>
constexpr auto PanicDiagnostics(T&& prefix = std::string_view{}) {
  struct Report {
    bool operator()(std::string_view prefix, std::string_view error) const {
      ZX_PANIC("%.*s%.*s", static_cast<int>(prefix.size()), prefix.data(),
               static_cast<int>(error.size()), error.data());
      return false;
    }
    // TODO(mcgrathr): more overloads for value arguments.
  };
  auto panic = [prefix = std::forward<T>(prefix)](auto&&... args) {
    return Report{}(prefix, std::forward<decltype(args)>(args)...);
  };
  return Diagnostics(panic, DiagnosticsPanicFlags());
}

// This returns a Diagnostics object that simply stores a single error or
// warning message string.  It always request early bail-out for errors on the
// expectation that only one error will be reported.  But if the same object is
// indeed called again for another failure, the new error message will replace
// the old one.
template <typename T, typename... Flags>
constexpr auto OneStringDiagnostics(T& holder, Flags&&... flags) {
  auto set_error = [&holder](std::string_view error, auto&&... args) {
    holder = error;
    return false;
  };
  return Diagnostics(set_error, std::forward<Flags>(flags)...);
}

// This returns a Diagnostics object that collects a container of messages.
template <typename T, typename... Flags>
constexpr auto CollectStringsDiagnostics(T& container, Flags&&... flags) {
  auto add_error = [&container](std::string_view error, auto&&... args) {
    container.emplace_back(error);
    return true;
  };
  return Diagnostics(add_error, std::forward<Flags>(flags)...);
}

// This returns a Diagnostics object that uses << on an ostream-style object.
// Any additional arguments are passed via << as a prefix on each message.  The
// ostream should probably be in << std::hex state for the output to look good.
template <typename Ostream, class Flags = DiagnosticsFlags, typename... Args>
constexpr auto OstreamDiagnostics(Ostream& ostream, Flags&& flags = {}, Args&&... prefix) {
  auto output = [prefix = std::make_tuple(std::forward<Args>(prefix)...), &ostream](
                    std::string_view error, auto&&... args) mutable -> bool {
    std::apply([&](auto&&... prefix) { ((ostream << prefix), ...); }, prefix);
    ostream << error;
    ((ostream << args), ...);
    ostream << "\n";
    return true;
  };
  return Diagnostics(std::move(output), flags);
}

// These overloads let the object returned by OstreamDiagnostics format the
// special argument types.

template <typename S, typename T>
constexpr decltype(auto) operator<<(S&& ostream, FileOffset<T> offset) {
  return std::forward<S>(ostream) << " at file offset " << *offset;
}

template <typename S, typename T>
constexpr decltype(auto) operator<<(S&& ostream, FileAddress<T> address) {
  return std::forward<S>(ostream) << " at relative address " << *address;
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_DIAGNOSTICS_H_
