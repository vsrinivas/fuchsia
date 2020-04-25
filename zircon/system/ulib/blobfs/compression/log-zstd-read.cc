// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log-zstd-read.h"

#include <iostream>
#include <ios>
#include <iomanip>

#include <stdio.h>

namespace blobfs {

namespace {

using std::cerr;
using std::endl;
using std::dec;
using std::hex;
using std::setfill;
using std::setw;
using std::right;

bool gLoggingEnabled = false;
constexpr size_t gLoggingBytesPerLine = 64;

}  // namespace

void EnableZSTDReadLogging() { gLoggingEnabled = true; }

void DisableZSTDReadLogging() { gLoggingEnabled = false; }

void LogZSTDRead(std::string name, uint8_t* buf, size_t byte_offset, size_t num_bytes) {
  if (!gLoggingEnabled) {
    return;
  }

  cerr << "ZSTD_READ(" << name << ") :: " << byte_offset << " " << num_bytes;

  if ((byte_offset  % gLoggingBytesPerLine) != 0) {
    cerr << endl << "ZSTD_READ(" << name << ") ";
    fprintf(stderr, "%10lu", static_cast<size_t>(0));
    cerr << " >> ";
  } else {
    cerr << endl;
  }
  for (size_t i = 0; i < (byte_offset % gLoggingBytesPerLine); i++)  {
    cerr << "  ";
  }
  for (size_t i = 0; i < num_bytes; i++) {
    if (((byte_offset + i) % gLoggingBytesPerLine) == 0) {
      cerr << endl << "ZSTD_READ(" << name << ") ";
      fprintf(stderr, "%10lu", i);
      cerr << " >> ";
    }
    fprintf(stderr, "%02X", buf[i]);
  }
  cerr << endl;
}

}  // namespace blobfs
