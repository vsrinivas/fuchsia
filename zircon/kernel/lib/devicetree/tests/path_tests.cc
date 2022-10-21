// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/path.h>
#include <lib/stdcompat/span.h>

#include <array>
#include <memory>
#include <string_view>
#include <type_traits>

#include <zxtest/zxtest.h>

namespace {

auto as_bytes = [](auto& val) {
  using byte_type = std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(val)>>,
                                       const uint8_t, uint8_t>;
  return cpp20::span<byte_type>(reinterpret_cast<byte_type*>(&val), sizeof(val));
};

auto append = [](auto& vec, auto&& other) { vec.insert(vec.end(), other.begin(), other.end()); };

uint32_t AsBigEndian(uint32_t val) {
  auto bytes = as_bytes(val);
  return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
         static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

struct AliasContext {
  devicetree::Properties properties() {
    return devicetree::Properties(
        {property_block.data(), property_block.size()},
        std::string_view(reinterpret_cast<const char*>(string_block.data()), string_block.size()));
  }

  void Add(std::string_view alias, std::string_view absolute_path) {
    constexpr std::array<uint8_t, sizeof(uint32_t) - 1> kPadding = {};

    uint32_t name_off = AsBigEndian(static_cast<uint32_t>(string_block.size()));
    // String must be null terminated.
    append(string_block, alias);
    string_block.push_back('\0');

    // Add the null terminator.
    uint32_t len = static_cast<uint32_t>(absolute_path.size()) + 1;
    cpp20::span<const uint8_t> padding;
    if (auto remainder = len % sizeof(uint32_t); remainder != 0) {
      padding = cpp20::span(kPadding).subspan(0, sizeof(uint32_t) - remainder);
    }
    len = AsBigEndian(len);

    if (!property_block.empty()) {
      const uint32_t kFdtPropToken = AsBigEndian(0x00000003);
      append(property_block, as_bytes(kFdtPropToken));
    }
    append(property_block, as_bytes(len));
    append(property_block, as_bytes(name_off));
    append(property_block, absolute_path);
    property_block.push_back('\0');
    append(property_block, padding);
  }

  std::vector<uint8_t> property_block;
  std::vector<uint8_t> string_block;
};

TEST(PathResolverTest, ResolveAboslutePath) {
  std::string_view path = "/absolute_path/with_leaf";
  std::optional<devicetree::Properties> empty_aliases;
  devicetree::PathResolver resolver(empty_aliases);

  auto resolved_path = resolver.Resolve(path);
  ASSERT_TRUE(resolved_path.is_ok());

  auto [stem, leaf] = *resolved_path;
  EXPECT_EQ(stem, path);
  EXPECT_TRUE(leaf.empty());
}

TEST(PathResolverTest, ResolveRelativePathNoAliasNode) {
  std::string_view path = "alias/with_leaf";
  std::optional<devicetree::Properties> empty_aliases;
  devicetree::PathResolver resolver(empty_aliases);

  auto resolved_path = resolver.Resolve(path);
  ASSERT_TRUE(resolved_path.is_error());
  EXPECT_EQ(resolved_path.error_value(), devicetree::PathResolver::ResolveError::kNoAliases);
}

TEST(PathResolverTest, ResolveRelativePathWithAliasNoLeaf) {
  std::string_view alias_path = "alias";
  std::string_view absolute_path = "/absolute_path/is_here";

  AliasContext aliases;
  aliases.Add("11", "/1");
  aliases.Add("2", "/2");
  aliases.Add("3", "/3");
  aliases.Add(alias_path, absolute_path);
  std::optional<devicetree::Properties> alias_prop = aliases.properties();
  devicetree::PathResolver resolver(alias_prop);

  auto resolved_path = resolver.Resolve(alias_path);
  ASSERT_TRUE(resolved_path.is_ok());

  auto [stem, leaf] = *resolved_path;
  EXPECT_EQ(stem, absolute_path);
  EXPECT_TRUE(leaf.empty());
}

TEST(PathResolverTest, ResolveRelativePathWithAliasWithLeaf) {
  std::string_view alias_path = "alias";
  std::string_view absolute_path = "/absolute_path/is_here";

  AliasContext aliases;
  aliases.Add("11", "/1");
  aliases.Add("2", "/2");
  aliases.Add("3", "/3");
  aliases.Add(alias_path, absolute_path);
  std::optional<devicetree::Properties> alias_prop = aliases.properties();
  devicetree::PathResolver resolver(alias_prop);

  std::string_view alias_with_leaf = "alias/this/is/a/leaf";
  auto resolved_path = resolver.Resolve(alias_with_leaf);
  ASSERT_TRUE(resolved_path.is_ok());

  auto [stem, leaf] = *resolved_path;
  EXPECT_EQ(stem, absolute_path);
  EXPECT_EQ(leaf, alias_with_leaf.substr(alias_path.size() + 1));
}

TEST(PathResolverTest, ResolveRelativePathWithNoMatchingAlias) {
  std::string_view alias_path = "alias";
  std::string_view absolute_path = "/absolute_path/is_here";

  AliasContext aliases;
  aliases.Add("11", "/1");
  aliases.Add("2", "/2");
  aliases.Add("3", "/3");
  aliases.Add(alias_path, absolute_path);
  std::optional<devicetree::Properties> alias_prop = aliases.properties();
  devicetree::PathResolver resolver(alias_prop);

  std::string_view unknown_alias = "unknown_alias/this/is/a/leaf";
  auto resolved_path = resolver.Resolve(unknown_alias);

  ASSERT_TRUE(resolved_path.is_error());
  EXPECT_EQ(resolved_path.error_value(), devicetree::PathResolver::ResolveError::kBadAlias);
}

TEST(PathResolverTest, ResolveRelativePathWithEmptyAlias) {
  std::string_view alias_path = "alias";
  std::string_view absolute_path = "";

  AliasContext aliases;
  aliases.Add("11", "/1");
  aliases.Add("2", "/2");
  aliases.Add("3", "/3");
  aliases.Add(alias_path, absolute_path);
  std::optional<devicetree::Properties> alias_prop = aliases.properties();
  devicetree::PathResolver resolver(alias_prop);

  std::string_view empty_alias = "alias/this/is/a/leaf";
  auto resolved_path = resolver.Resolve(empty_alias);

  ASSERT_TRUE(resolved_path.is_error());
  EXPECT_EQ(resolved_path.error_value(), devicetree::PathResolver::ResolveError::kBadAlias);
}

// This test introduces the expected usage on the matcher infrastructure.
TEST(PathResolverTest, AliasPopulatedAfterCreation) {
  std::string_view alias_path = "alias";
  std::string_view absolute_path = "/A/B/C/D";

  std::string_view not_yet_available_alias = "alias/this/is/a/leaf";

  // When scanning a devicetree the alias are empty initially, since they
  // have not been populated yet.
  std::optional<devicetree::Properties> alias_initially_empty;
  devicetree::PathResolver resolver(alias_initially_empty);

  // Attempting to resolve an aliased path(before we actually find the aliases node)
  // gives a signal to the user that it cant make progress yet.
  auto resolved_path = resolver.Resolve(not_yet_available_alias);
  ASSERT_TRUE(resolved_path.is_error());
  EXPECT_EQ(resolved_path.error_value(), devicetree::PathResolver::ResolveError::kNoAliases);

  // Eventually if there is an alias node in a devicetree, it will get populated.
  AliasContext aliases;
  aliases.Add(alias_path, absolute_path);
  alias_initially_empty.emplace(aliases.properties());
  // Now resolving this should return something else.
  resolved_path = resolver.Resolve(not_yet_available_alias);
  ASSERT_TRUE(resolved_path.is_ok());

  EXPECT_EQ(resolved_path->prefix, absolute_path);
  EXPECT_EQ(resolved_path->suffix, "this/is/a/leaf");
}

std::vector<std::string_view> ConvertPath(std::string_view path) {
  std::vector<std::string_view> components;

  while (!path.empty()) {
    size_t component_end = path.find('/');
    components.push_back(path.substr(0, component_end));
    path.remove_prefix(component_end != std::string_view::npos ? component_end : path.size());
    if (!path.empty()) {
      // remove '/'
      path.remove_prefix(1);
    }
  }
  return components;
}

TEST(CompareRangesOfNodesInternalTest, PerfectMatch) {
  auto path_a = ConvertPath("/this/is/my/path");
  auto path_b = ConvertPath("/this/is/my/path");

  auto [it_1, it_2] = devicetree::internal::CompareRangesOfNodes(path_a.begin(), path_a.end(),
                                                                 path_b.begin(), path_b.end());
  EXPECT_EQ(it_1, path_a.end());
  EXPECT_EQ(it_2, path_b.end());
}

TEST(CompareRangesOfNodesInternalTest, PathBIsContainedInPathA) {
  auto path_a = ConvertPath("/this/is/my/path/way/longer/than/b");
  auto path_b = ConvertPath("/this/is/my/path");

  auto [it_1, it_2] = devicetree::internal::CompareRangesOfNodes(path_a.begin(), path_a.end(),
                                                                 path_b.begin(), path_b.end());
  ASSERT_NE(it_1, path_a.end());
  EXPECT_EQ(*it_1, "way");
  EXPECT_EQ(it_2, path_b.end());
}

TEST(CompareRangesOfNodesInternalTest, PathAIsContainedInPathB) {
  auto path_a = ConvertPath("/this/is/my/path");
  auto path_b = ConvertPath("/this/is/my/path/way/longer/than/b");

  auto [it_1, it_2] = devicetree::internal::CompareRangesOfNodes(path_a.begin(), path_a.end(),
                                                                 path_b.begin(), path_b.end());
  EXPECT_EQ(it_1, path_a.end());
  ASSERT_NE(it_2, path_b.end());
  EXPECT_EQ(*it_2, "way");
}

TEST(CompareRangesOfNodesInternalTest, PathAandBDoNotMatch) {
  auto path = ConvertPath("/this/is/my/path");
  auto path_2 = ConvertPath("/this/is/my/other/path");

  auto [it_1, it_2] = devicetree::internal::CompareRangesOfNodes(path.begin(), path.end(),
                                                                 path_2.begin(), path_2.end());
  ASSERT_NE(it_1, path.end());
  ASSERT_NE(it_2, path_2.end());

  EXPECT_EQ(*it_1, "path");
  EXPECT_EQ(*it_2, "other");
}

TEST(CompareRangesOfNodesInternalTest, WithAddressAndNoWildcardMatch) {
  auto path = ConvertPath("/this/is/my@10/path");
  auto path_2 = ConvertPath("/this/is/my@10/path");

  auto [it_1, it_2] = devicetree::internal::CompareRangesOfNodes(path.begin(), path.end(),
                                                                 path_2.begin(), path_2.end());
  ASSERT_EQ(it_1, path.end());
  ASSERT_EQ(it_2, path_2.end());
}

TEST(CompareRangesOfNodesInternalTest, WithAddressAndNoWildcardDoesntMatch) {
  auto path = ConvertPath("/this/is/my@11/path");
  auto path_2 = ConvertPath("/this/is/my@10/path");

  auto [it_1, it_2] = devicetree::internal::CompareRangesOfNodes(path.begin(), path.end(),
                                                                 path_2.begin(), path_2.end());
  ASSERT_NE(it_1, path.end());
  ASSERT_NE(it_2, path_2.end());
  EXPECT_EQ(*it_1, "my@11");
  EXPECT_EQ(*it_2, "my@10");
}

struct NodePathHelper {
  NodePathHelper() = default;
  NodePathHelper(const NodePathHelper&) = delete;
  NodePathHelper(NodePathHelper&&) = default;
  ~NodePathHelper() {
    while (!path.is_empty()) {
      // The pointers will get released as part of nodes destructor.
      std::ignore = path.pop_back();
    }
  }

  std::vector<std::unique_ptr<devicetree::Node>> nodes;
  devicetree::NodePath path;
};

NodePathHelper ConvertToNodePath(std::string_view path) {
  NodePathHelper path_helper;
  auto components = ConvertPath(path);
  for (auto component : components) {
    path_helper.nodes.push_back(std::make_unique<devicetree::Node>(component));
    path_helper.path.push_back(path_helper.nodes.back().get());
  }

  return path_helper;
}

auto ToResolvedPath(std::string_view path,
                    std::optional<devicetree::Properties> aliases = std::nullopt) {
  devicetree::PathResolver resolver(aliases);
  auto resolved = resolver.Resolve(path);
  ZX_ASSERT(resolved.is_ok());
  return resolved.value();
}

TEST(ComparePathTest, AbsolutePathMismatchSameLength) {
  {
    auto [nodes, node_path] = ConvertToNodePath("/A/B/C/D");
    auto target_path = ToResolvedPath("/A/B/E/D");

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMismatch);
    EXPECT_EQ(ComparePath(node_path, "/A/B/E/D"), devicetree::CompareResult::kIsMismatch);
  }
  {
    auto [nodes, node_path] = ConvertToNodePath("/A/B");
    auto target_path = ToResolvedPath("/A/C/E/D");

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMismatch);
    EXPECT_EQ(ComparePath(node_path, "/A/C/E/D"), devicetree::CompareResult::kIsMismatch);
  }
  {
    auto [nodes, node_path] = ConvertToNodePath("/A/C/E/D");
    auto target_path = ToResolvedPath("/A/B");

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMismatch);
    EXPECT_EQ(ComparePath(node_path, "/A/B"), devicetree::CompareResult::kIsMismatch);
  }
}

