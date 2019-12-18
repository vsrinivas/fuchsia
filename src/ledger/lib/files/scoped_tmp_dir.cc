// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/scoped_tmp_dir.h"

#include <sys/stat.h>

#include "src/ledger/lib/files/directory.h"
#include "src/ledger/lib/files/path.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace {

std::string GetGlobalTmpDir() {
  const char* env_var = getenv("TMPDIR");
  return env_var ? std::string(env_var) : "/tmp";
}

// Fills the first 6 bytes of |tp| with random characters suitable for the file
// system. The implementation is taken from __randname.c in //zircon
void GenerateRandName(char* tp) {
  LEDGER_DCHECK(strlen(tp) >= 6);

  struct timespec ts;
  unsigned long r;

  clock_gettime(CLOCK_REALTIME, &ts);
  r = ts.tv_nsec * 65537 ^
      (reinterpret_cast<uintptr_t>(&ts) / 16 + reinterpret_cast<uintptr_t>(tp));
  for (size_t i = 0; i < 6; i++, r >>= 5) {
    tp[i] = 'A' + (r & 15) + (r & 16) * 2;
  }
}

// Creates a unique temporary directory under |root_fd| from template |tp|.
char* MkdTempAt(int root_fd, char* tp, size_t tp_length) {
  LEDGER_DCHECK(strlen(tp) == tp_length);
  LEDGER_DCHECK(tp_length >= 6);
  LEDGER_DCHECK(memcmp(tp + tp_length - 6, "XXXXXX", 6) == 0);
  int retries = 100;
  do {
    GenerateRandName(tp + tp_length - 6);
    if (mkdirat(root_fd, tp, 0700) == 0) {
      return tp;
    }
  } while (--retries && errno == EEXIST);

  memcpy(tp + tp_length - 6, "XXXXXX", 6);
  return nullptr;
}
}  // namespace

ScopedTmpDir::ScopedTmpDir() : ScopedTmpDir(DetachedPath(AT_FDCWD, GetGlobalTmpDir())) {}

ScopedTmpDir::ScopedTmpDir(DetachedPath parent_path) {
  // MkdTempAt replaces "XXXXXX" so that the resulting directory path is unique.
  std::string directory_path = parent_path.path() + "/temp_dir_XXXXXX";
  if (!CreateDirectoryAt(parent_path.root_fd(), parent_path.path()) ||
      !MkdTempAt(parent_path.root_fd(), &directory_path[0], directory_path.size())) {
    directory_path = "";
  }
  path_ = DetachedPath(parent_path.root_fd(), directory_path);
}

ScopedTmpDir::~ScopedTmpDir() {
  if (path_.path().size()) {
    if (!DeletePathAt(path_.root_fd(), path_.path(), true)) {
      LEDGER_LOG(WARNING) << "Unable to delete: " << path_.path();
    }
  }
}

}  // namespace ledger
