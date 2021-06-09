// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/range.h>

#include <string_view>

namespace memalloc {

using namespace std::string_view_literals;

std::string_view ToString(Type type) {
  using namespace std::string_view_literals;

  switch (type) {
    case Type::kFreeRam:
      return "free RAM"sv;
    case Type::kReserved:
      return "reserved"sv;
    case Type::kPeripheral:
      return "peripheral"sv;
  }
  return "unknown"sv;
}

}  // namespace memalloc
