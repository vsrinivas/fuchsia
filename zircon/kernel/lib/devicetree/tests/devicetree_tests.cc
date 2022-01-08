// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/devicetree/devicetree.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>

#ifndef __Fuchsia__
#include <libgen.h>
#include <unistd.h>
#endif  // !__Fuchsia__

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <memory>

#include <zxtest/zxtest.h>

namespace {

constexpr size_t kMaxSize = 1024;

#if defined(__Fuchsia__)
constexpr std::string_view kTestDataDir = "/pkg/data";
#else
constexpr std::string_view kTestDataDir = "test_data/devicetree";
#endif

// TODO: make the non-fuchsia part of this a general utility that can be shared
// across host-side tests.
void GetTestDataPath(std::string_view filename, std::filesystem::path& path) {
#if defined(__Fuchsia__)
  path.append(kTestDataDir);
#elif defined(__APPLE__)
  uint32_t length = PATH_MAX;
  char self_path[length];
  char self_path_symlink[length];
  _NSGetExecutablePath(self_path_symlink, &length);
  const char* bin_dir = dirname(realpath(self_path_symlink, self_path));
  path.append(bin_dir).append(kTestDataDir);
#elif defined(__linux__)
  char self_path[PATH_MAX];
  const char* bin_dir = dirname(realpath("/proc/self/exe", self_path));
  path.append(bin_dir).append(kTestDataDir);
#else
#error unknown platform.
#endif
  path.append(filename);
}

void ReadTestData(std::string_view filename, uint8_t buff[kMaxSize]) {
  std::filesystem::path path;
  ASSERT_NO_FATAL_FAILURE(GetTestDataPath(filename, path));

  FILE* file = fopen(path.c_str(), "r");
  ASSERT_NOT_NULL(file, "failed to open %s: %s", path.c_str(), strerror(errno));

  ASSERT_EQ(0, fseek(file, 0, SEEK_END));
  auto size = static_cast<size_t>(ftell(file));
  ASSERT_LE(size, kMaxSize, "file is too large (%zu bytes)", size);
  rewind(file);

  ASSERT_EQ(size, fread(reinterpret_cast<char*>(buff), 1, size, file));

  ASSERT_EQ(0, fclose(file));
}

TEST(DevicetreeTest, SplitNodeName) {
  {
    auto [name, unit_addr] = devicetree::SplitNodeName("abc");
    EXPECT_STREQ("abc", name);
    EXPECT_STREQ("", unit_addr);
  }
  {
    auto [name, unit_addr] = devicetree::SplitNodeName("abc@");
    EXPECT_STREQ("abc", name);
    EXPECT_STREQ("", unit_addr);
  }
  {
    auto [name, unit_addr] = devicetree::SplitNodeName("abc@def");
    EXPECT_STREQ("abc", name);
    EXPECT_STREQ("def", unit_addr);
  }
  {
    auto [name, unit_addr] = devicetree::SplitNodeName("@def");
    EXPECT_STREQ("", name);
    EXPECT_STREQ("def", unit_addr);
  }
}

TEST(DevicetreeTest, EmptyTree) {
  uint8_t fdt[kMaxSize];
  ReadTestData("empty.dtb", fdt);
  devicetree::Devicetree dt({fdt, kMaxSize});

  size_t seen = 0;
  auto walker = [&seen](const devicetree::NodePath& path, devicetree::Properties) {
    if (seen++ == 0) {
      size_t size = path.size_slow();
      EXPECT_EQ(1, size);
      if (size > 0) {
        EXPECT_TRUE(path.back().empty());  // Root node.
      }
    }
    return true;
  };
  dt.Walk(walker);
  EXPECT_EQ(1, seen);
}

TEST(DevicetreeTest, NodesAreVisitedDepthFirst) {
  /*
         *
        / \
       A   E
      / \   \
     B   C   F
        /   / \
       D   G   I
          /
         H
  */
  uint8_t fdt[kMaxSize];
  ReadTestData("complex_no_properties.dtb", fdt);
  devicetree::Devicetree dt({fdt, kMaxSize});

  size_t seen = 0;
  constexpr std::string_view expected_names[] = {"", "A", "B", "C", "D", "E", "F", "G", "H", "I"};
  constexpr size_t expected_sizes[] = {1, 2, 3, 3, 4, 2, 3, 4, 5, 4};
  auto walker = [&seen, &expected_names, &expected_sizes](const devicetree::NodePath& path,
                                                          devicetree::Properties) {
    if (seen < std::size(expected_names)) {
      size_t size = path.size_slow();
      EXPECT_EQ(expected_sizes[seen], path.size_slow());
      if (size > 0) {
        EXPECT_STREQ(expected_names[seen], path.back());
      }
    }
    ++seen;
    return true;
  };
  dt.Walk(walker);
  EXPECT_EQ(std::size(expected_names), seen);
}

TEST(DevicetreeTest, SubtreesArePruned) {
  /*
         *
        / \
       A   E
      / \   \
     B   C^  F^
        /   / \
       D   G   I
          /
         H

   ^ = root of pruned subtree
  */
  uint8_t fdt[kMaxSize];
  ReadTestData("complex_no_properties.dtb", fdt);
  devicetree::Devicetree dt({fdt, kMaxSize});

  size_t seen = 0;
  constexpr std::string_view expected_names[] = {"", "A", "B", "C", "E", "F"};
  constexpr size_t expected_sizes[] = {1, 2, 3, 3, 2, 3};
  constexpr bool pruned[] = {false, false, false, true, false, true};
  auto walker = [&seen, &expected_names, &expected_sizes](const devicetree::NodePath& path,
                                                          devicetree::Properties) {
    if (seen < std::size(expected_names)) {
      size_t size = path.size_slow();
      EXPECT_EQ(expected_sizes[seen], size);
      if (size > 0) {
        EXPECT_STREQ(expected_names[seen], path.back());
      }
    }
    return !pruned[seen++];
  };
  dt.Walk(walker);
  EXPECT_EQ(std::size(expected_names), seen);
}

TEST(DevicetreeTest, WholeTreeIsPruned) {
  /*
           *^
          / \
         A   E
        / \   \
       B   C   F
          /   / \
         D   G   I
            /
           H

     ^ = root of pruned subtree
    */

  uint8_t fdt[kMaxSize];
  ReadTestData("complex_no_properties.dtb", fdt);
  devicetree::Devicetree dt({fdt, kMaxSize});

  size_t seen = 0;
  auto walker = [&seen](const devicetree::NodePath& path, devicetree::Properties) {
    if (seen++ == 0) {
      size_t size = path.size_slow();
      EXPECT_EQ(1, size);
      if (size > 0) {
        EXPECT_TRUE(path.back().empty());  // Root node.
      }
    }
    return false;
  };
  dt.Walk(walker);
  EXPECT_EQ(1, seen);
}

TEST(DevicetreeTest, PropertiesAreTranslated) {
  /*
         *
        / \
       A   C
      /     \
     B       D
  */
  uint8_t fdt[kMaxSize];
  ReadTestData("simple_with_properties.dtb", fdt);
  devicetree::Devicetree dt({fdt, kMaxSize});

  size_t seen = 0;
  auto walker = [&seen](const devicetree::NodePath& path, devicetree::Properties props) {
    switch (seen++) {
      case 0: {  // root
        size_t size = path.size_slow();
        EXPECT_EQ(1, size);
        if (size > 0) {
          EXPECT_TRUE(path.back().empty());
        }

        devicetree::Properties::iterator begin;
        begin = props.begin();  // Can copy-assign.
        EXPECT_EQ(begin, props.end());

        break;
      }
      case 1: {  // A
        size_t size = path.size_slow();
        EXPECT_EQ(2, size);
        if (size > 0) {
          EXPECT_STREQ("A", path.back());
        }
        EXPECT_EQ(props.end(), std::next(props.begin(), 2));  // 2 properties.

        auto prop1 = *props.begin();
        EXPECT_STREQ("a1", prop1.name);
        EXPECT_TRUE(prop1.value.AsBool());
        auto prop2 = *std::next(props.begin());
        EXPECT_STREQ("a2", prop2.name);
        EXPECT_STREQ("root", prop2.value.AsString());
        break;
      }
      case 2: {  // B
        size_t size = path.size_slow();
        EXPECT_EQ(3, size);
        if (size > 0) {
          EXPECT_STREQ("B", path.back());
        }
        EXPECT_EQ(props.end(), std::next(props.begin(), 3));  // 3 properties.

        auto prop1 = *props.begin();
        EXPECT_STREQ("b1", prop1.name);
        EXPECT_EQ(0x1, prop1.value.AsUint32());
        auto prop2 = *std::next(props.begin());
        EXPECT_STREQ("b2", prop2.name);
        EXPECT_EQ(0x10, prop2.value.AsUint32());
        auto prop3 = *std::next(props.begin(), 2);
        EXPECT_STREQ("b3", prop3.name);
        EXPECT_EQ(0x100, prop3.value.AsUint32());
        break;
      }
      case 3: {  // C
        size_t size = path.size_slow();
        EXPECT_EQ(2, size);
        if (size > 0) {
          EXPECT_STREQ("C", path.back());
        }
        EXPECT_EQ(props.end(), std::next(props.begin(), 2));  // 2 properties.

        auto prop1 = *props.begin();
        EXPECT_STREQ("c1", prop1.name);
        EXPECT_STREQ("hello", prop1.value.AsString());
        auto prop2 = *std::next(props.begin());
        EXPECT_STREQ("c2", prop2.name);
        EXPECT_STREQ("world", prop2.value.AsString());
        break;
      }
      case 4: {  // D
        size_t size = path.size_slow();
        EXPECT_EQ(3, size);
        if (size > 0) {
          EXPECT_STREQ("D", path.back());
        }
        EXPECT_EQ(props.end(), std::next(props.begin(), 3));  // 3 properties.

        auto prop1 = *props.begin();
        EXPECT_STREQ("d1", prop1.name);
        EXPECT_EQ(0x1000, prop1.value.AsUint64());
        auto prop2 = *std::next(props.begin());
        EXPECT_STREQ("d2", prop2.name);
        EXPECT_EQ(0x10000, prop2.value.AsUint64());
        auto prop3 = *std::next(props.begin(), 2);
        EXPECT_STREQ("d3", prop3.name);
        EXPECT_EQ(0x100000, prop3.value.AsUint64());
        break;
      }
    }
    return true;
  };
  dt.Walk(walker);
  EXPECT_EQ(5, seen);
}

TEST(DevicetreeTest, MemoryReservations) {
  uint8_t fdt[kMaxSize];
  ReadTestData("memory_reservations.dtb", fdt);
  const devicetree::Devicetree dt({fdt, kMaxSize});

  unsigned int i = 0;
  for (auto [start, size] : dt.memory_reservations()) {
    switch (i++) {
      case 0:
        EXPECT_EQ(start, 0x12340000);
        EXPECT_EQ(size, 0x2000);
        break;
      case 1:
        EXPECT_EQ(start, 0x56780000);
        EXPECT_EQ(size, 0x3000);
        break;
      case 2:
        EXPECT_EQ(start, 0x7fffffff12340000);
        EXPECT_EQ(size, 0x400000000);
        break;
      case 3:
        EXPECT_EQ(start, 0x00ffffff56780000);
        EXPECT_EQ(size, 0x500000000);
        break;
      default:
        EXPECT_LT(i, 4, "too many entries");
        break;
    }
  }
  EXPECT_EQ(i, 4, "wrong number of entries");
}

TEST(DevicetreeTest, StringList) {
  using namespace std::literals;

  unsigned int i = 0;
  for (auto str : devicetree::StringList(""sv)) {
    ++i;
    EXPECT_FALSE(true, "list should be empty");
    EXPECT_TRUE(str.empty());
  }
  EXPECT_EQ(i, 0);

  i = 0;
  for (auto str : devicetree::StringList("one"sv)) {
    ++i;
    EXPECT_STREQ("one", str);
  }
  EXPECT_EQ(i, 1);

  i = 0;
  for (auto str : devicetree::StringList("one\0two\0three"sv)) {
    switch (i++) {
      case 0:
        EXPECT_STREQ("one", str);
        break;
      case 1:
        EXPECT_STREQ("two", str);
        break;
      case 2:
        EXPECT_STREQ("three", str);
        break;
    }
  }
  EXPECT_EQ(i, 3);

  i = 0;
  for (auto str : devicetree::StringList("one\0\0two\0"sv)) {
    switch (i++) {
      case 0:
        EXPECT_STREQ("one", str);
        break;
      case 2:
        EXPECT_STREQ("two", str);
        break;
      default:
        EXPECT_EQ(0, str.size());
    }
  }
  EXPECT_EQ(i, 4);

  i = 0;
  for (auto str : devicetree::StringList<'/'>("foo/bar/baz"sv)) {
    switch (i++) {
      case 0:
        EXPECT_STREQ("foo", str);
        break;
      case 1:
        EXPECT_STREQ("bar", str);
        break;
      case 3:
        EXPECT_STREQ("baz", str);
        break;
    }
  }
  EXPECT_EQ(i, 3);
}

}  // namespace
