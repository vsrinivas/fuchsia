// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <ctype.h>
#include <debug.h>
#include <lib/debuglog.h>
#include <lib/io.h>
#include <platform.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <platform/debug.h>
#include <vm/vm.h>

static SpinLock dputc_spin_lock;

void serial_write(ktl::string_view str) {
  AutoSpinLock guard(&dputc_spin_lock);

  // Write out the serial port.
  platform_dputs_irq(str.data(), str.size());
}

static SpinLock print_spin_lock;
static fbl::DoublyLinkedList<PrintCallback*> print_callbacks TA_GUARDED(print_spin_lock);

void console_write(ktl::string_view str) {
  // Print to any registered console loggers.
  AutoSpinLock guard(&print_spin_lock);

  for (PrintCallback& print_callback : print_callbacks) {
    print_callback.Print(str);
  }
}

static void stdout_write(ktl::string_view str) {
  if (dlog_bypass() == false) {
    if (dlog_write(DEBUGLOG_INFO, 0, str) == ZX_OK)
      return;
  }
  console_write(str);
  serial_write(str);
}

static void stdout_write_buffered(ktl::string_view str) {
  Thread* t = Thread::Current::Get();

  if (unlikely(t == nullptr)) {
    stdout_write(str);
    return;
  }

  t->linebuffer().Write(str);
}

void Linebuffer::Write(ktl::string_view str) {
  // Look for corruption and don't continue.
  if (unlikely(!is_kernel_address((uintptr_t)buffer_.data()) || pos_ >= buffer_.size())) {
    stdout_write("<linebuffer corruption>\n"sv);
    return;
  }

  while (!str.empty()) {
    size_t remaining = buffer_.size() - pos_;
    auto substring = str.substr(0, remaining);
    size_t newline_pos = substring.find_first_of('\n');

    size_t size;
    bool inject;
    bool flush;
    if (newline_pos != substring.npos) {
      // A newline that fits in our remaining buffer.
      size = newline_pos + 1;
      inject = false;
      flush = true;
    } else if (substring.size() == remaining) {
      // We fill the buffer, injecting a newline.
      size = remaining - 1;
      inject = true;
      flush = true;
    } else {
      // We only add to the buffer.
      size = substring.size();
      inject = false;
      flush = false;
    }

    memcpy(&buffer_[pos_], substring.data(), size);
    str.remove_prefix(size);
    pos_ += size;

    if (inject) {
      buffer_[pos_] = '\n';
      pos_ += 1;
    }
    if (flush) {
      stdout_write({buffer_.data(), pos_});
      pos_ = 0;
    }
  }
}

void register_print_callback(PrintCallback* cb) {
  AutoSpinLock guard(&print_spin_lock);

  print_callbacks.push_front(cb);
}

void unregister_print_callback(PrintCallback* cb) {
  AutoSpinLock guard(&print_spin_lock);

  print_callbacks.erase(*cb);
}

// This is what printf calls.  Really this could and should be const.
// But all the stdio function signatures require non-const `FILE*`.
FILE FILE::stdout_{[](void*, ktl::string_view str) {
                     stdout_write_buffered(str);
                     return static_cast<int>(str.size());
                   },
                   nullptr};

FILE gConsoleFile{[](void*, ktl::string_view str) {
                    console_write(str);
                    return static_cast<int>(str.size());
                  },
                  nullptr};

FILE gSerialFile{[](void*, ktl::string_view str) {
                   serial_write(str);
                   return static_cast<int>(str.size());
                 },
                 nullptr};