TEST(ComparePathTest, AbsolutePathMatch) {
  auto [nodes, node_path] = ConvertToNodePath("/A/B/C/D");
  auto target_path = ToResolvedPath("/A/B/C/D");

  EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMatch);
}

TEST(ComparePathTest, AbsolutePathAncestor) {
  {
    auto [nodes, node_path] = ConvertToNodePath("/A/B");
    auto target_path = ToResolvedPath("/A/B/C/D");

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsAncestor);
    EXPECT_EQ(ComparePath(node_path, "/A/B/C/D"), devicetree::CompareResult::kIsAncestor);
  }
  {
    auto [nodes, node_path] = ConvertToNodePath("");
    auto target_path = ToResolvedPath("/A/B/C/D");

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsAncestor);
    EXPECT_EQ(ComparePath(node_path, "/A/B/C/D"), devicetree::CompareResult::kIsAncestor);
  }
}

TEST(ComparePathTest, AbsolutePathDescendant) {
  auto [nodes, node_path] = ConvertToNodePath("/A/B/C/D");
  // Empty string is the root node, so its parent of everything.
  auto target_path = ToResolvedPath("");

  EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsDescendant);
  EXPECT_EQ(ComparePath(node_path, ""), devicetree::CompareResult::kIsDescendant);
}

