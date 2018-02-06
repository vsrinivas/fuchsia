// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/source_manager.h"

namespace fidl {

const SourceFile* SourceManager::CreateSource(StringView filename) {
    FILE* file = fopen(filename.data(), "rb");
    if (!file)
        return nullptr;

    // The lexer requires zero terminated data.
    std::string data;
    fseek(file, 0, SEEK_END);
    auto filesize = ftell(file);
    data.resize(filesize + 1);
    rewind(file);
    fread(&data[0], 1, filesize, file);
    data[filesize] = 0;
    fclose(file);

    sources_.emplace_back(filename, std::move(data));
    return &sources_.back();
}

} // namespace fidl
