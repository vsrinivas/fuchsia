// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/chars-from.h>
#include <stdio.h>

#include <efi/protocol/serial-io.h>
#include <efi/protocol/simple-text-output.h>
#include <efi/system-table.h>
#include <ktl/span.h>
#include <phys/stdio.h>

#include <ktl/enforce.h>

namespace {

constexpr size_t kLineBufferSize = 256;

// The Write(Protocol*, ktl::span<char16_t>) function is passed a nonempty
// character sequence returns the number of characters written.  If that is
// zero, this is treated as an error.  If it is fewer than the number passed,
// characters will be dropped rather than looping for retransmission.
template <class Protocol>
struct Traits;

// The Simple Text Output Protocol takes a terminated char16_t string.
template <>
struct Traits<efi_simple_text_output_protocol> {
  using Char = char16_t;

  static constexpr size_t kMinLeft = 1;

  static size_t Write(efi_simple_text_output_protocol* out, ktl::span<char16_t> chars) {
    *chars.end() = L'\0';
    return out->OutputString(out, chars.data()) == EFI_SUCCESS ? chars.size() : 0;
  }
};

// The Serial I/O Protocol takes an unterminated byte span.
template <>
struct Traits<efi_serial_io_protocol> {
  using Char = uint8_t;

  static constexpr size_t kMinLeft = 0;

  static size_t Write(efi_serial_io_protocol* out, ktl::span<uint8_t> chars) {
    size_t n = chars.size_bytes();
    // The UEFI spec says this Write call always returns the result parameter
    // even in error cases.
    out->Write(out, &n, chars.data());
    return n;
  }
};

// Do CRLF conversion into a buffer and call Traits::Write to flush it.
template <typename Protocol>
int EfiStdoutWrite(void* protocol, ktl::string_view str) {
  using Char = typename Traits<Protocol>::Char;
  using CharsFrom = uart::CharsFrom<ktl::string_view, Char>;

  constexpr size_t kMinLeft = Traits<Protocol>::kMinLeft;
  Char buf[kLineBufferSize];
  ktl::span<Char> left(buf);

  int wrote = 0;
  auto flush = [protocol, &buf, &left, &wrote]() -> bool {
    ktl::span chars = ktl::span(buf).subspan(0, ktl::size(buf) - left.size());
    while (!chars.empty()) {
      size_t n = Traits<Protocol>::Write(static_cast<Protocol*>(protocol), chars);
      if (n == 0) {
        return false;
      }
      chars = chars.subspan(n);
      wrote += static_cast<int>(n);
    }
    return true;
  };

  for (const Char c : CharsFrom(str)) {
    left.front() = c;
    left = left.subspan(1);
    if (left.size() == kMinLeft && !flush()) {
      break;
    }
  }

  if ((left.size() < ktl::size(buf) - kMinLeft && !flush()) || wrote == 0) {
    return -1;
  }
  return wrote;
}

}  // namespace

void SetEfiStdout(efi_system_table* sys) {
  PhysConsole& console = PhysConsole::Get();

  if (sys->ConOut) {
    console.set_graphics(FILE{
        EfiStdoutWrite<efi_simple_text_output_protocol>,
        sys->ConOut,
    });

    // TODO(mcgrathr): in headless qemu/ovmf, ConOut is also the serial console
    // so enabling both double-prints everything.  Need a way to figure out if
    // ConOut is actually serial and skip serial if so.
  }

  void* serial_ptr;
  efi_status status = sys->BootServices->LocateProtocol(&SerialIoProtocol, nullptr, &serial_ptr);
  if (status == EFI_SUCCESS) {
    auto serial = static_cast<efi_serial_io_protocol*>(serial_ptr);
    console.set_serial(FILE{
        EfiStdoutWrite<efi_serial_io_protocol>,
        serial,
    });
  } else {
    printf("EFI: no serial console found: %#zx\n", status);
  }
}
