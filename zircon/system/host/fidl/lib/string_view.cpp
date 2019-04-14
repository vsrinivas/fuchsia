// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/string_view.h>

#include <sstream>

namespace fidl {

std::ostream& operator<<(std::ostream& os, const StringView& string_view) {
    os.rdbuf()->sputn(string_view.data(), string_view.size());
    return os;
}

} // namespace fidl
