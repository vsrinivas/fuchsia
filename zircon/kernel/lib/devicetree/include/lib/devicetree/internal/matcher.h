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

  constexpr MatcherResult state() const { return state_; }
  void set_state(MatcherResult state) { state_ = state; }

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

template <typename Matcher>
constexpr bool kIsValidMatcher =
    std::is_invocable_v<Matcher, const NodePath&, Properties> ^
    std::is_invocable_v<Matcher, const NodePath&, Properties, const PathResolver&>;

// Compile time dispatch depending on the implemented matcher interface.
template <typename Matcher>
constexpr auto Dispatch(Matcher&& matcher, const NodePath& path, const Properties& props,
                        const PathResolver& resolver) {
  if constexpr (std::is_invocable_v<Matcher, const NodePath&, Properties>) {
    return matcher(path, props);
  } else {
    return matcher(path, props, resolver);
  }
}

template <typename Matcher>
using MatcherResultType = decltype(Dispatch(
    std::declval<Matcher>(), NodePath{}, std::declval<Properties>(), std::declval<PathResolver>()));

// Helper to obtain the requested scans from the return type.
template <size_t N>
constexpr size_t GetRequestedScans(MatcherScanResult<N>* r) {
  return N;
}

constexpr size_t GetRequestedScans(MatcherResult* r) { return 1; }

template <typename Matcher>
constexpr bool kNeedsAliases =
    std::is_invocable_v<Matcher, const NodePath&, Properties, const PathResolver&>;

// Actual number of scans requested by a given implementation of a Matcher.
// When a matcher needs aliases, an extra scan may be needed to resolve the `aliases` node.
template <typename Matcher>
constexpr size_t kRequestedScans =
    GetRequestedScans(static_cast<MatcherResultType<Matcher>*>(nullptr)) +
    (kNeedsAliases<Matcher> ? 1 : 0);

// For a set of matchers, the maximum number of scans needed to complete them,
// in an ideal case.
template <typename... Matchers>
constexpr size_t kMaxRequestedScans = std::max({kRequestedScans<Matchers>...});

// Used for looking for the aliases node.
struct AliasMatcher {
  MatcherResult operator()(const NodePath& path, Properties props);

  std::optional<Properties> aliases;
};

template <size_t Index, typename... Matchers>
using MatcherTypeAt = std::tuple_element_t<Index, std::tuple<Matchers...>>;

template <typename... Matchers>
constexpr bool kHasAliasMatcher =
    std::is_same_v<devicetree::internal::AliasMatcher,
                   MatcherTypeAt<sizeof...(Matchers) - 1, Matchers...>>;

template <typename... Matchers>
inline PathResolver GetPathResolver(Matchers&&... matchers) {
  auto& maybe_alias_matcher = std::get<sizeof...(Matchers) - 1>(std::forward_as_tuple(matchers...));
  if constexpr (std::is_same_v<std::decay_t<decltype(maybe_alias_matcher)>, AliasMatcher>) {
    return PathResolver(maybe_alias_matcher.aliases);
  } else {
    return PathResolver(std::nullopt);
  }
}

template <typename... Matchers>
size_t Match(Devicetree& tree, Matchers&... matchers) {
  // Walk state for each matcher.
  std::array<VisitState, sizeof...(Matchers)> visit_state = {};

  // Helper for checking if we can terminate early if all matchers are done.
  auto all_matchers_done = [&visit_state]() {
    // Alias node do not need to be resolved for the match operation to be completed, if
    // all other matchers are completed already.
    if constexpr (kHasAliasMatcher<Matchers...>) {
      return std::all_of(visit_state.begin(), std::prev(visit_state.end()),
                         [](const VisitState& matcher_state) {
                           return matcher_state.state() == MatcherResult::kDone;
                         });
    } else {
      return std::all_of(visit_state.begin(), visit_state.end(),
                         [](const VisitState& matcher_state) {
                           return matcher_state.state() == MatcherResult::kDone;
                         });
    }
  };

  static constexpr auto to_matcher_result = [](auto typed_result) constexpr {
    if constexpr (std::is_same_v<decltype(typed_result), MatcherResult>) {
      return typed_result;
    } else {
      return typed_result.result;
    }
  };

  auto visit_and_prune = [&visit_state, &matchers...](const NodePath& path, Properties props) {
    // Use of optional for delayed instantiation, |*resolver| is always valid.
    auto resolver = GetPathResolver(matchers...);

    ForEachMatcher(
        [&resolver, &visit_state, &path, props](auto& matcher, size_t index) -> void {
          if (visit_state[index].state() == MatcherResult::kVisitSubtree ||
              (visit_state[index].state() == MatcherResult::kNeedsAliases &&
               resolver.has_aliases())) {
            visit_state[index].set_state(
                to_matcher_result(Dispatch(matcher, path, props, resolver)));
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

  for (size_t i = 0; i < kMaxRequestedScans<Matchers...>; ++i) {
    tree.Walk(visit_and_prune, unprune);

    if (all_matchers_done()) {
      return i + 1;
    }
  }

  return -1;
}

}  // namespace devicetree::internal
#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_INTERNAL_MATCHER_H_
