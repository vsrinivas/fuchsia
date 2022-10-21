// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_MATCHER_H_
#define ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_MATCHER_H_

#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/matcher-result.h>
#include <lib/devicetree/path.h>

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace devicetree::internal {

// Walk state kept by the Scan operation, to determine when a subtree needs to be visited,
// or if more scans are needed.
class VisitState {
 public:
  constexpr VisitState() = default;
  constexpr VisitState(const VisitState&) = default;
  constexpr VisitState& operator=(const VisitState&) = default;

  // Initialize and assign from
  explicit constexpr VisitState(MatcherResult state) : state_(state) {}
  constexpr VisitState& operator=(MatcherResult state) {
    state_ = state;
    mark_ = nullptr;
    return *this;
  }

  constexpr MatcherResult state() const { return state_; }

  constexpr void Prune(const NodePath& path) { mark_ = &path.back(); }

  constexpr void Unprune(const NodePath& path) {
    if (mark_ == &path.back()) {
      *this = VisitState();
    }
  }

 private:
  MatcherResult state_ = MatcherResult::kVisitSubtree;
  const Node* mark_ = nullptr;
};

// All visitors have the same return type.
template <typename Visitor, typename... Matchers>
using VisitorResultType =
    std::invoke_result_t<Visitor, typename std::tuple_element_t<0, std::tuple<Matchers...>>,
                         size_t>;

// Helper for visiting each matcher with their respective index (matcher0, .... matcherN-1,
// where N == sizeof...(Matchers...)).
template <typename Visitor, size_t... Is, typename... Matchers>
constexpr void ForEachMatcher(Visitor&& visitor, std::index_sequence<Is...> is,
                              Matchers&&... matchers) {
  // Visitor has void return type, wrap on some callable that just returns true and discard
  // the result.
  auto wrapper = [&](auto& matcher, size_t index) -> bool {
    visitor(matcher, index);
    return true;
  };
  [[maybe_unused]] auto res = {wrapper(matchers, Is)...};
}

// Helper for visiting each matcher.
template <typename Visitor, typename... Matchers>
constexpr void ForEachMatcher(Visitor&& visitor, Matchers&&... matchers) {
  // All visitor must be invocable with the visit state and the matchers.
  static_assert((... && std::is_invocable_v<Visitor, Matchers, size_t>),
                "|Visitor| must provide an overload for each provided matcher.");

  static_assert((... && std::is_same_v<VisitorResultType<Visitor, Matchers...>,
                                       std::invoke_result_t<Visitor, Matchers, size_t>>),
                "|Visitor| must have the same return type for all matcher types.");
  ForEachMatcher(std::forward<Visitor>(visitor), std::make_index_sequence<sizeof...(Matchers)>{},
                 std::forward<Matchers>(matchers)...);
}

template <typename... Matchers>
size_t Match(Devicetree& tree, Matchers&... matchers) {
  // Walk state for each matcher.
  std::array<VisitState, sizeof...(Matchers)> visit_state = {};

  // Helper for checking if we can terminate early if all matchers are done.
  auto all_matchers_done = [&visit_state]() {
    return std::all_of(visit_state.begin(), visit_state.end(), [](const VisitState& matcher_state) {
      return matcher_state.state() == MatcherResult::kDone;
    });
  };

  size_t i = 0;

  auto visit_and_prune = [&visit_state, &matchers...](const NodePath& path, Properties props) {
    ForEachMatcher(
        [&visit_state, &path, props](auto& matcher, size_t index) -> void {
          if (visit_state[index].state() == MatcherResult::kVisitSubtree) {
            visit_state[index] = matcher(path, props);
            if (visit_state[index].state() == MatcherResult::kAvoidSubtree) {
              visit_state[index].Prune(path);
            }
          }
        },
        matchers...);

    return std::any_of(visit_state.begin(), visit_state.end(), [](auto& visit_state) {
      return visit_state.state() == MatcherResult::kVisitSubtree;
    });
  };

  auto unprune = [&visit_state, &matchers...](const NodePath& path, Properties props) {
    ForEachMatcher(
        [&visit_state, &path](auto& matcher, size_t index) { visit_state[index].Unprune(path); },
        matchers...);
    return true;
  };

  while (!all_matchers_done()) {
    tree.Walk(visit_and_prune, unprune);
    ++i;
  }

  return i;
}

}  // namespace devicetree::internal

#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_MATCHER_H_
