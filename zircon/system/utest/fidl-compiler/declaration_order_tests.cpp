// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <string>

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

#define DECL_NAME(D) \
    static_cast<const std::string>(D->name.name_part()).c_str()

#define ASSERT_DECL_NAME(D, N) \
    ASSERT_STR_EQ(N, DECL_NAME(D));

namespace {

// The calculated declaration order is a product of both the inter-type dependency relationships,
// and an ordering among the type names. To eliminate the effect of name ordering and exclusively
// test dependency ordering, this utility manufactures random names for the types tested.
class Namer {
public:
    Namer() : vars_() {}

    std::string mangle(std::string input) {
        std::size_t start_pos = 0;
        std::size_t max_length = 0;
        while ((start_pos = input.find_first_of('#', start_pos)) != std::string::npos) {
            std::size_t end_pos = input.find_first_of('#', start_pos + 1);
            assert(end_pos != std::string::npos);
            std::size_t key_len = end_pos - start_pos;
            max_length = std::max(max_length, key_len);
            start_pos = end_pos + 1;
        }
        std::size_t normalize_length = max_length + 5;
        while ((start_pos = input.find_first_of('#')) != std::string::npos) {
            std::size_t end_pos = input.find_first_of('#', start_pos + 1);
            auto key = input.substr(start_pos + 1, end_pos - start_pos - 1);
            if (vars_.find(key) == vars_.end()) {
                vars_[key] = random_prefix(key, normalize_length);
            }
            auto replacement = vars_.at(key);
            input.replace(start_pos, end_pos - start_pos + 1, replacement);
        }
        return input;
    }

    const char* of(const std::string& key) const {
        return vars_.at(key).c_str();
    }

private:
    std::string random_prefix(std::string label, std::size_t up_to) {
        // normalize any name to at least |up_to| characters, by adding random prefix
        static std::string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        static long seed = std::chrono::system_clock::now().time_since_epoch().count();
        static std::default_random_engine gen(seed);
        static std::uniform_int_distribution<size_t> distribution(0, characters.size() - 1);
        if (label.size() < up_to - 1) {
            label = "_" + label;
        }
        while (label.size() < up_to) {
            label.insert(0, 1, characters[distribution(gen)]);
        }
        return label;
    }

    std::map<std::string, std::string> vars_;
};

constexpr int kRepeatTestCount = 100;

bool nonnullable_ref() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

struct #Request# {
  array<#Element#>:4 req;
};

struct #Element# {};

protocol #Protocol# {
  SomeMethod(#Request# req);
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(4, decl_order.size());
        ASSERT_DECL_NAME(decl_order[0], namer.of("Element"));
        ASSERT_DECL_NAME(decl_order[1], namer.of("Request"));
        ASSERT_DECL_NAME(decl_order[2], "SomeLongAnonymousPrefix0");
        ASSERT_DECL_NAME(decl_order[3], namer.of("Protocol"));
    }

    END_TEST;
}

bool nullable_ref_breaks_dependency() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

struct #Request# {
  array<#Element#?>:4 req;
};

struct #Element# {
  #Protocol# prot;
};

protocol #Protocol# {
  SomeMethod(#Request# req);
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(4, decl_order.size());

        // Since the Element struct contains a Protocol handle, it does not
        // have any dependencies, and we therefore have two independent
        // declaration sub-graphs:
        //   a. Element
        //   b. Request <- SomeLongAnonymousPrefix0 <- Protocol
        // Because of random prefixes, either (a) or (b) will be selected to
        // be first in the declaration order.
        bool element_is_first = strcmp(
          DECL_NAME(decl_order[0]), namer.of("Element")) == 0;

        if (element_is_first) {
          ASSERT_DECL_NAME(decl_order[0], namer.of("Element"));
          ASSERT_DECL_NAME(decl_order[1], namer.of("Request"));
          ASSERT_DECL_NAME(decl_order[2], "SomeLongAnonymousPrefix0");
          ASSERT_DECL_NAME(decl_order[3], namer.of("Protocol"));
        } else {
          ASSERT_DECL_NAME(decl_order[0], namer.of("Request"));
          ASSERT_DECL_NAME(decl_order[1], "SomeLongAnonymousPrefix0");
          ASSERT_DECL_NAME(decl_order[2], namer.of("Protocol"));
          ASSERT_DECL_NAME(decl_order[3], namer.of("Element"));
        }
    }

    END_TEST;
}

