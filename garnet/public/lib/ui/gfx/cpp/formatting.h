// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_GFX_CPP_FORMATTING_H_
#define LIB_UI_GFX_CPP_FORMATTING_H_

#include <iosfwd>

#include <fuchsia/ui/gfx/cpp/fidl.h>

namespace fuchsia {
namespace ui {
namespace gfx {

// NOTE:
// //garnet/public/lib/fostr/fidl/fuchsia.ui.gfx generates ostream formatters
// for this library *except* those formatters that are listed here. The code
// generator knows which formatters to exclude from the generated code by
// consulting the 'amendments.json' file in that directory.
//
// If you add or remove formatters from this file, please be sure that the
// amendments.json file is updated accordingly.

// NOTE: fostr doesn't generate operator<< for union tags, so we explicitly add
// our own where desired.

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Value::Tag& tag);

}  // namespace gfx
}  // namespace ui
}  // namespace fuchsia

#endif  // LIB_UI_GFX_CPP_FORMATTING_H_
