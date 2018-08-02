// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/source_manager.h"

#include <utility>

namespace fidl {

bool SourceManager::CreateSource(StringView filename) {
    FILE* file = fopen(filename.data(), "rb");
    if (!file)
        return false;

    // The lexer requires zero terminated data.
    std::string data;
    fseek(file, 0, SEEK_END);
    auto filesize = ftell(file);
    data.resize(filesize + 1);
    rewind(file);
    fread(&data[0], 1, filesize, file);
    data[filesize] = 0;
    fclose(file);

    AddSourceFile(std::make_unique<SourceFile>(filename, std::move(data)));
    return true;
}

void SourceManager::AddSourceFile(std::unique_ptr<SourceFile> file) {
    sources_.push_back(std::move(file));
}

} // namespace fidl