bool request_type_breaks_dependency_graph() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

struct #Request# {
  request<#Protocol#> req;
};

protocol #Protocol# {
  SomeMethod(#Request# req);
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(3, decl_order.size());
        ASSERT_DECL_NAME(decl_order[0], namer.of("Request"));
        ASSERT_DECL_NAME(decl_order[1], "SomeLongAnonymousPrefix0");
        ASSERT_DECL_NAME(decl_order[2], namer.of("Protocol"));
    }

    END_TEST;
}

// A xunion has the same effect dependency-wise, be it nullable or nonnullable.
bool nonnullable_xunion() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

xunion #Xunion# {
  request<#Protocol#> req;
  #Payload# foo;
};

protocol #Protocol# {
  SomeMethod(#Xunion# req);
};

struct #Payload# {
  int32 a;
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(4, decl_order.size());
        ASSERT_DECL_NAME(decl_order[0], namer.of("Payload"));
        ASSERT_DECL_NAME(decl_order[1], namer.of("Xunion"));
        ASSERT_DECL_NAME(decl_order[2], "SomeLongAnonymousPrefix0");
        ASSERT_DECL_NAME(decl_order[3], namer.of("Protocol"));
    }

    END_TEST;
}

bool nullable_xunion() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

xunion #Xunion# {
  request<#Protocol#> req;
  #Payload# foo;
};

protocol #Protocol# {
  SomeMethod(#Xunion#? req);
};

struct #Payload# {
  int32 a;
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(4, decl_order.size());
        ASSERT_DECL_NAME(decl_order[0], namer.of("Payload"));
        ASSERT_DECL_NAME(decl_order[1], namer.of("Xunion"));
        ASSERT_DECL_NAME(decl_order[2], "SomeLongAnonymousPrefix0");
        ASSERT_DECL_NAME(decl_order[3], namer.of("Protocol"));
    }

    END_TEST;
}

bool nonnullable_xunion_in_struct() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

struct #Payload# {
  int32 a;
};

protocol #Protocol# {
  SomeMethod(#Request# req);
};

struct #Request# {
  #Xunion# xu;
};

xunion #Xunion# {
  #Payload# foo;
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(5, decl_order.size());
        ASSERT_DECL_NAME(decl_order[0], namer.of("Payload"));
        ASSERT_DECL_NAME(decl_order[1], namer.of("Xunion"));
        ASSERT_DECL_NAME(decl_order[2], namer.of("Request"));
        ASSERT_DECL_NAME(decl_order[3], "SomeLongAnonymousPrefix0");
        ASSERT_DECL_NAME(decl_order[4], namer.of("Protocol"));
    }

    END_TEST;
}

bool nullable_xunion_in_struct() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        Namer namer;
        auto source = namer.mangle(R"FIDL(
library example;

struct #Payload# {
  int32 a;
};

protocol #Protocol# {
  SomeMethod(#Request# req);
};

struct #Request# {
  #Xunion#? xu;
};

xunion #Xunion# {
  #Payload# foo;
};

)FIDL");
        TestLibrary library(source);
        ASSERT_TRUE(library.Compile());
        auto decl_order = library.declaration_order();
        ASSERT_EQ(5, decl_order.size());
        ASSERT_DECL_NAME(decl_order[0], namer.of("Payload"));
        ASSERT_DECL_NAME(decl_order[1], namer.of("Xunion"));
        ASSERT_DECL_NAME(decl_order[2], namer.of("Request"));
        ASSERT_DECL_NAME(decl_order[3], "SomeLongAnonymousPrefix0");
        ASSERT_DECL_NAME(decl_order[4], namer.of("Protocol"));
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(declaration_order_test);
RUN_TEST(nonnullable_ref);
RUN_TEST(nullable_ref_breaks_dependency);
RUN_TEST(request_type_breaks_dependency_graph);
RUN_TEST(nonnullable_xunion);
RUN_TEST(nullable_xunion);
RUN_TEST(nonnullable_xunion_in_struct);
RUN_TEST(nullable_xunion_in_struct);
END_TEST_CASE(declaration_order_test);
