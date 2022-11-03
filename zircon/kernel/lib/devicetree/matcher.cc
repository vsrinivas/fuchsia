// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/devicetree/internal/matcher.h"

#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/matcher-result.h>
#include <lib/devicetree/matcher.h>
#include <lib/devicetree/path.h>

#include <string_view>

namespace devicetree {

MatcherResult internal::AliasMatcher::operator()(const NodePath& path, Properties props) {
  constexpr std::string_view kAliasPath = "/aliases";
  switch (ComparePath(path, kAliasPath)) {
    case kIsAncestor:
      return MatcherResult::kVisitSubtree;
    case kIsMatch:
      aliases.emplace(props);
      return MatcherResult::kDone;
    default:
      return MatcherResult::kAvoidSubtree;
  };
}

}  // namespace devicetree
