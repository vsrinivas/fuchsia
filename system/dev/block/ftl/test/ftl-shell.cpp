// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl-shell.h"

#include <memory>

bool FtlShell::Init(const ftl::VolumeOptions& options) {
    const char* error = volume_.Init(std::make_unique<NdmRamDriver>(options));
    return error ? false : true;
}

bool FtlShell::ReAttach() {
    const char* error = volume_.ReAttach();
    return error ? false : true;
}

bool FtlShell::OnVolumeAdded(uint32_t page_size, uint32_t num_pages) {
    page_size_ = page_size;
    num_pages_ = num_pages;
    return true;
}
