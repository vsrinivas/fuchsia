// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "source_manager.h"

namespace fidl {

bool SourceManager::CreateSource(const char* filename, StringView* out_source) {
    FILE* file = fopen(filename, "rb");
    if (!file)
        return false;

    sources_.emplace_back();
    auto& source = sources_.back();

    // The lexer requires zero terminated data.
    fseek(file, 0, SEEK_END);
    auto filesize = ftell(file);
    source.resize(filesize + 1);
    rewind(file);
    fread(&source[0], 1, filesize, file);
    source[filesize] = 0;
    fclose(file);

    *out_source = StringView(source);
    return true;
}

} // namespace fidl
