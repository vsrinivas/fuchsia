// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_
#define ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_

#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <optional>
#include <string_view>

#include <fbl/function.h>

// Cmdline is used to build and access the kernel command line.
//
// The underlying data is stored as a sequence of zero or more C strings followed by a final \0
// (i.e. an empty string).
//
// It can be accessed using the Get* methods or via data() and size().
//
// The Get* methods treat later values as overrides for earlier ones.
//
// For example, an empty command line is [\0], and a command line containing "a=b" is [a=b\0\0].
class Cmdline {
 public:
  static constexpr uint32_t kCmdlineMax = 4096;

  // Append |str| to the command line.
  //
  // |str| should contain "key=value" elements, separated by spaces.  Repeated spaces in |str| will
  // be combined.  Invalid characters will be replaced with '.'.
  //
  // For example:
  //
  //   Append("key=value  red foo=bar\n");
  //
  // will result in [key=value\0red=\0foo=bar.\0\0]
  //
  // Append may be called repeatedly. If |kCmdlineMax| is exceeded, will panic.
  //
  // The command line will always be properly terminated.
  void Append(const char* str);

  // Return the last value for |key| or nullptr if not found.
  //
  // When |key| is nullptr, the entire command line is returned.
  const char* GetString(const char* key) const;

  // Return the last value for |key| or |default_value| if not found.
  //
  // "0", "false", and "off" are considered false.  All other values are considered true.
  bool GetBool(const char* key, bool default_value) const;

  // Return the last value for |key| or |default_value| if not found.
  uint32_t GetUInt32(const char* key, uint32_t default_value) const;

  // Return the last value for |key| or |default_value| if not found.
  uint64_t GetUInt64(const char* key, uint64_t default_value) const;

  // Process and issue callbacks for the reserved RAM entries of the kernel
  // command line, fixing up the entries in response to the results of the
  // callback.
  //
  // A kernel command line may include commands to reserve sections of
  // contiguous physical RAM, usually for testing purposes.  Reserved sections
  // will be contiguous in physical RAM, off limits to the PMM allocator, and
  // accessible by usermode software with access to the root resource or an MMIO
  // resource with appropriate range.  The commands take the following form.
  //
  // kernel.ram.reserve.<name>=<size>,0xXXXXXXXXXXXXXXXX
  //
  // Note the "0xXXXXXXXXXXXXXXXX".  This is a placeholder for a dynamically
  // allocated address and needs to be replicated exactly so that the kernel has
  // a place to publish the physical address of the reservation to usermode.
  //
  // To assist in processing these regions, the Cmdline class provides the
  // ProcessRamReservions method.  This method will attempt to find all of the
  // requested reservation pairs in the system and call the user supplied
  // callback for each.  If the reservation fails for any reason, the method
  // will erase the entry in the cmd line image, replacing it with
  // "." characters instead.  If the reservation is successful, the method will
  // update the base address placeholder with physical address which was reserved.
  //
  // Users must supply a callback function/lambda to the ProcessRamReservations
  // call.  The size and name of each valid reservation will be supplied to the
  // callback, which must return the physical address of the successful
  // reservation, or std::nullopt in the case that the reservation fails for any
  // reason.
  using ProcessRamReservationsCbk =
      fbl::InlineFunction<std::optional<uintptr_t>(size_t size, std::string_view name),
                          sizeof(void*)>;
  void ProcessRamReservations(const ProcessRamReservationsCbk& cbk);

  // read-only  access to the underlying data
  const char* data() const { return data_; }

  // Return the size of data() including the final \0.
  //
  // Guaranteed to be >= 1;
  size_t size() const;

 protected:
  // Adds the given character to data_ and updates length_. If the character would cause the buffer
  // to exceed kCmdlineMax, panic.
  void AddOrAbort(char c);

  // Find the key of the explicitly specified length in our list of command line
  // arguments.
  const char* FindKey(const char* key, size_t key_len) const;

  // Zero-initialize to ensure the |gCmdline| instance of this class lives in the BSS rather than
  // DATA segment so we don't bloat the kernel.
  char data_[kCmdlineMax]{};
  // Does not include the final \0.
  size_t length_{};
};

extern Cmdline gCmdline;

#endif  // ZIRCON_KERNEL_LIB_CMDLINE_INCLUDE_LIB_CMDLINE_H_