TEST(ComparePathTest, AliasedPathMismatch) {
  AliasContext aliases;
  aliases.Add("alias", "/A/B/D");

  {  // Stem mismatch with non empty leaf.
    auto [nodes, node_path] = ConvertToNodePath("/A/B/C/D");
    auto target_path = ToResolvedPath("alias/D", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMismatch);
  }
  {  // Stem mismatch with empty leaf
    auto [nodes, node_path] = ConvertToNodePath("/A/B/C/D");
    auto target_path = ToResolvedPath("alias", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMismatch);
  }
  {  // Stem match left mismatch.
    auto [nodes, node_path] = ConvertToNodePath("/A/B/D/D");
    auto target_path = ToResolvedPath("alias/C", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMismatch);
  }
}

TEST(ComparePathTest, AliasedPathAncestor) {
  AliasContext aliases;
  aliases.Add("alias", "/A/B/D");

  {  // Root is ancestor of every node.
    auto [nodes, node_path] = ConvertToNodePath("");
    auto target_path = ToResolvedPath("alias", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsAncestor);
  }

  {  // Ancestor of stem Empty leaf
    auto [nodes, node_path] = ConvertToNodePath("/A/B");
    auto target_path = ToResolvedPath("alias", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsAncestor);
  }

  {  // Ancestor of stem non empty leaf
    auto [nodes, node_path] = ConvertToNodePath("/A/B");
    auto target_path = ToResolvedPath("alias/C", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsAncestor);
  }

  {  // Ancestor of leaf (stem matches)
    auto [nodes, node_path] = ConvertToNodePath("/A/B/D");
    auto target_path = ToResolvedPath("alias/C", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsAncestor);
  }
}

TEST(ComparePathTest, AliasedPathDescendant) {
  AliasContext aliases;
  aliases.Add("alias", "/A");

  {  // Current node is descendant of the alias.
    auto [nodes, node_path] = ConvertToNodePath("/A/B/C");
    auto target_path = ToResolvedPath("alias", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsDescendant);
  }

  {  // Current Node is descendant of the alias with the leaf.
    auto [nodes, node_path] = ConvertToNodePath("/A/B/C");
    auto target_path = ToResolvedPath("alias/B", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsDescendant);
  }
}

TEST(ComparePathTest, AliasedPathMatches) {
  AliasContext aliases;
  aliases.Add("alias", "/A");

  {  // Stem only match
    auto [nodes, node_path] = ConvertToNodePath("/A");
    auto target_path = ToResolvedPath("alias", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMatch);
  }

  {  // Stem and leaf match
    auto [nodes, node_path] = ConvertToNodePath("/A/B/C");
    auto target_path = ToResolvedPath("alias/B/C", aliases.properties());

    EXPECT_EQ(ComparePath(node_path, target_path), devicetree::CompareResult::kIsMatch);
  }
}
}  // namespace
