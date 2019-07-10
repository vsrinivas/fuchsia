// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mutex>

#include <fbl/unique_fd.h>
#include <lib/zircon-internal/thread_annotations.h>

// Helper class to record file size information into a log.
class FileSizeRecorder {
public:
    FileSizeRecorder();
    ~FileSizeRecorder();

    // Open a file to record sizes into.
    bool OpenSizeFile(const char* const path);

    // If a sizes file was opened, record that the file |name| occupied
    // |size| bytes.
    bool AppendSizeInformation(const char* const name, size_t size);

private:
    std::mutex sizes_file_lock_;
    fbl::unique_fd sizes_file_ TA_GUARDED(sizes_file_lock_);
};
