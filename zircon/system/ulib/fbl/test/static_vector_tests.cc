// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <fbl/static_vector.h>
#include <zxtest/zxtest.h>

namespace {

class Obj;

static constexpr int kSize = 5;
static constexpr int kDefaultValue = 42;
static std::set<Obj*> global_constructed_objects;
static int dtor_count = 0;

class Obj {
 public:
  ~Obj() {
    dtor_count++;
    global_constructed_objects.erase(this);
  }

  Obj() { AssertDoesNotExist(); }
  explicit Obj(int v) : value_(v) { AssertDoesNotExist(); }
  Obj(const Obj& rhs) {
    AssertDoesNotExist();
    *this = rhs;
  }
  Obj(Obj&& rhs) {
    AssertDoesNotExist();
    *this = std::move(rhs);
  }

  Obj& operator=(const Obj& rhs) = default;
  Obj& operator=(Obj&& rhs) {
    value_ = rhs.value_;
    rhs.value_ = 0;
    return *this;
  }

  int value() const { return value_; }
  bool operator==(const Obj& rhs) { return rhs.value_ == value_; }
  bool operator!=(const Obj& rhs) { return rhs.value_ == value_; }

 private:
  void AssertDoesNotExist() {
    ASSERT_TRUE(global_constructed_objects.count(this) == 0 && "object constructed twice?");
    global_constructed_objects.insert(this);
  }
  int value_ = kDefaultValue;
};

static constexpr fbl::static_vector<int, kSize> MakeConstexprWithPushPop() {
  fbl::static_vector<int, kSize> v;
  v.push_back(0);
  v.push_back(1);
  v.push_back(9);
  v.pop_back();
  v.push_back(2);
  return v;
}

TEST(StaticVectorTest, ZeroCapacity) {
  static_assert(std::is_empty_v<fbl::static_vector<int, 0>>);
  static_assert(std::is_empty_v<fbl::static_vector<double, 0>>);

  fbl::static_vector<int, 0> v;
  EXPECT_EQ(v.size(), 0);
}

TEST(StaticVectorTest, ConstexprFromEmpty) {
  constexpr fbl::static_vector<int, kSize> v;
  static_assert(v.size() == 0);
}

TEST(StaticVectorTest, ConstexprFromDefaultValues) {
  constexpr fbl::static_vector<int, kSize> v(3);
  static_assert(v.size() == 3);
  static_assert(v[0] == 0);
  static_assert(v[1] == 0);
  static_assert(v[2] == 0);
}

TEST(StaticVectorTest, ConstexprFromInitializerList) {
  constexpr fbl::static_vector<int, kSize> v{0, 1, 2};
  static_assert(v.size() == 3);
  static_assert(v[0] == 0);
  static_assert(v[1] == 1);
  static_assert(v[2] == 2);
}

TEST(StaticVectorTest, ConstexprFromFunctionWithPushPop) {
  constexpr auto v = MakeConstexprWithPushPop();
  static_assert(v.size() == 3);
  static_assert(v[0] == 0);
  static_assert(v[1] == 1);
  static_assert(v[2] == 2);
}

TEST(StaticVectorTest, ConstexprIterators) {
  constexpr fbl::static_vector<int, kSize> v{0, 1, 2, 3, 4};
  static_assert(v.size() == 5);

  static_assert(v.begin() + 5 == v.end());
  static_assert(v.cbegin() + 5 == v.cend());
  static_assert(*(v.begin() + 0) == 0);
  static_assert(*(v.begin() + 1) == 1);
  static_assert(*(v.cbegin() + 0) == 0);
  static_assert(*(v.cbegin() + 1) == 1);

  static_assert(v.rbegin() + 5 == v.rend());
  static_assert(v.crbegin() + 5 == v.crend());
  static_assert(*(v.rbegin() + 0) == 4);
  static_assert(*(v.rbegin() + 1) == 3);
  static_assert(*(v.crbegin() + 0) == 4);
  static_assert(*(v.crbegin() + 1) == 3);
}

TEST(StaticVectorTest, DefaultCtorIsEmpty) {
  fbl::static_vector<Obj, kSize> v;
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0);
}

TEST(StaticVectorTest, CtorFromDefaultValues) {
  {
    fbl::static_vector<Obj, kSize> v(0);
    EXPECT_EQ(v.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v(3);
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), kDefaultValue);
    EXPECT_EQ(v[1].value(), kDefaultValue);
    EXPECT_EQ(v[2].value(), kDefaultValue);
  }
  {
    fbl::static_vector<Obj, kSize> v(5);
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), kDefaultValue);
    EXPECT_EQ(v[1].value(), kDefaultValue);
    EXPECT_EQ(v[2].value(), kDefaultValue);
    EXPECT_EQ(v[3].value(), kDefaultValue);
    EXPECT_EQ(v[4].value(), kDefaultValue);
  }
}

TEST(StaticVectorTest, CtorFromCopiedValue) {
  {
    fbl::static_vector<Obj, kSize> v(0, Obj(9));
    EXPECT_EQ(v.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v(3, Obj(9));
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 9);
    EXPECT_EQ(v[1].value(), 9);
    EXPECT_EQ(v[2].value(), 9);
  }
  {
    fbl::static_vector<Obj, kSize> v(5, Obj(9));
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 9);
    EXPECT_EQ(v[1].value(), 9);
    EXPECT_EQ(v[2].value(), 9);
    EXPECT_EQ(v[3].value(), 9);
    EXPECT_EQ(v[4].value(), 9);
  }
}

TEST(StaticVectorTest, CtorFromIterator) {
  {
    std::vector<Obj> in;
    fbl::static_vector<Obj, kSize> v(in.begin(), in.end());
    EXPECT_EQ(v.size(), 0);
  }
  {
    std::vector<Obj> in{Obj(0), Obj(1), Obj(2)};
    fbl::static_vector<Obj, kSize> v(in.begin(), in.end());
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
  }
  {
    std::vector<Obj> in{Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)};
    fbl::static_vector<Obj, kSize> v(in.begin(), in.end());
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
    EXPECT_EQ(v[3].value(), 3);
    EXPECT_EQ(v[4].value(), 4);
  }
}

TEST(StaticVectorTest, CtorFromInitializerList) {
  {
    fbl::static_vector<Obj, kSize> v{};
    EXPECT_EQ(v.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2)};
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)};
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
    EXPECT_EQ(v[3].value(), 3);
    EXPECT_EQ(v[4].value(), 4);
  }
}

TEST(StaticVectorTest, AssignOpCopy) {
  {
    fbl::static_vector<Obj, kSize> v;
    fbl::static_vector<Obj, kSize> rhs{Obj(0), Obj(1), Obj(2)};
    v = rhs;
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
    ASSERT_EQ(rhs.size(), 3);
    EXPECT_EQ(rhs[0].value(), 0);
    EXPECT_EQ(rhs[1].value(), 1);
    EXPECT_EQ(rhs[2].value(), 2);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2)};
    fbl::static_vector<Obj, kSize> rhs;
    v = rhs;
    ASSERT_EQ(v.size(), 0);
    ASSERT_EQ(rhs.size(), 0);
  }
}

TEST(StaticVectorTest, AssignOpMove) {
  {
    fbl::static_vector<Obj, kSize> v(4, Obj(9));
    fbl::static_vector<Obj, kSize> rhs(3, Obj(9));
    dtor_count = 0;
    v = std::move(rhs);
    EXPECT_EQ(dtor_count, 4);  // clear v before the move
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 9);
    EXPECT_EQ(v[1].value(), 9);
    EXPECT_EQ(v[2].value(), 9);
    ASSERT_EQ(rhs.size(), 3);
    EXPECT_EQ(rhs[0].value(), 0);  // Obj's move ctor sets rhs.value = 0
    EXPECT_EQ(rhs[1].value(), 0);
    EXPECT_EQ(rhs[2].value(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v(4, Obj(9));
    fbl::static_vector<Obj, kSize> rhs;
    dtor_count = 0;
    v = rhs;
    EXPECT_EQ(dtor_count, 4);
    ASSERT_EQ(v.size(), 0);
    ASSERT_EQ(rhs.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v;
    fbl::static_vector<Obj, kSize> rhs(3, Obj(9));
    dtor_count = 0;
    v = std::move(rhs);
    EXPECT_EQ(dtor_count, 0);
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 9);
    EXPECT_EQ(v[1].value(), 9);
    EXPECT_EQ(v[2].value(), 9);
    ASSERT_EQ(rhs.size(), 3);
    EXPECT_EQ(rhs[0].value(), 0);  // Obj's move ctor sets rhs.value = 0
    EXPECT_EQ(rhs[1].value(), 0);
    EXPECT_EQ(rhs[2].value(), 0);
  }
}

TEST(StaticVectorTest, AssignFromIterator) {
  {
    std::vector<Obj> in;
    fbl::static_vector<Obj, kSize> v;
    v.assign(in.begin(), in.end());
    EXPECT_EQ(v.size(), 0);
  }
  {
    std::vector<Obj> in{Obj(0), Obj(1), Obj(2)};
    fbl::static_vector<Obj, kSize> v;
    v.assign(in.begin(), in.end());
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
  }
  {
    std::vector<Obj> in{Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)};
    fbl::static_vector<Obj, kSize> v;
    v.assign(in.begin(), in.end());
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
    EXPECT_EQ(v[3].value(), 3);
    EXPECT_EQ(v[4].value(), 4);
  }
}

TEST(StaticVectorTest, AssignFromCopiedValue) {
  {
    fbl::static_vector<Obj, kSize> v;
    v.assign(0, Obj(9));
    EXPECT_EQ(v.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v;
    v.assign(3, Obj(9));
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 9);
    EXPECT_EQ(v[1].value(), 9);
    EXPECT_EQ(v[2].value(), 9);
  }
  {
    fbl::static_vector<Obj, kSize> v;
    v.assign(5, Obj(9));
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 9);
    EXPECT_EQ(v[1].value(), 9);
    EXPECT_EQ(v[2].value(), 9);
    EXPECT_EQ(v[3].value(), 9);
    EXPECT_EQ(v[4].value(), 9);
  }
}

TEST(StaticVectorTest, AssignFromInitializerList) {
  {
    fbl::static_vector<Obj, kSize> v(3);
    v.assign({});
    EXPECT_EQ(v.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v;
    v.assign({Obj(0), Obj(1), Obj(2)});
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
  }
  {
    fbl::static_vector<Obj, kSize> v;
    v.assign({Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)});
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 2);
    EXPECT_EQ(v[3].value(), 3);
    EXPECT_EQ(v[4].value(), 4);
  }
}

TEST(StaticVectorTest, Iterators) {
  {
    fbl::static_vector<Obj, kSize> v{};
    ASSERT_EQ(v.begin(), v.end());
    ASSERT_EQ(v.rbegin(), v.rend());
    ASSERT_EQ(v.cbegin(), v.cend());
    ASSERT_EQ(v.crbegin(), v.crend());
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0)};
    ASSERT_EQ(v.begin() + 1, v.end());
    ASSERT_EQ(v.cbegin() + 1, v.cend());
    EXPECT_EQ(v.begin()->value(), 0);
    EXPECT_EQ(v.cbegin()->value(), 0);

    ASSERT_EQ(v.rbegin() + 1, v.rend());
    ASSERT_EQ(v.crbegin() + 1, v.crend());
    EXPECT_EQ(v.rbegin()->value(), 0);
    EXPECT_EQ(v.crbegin()->value(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)};
    ASSERT_EQ(v.begin() + 5, v.end());
    ASSERT_EQ(v.cbegin() + 5, v.cend());
    EXPECT_EQ((v.begin() + 0)->value(), 0);
    EXPECT_EQ((v.begin() + 1)->value(), 1);
    EXPECT_EQ((v.cbegin() + 0)->value(), 0);
    EXPECT_EQ((v.cbegin() + 1)->value(), 1);

    ASSERT_EQ(v.rbegin() + 5, v.rend());
    ASSERT_EQ(v.crbegin() + 5, v.crend());
    EXPECT_EQ((v.rbegin() + 0)->value(), 4);
    EXPECT_EQ((v.rbegin() + 1)->value(), 3);
    EXPECT_EQ((v.crbegin() + 0)->value(), 4);
    EXPECT_EQ((v.crbegin() + 1)->value(), 3);
  }
}

TEST(StaticVectorTest, Empty) {
  {
    fbl::static_vector<Obj, kSize> v(0);
    EXPECT_EQ(v.size(), 0);
    EXPECT_TRUE(v.empty());
  }
  {
    fbl::static_vector<Obj, kSize> v(3);
    EXPECT_EQ(v.size(), 3);
    EXPECT_FALSE(v.empty());
  }
}

TEST(StaticVectorTest, StaticMethods) {
  using T = fbl::static_vector<Obj, kSize>;
  EXPECT_EQ(T::max_size(), kSize);
  EXPECT_EQ(T::capacity(), kSize);
}

TEST(StaticVectorTest, Resize) {
  // Size gets bigger.
  {
    fbl::static_vector<Obj, kSize> v{};
    dtor_count = 0;
    v.resize(1);
    EXPECT_EQ(dtor_count, 0);
    ASSERT_EQ(v.size(), 1);
    EXPECT_EQ(v[0].value(), kDefaultValue);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1)};
    dtor_count = 0;
    v.resize(5);
    EXPECT_EQ(dtor_count, 0);
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), kDefaultValue);
    EXPECT_EQ(v[3].value(), kDefaultValue);
    EXPECT_EQ(v[4].value(), kDefaultValue);
  }

  // Size gets smaller.
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1)};
    dtor_count = 0;
    v.resize(1);
    EXPECT_EQ(dtor_count, 1);
    ASSERT_EQ(v.size(), 1);
    EXPECT_EQ(v[0].value(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1)};
    dtor_count = 0;
    v.resize(0);
    EXPECT_EQ(dtor_count, 2);
    ASSERT_EQ(v.size(), 0);
  }
}

TEST(StaticVectorTest, ResizeWithDefaultValue) {
  Obj obj(9);

  // Size gets bigger.
  {
    fbl::static_vector<Obj, kSize> v{};
    dtor_count = 0;
    v.resize(1, obj);
    EXPECT_EQ(dtor_count, 0);
    ASSERT_EQ(v.size(), 1);
    EXPECT_EQ(v[0].value(), 9);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1)};
    dtor_count = 0;
    v.resize(5, obj);
    EXPECT_EQ(dtor_count, 0);
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v[0].value(), 0);
    EXPECT_EQ(v[1].value(), 1);
    EXPECT_EQ(v[2].value(), 9);
    EXPECT_EQ(v[3].value(), 9);
    EXPECT_EQ(v[4].value(), 9);
  }

  // Size gets smaller.
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1)};
    dtor_count = 0;
    v.resize(1, obj);
    EXPECT_EQ(dtor_count, 1);
    ASSERT_EQ(v.size(), 1);
    EXPECT_EQ(v[0].value(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1)};
    dtor_count = 0;
    v.resize(0, obj);
    EXPECT_EQ(dtor_count, 2);
    ASSERT_EQ(v.size(), 0);
  }
}

TEST(StaticVectorTest, Indexing) {
  fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2)};
  ASSERT_EQ(v.size(), 3);
  EXPECT_EQ(v[0].value(), 0);
  EXPECT_EQ(v[1].value(), 1);
  EXPECT_EQ(v[2].value(), 2);
}

TEST(StaticVectorTest, FrontBack) {
  {
    fbl::static_vector<Obj, kSize> v{Obj(0)};
    ASSERT_EQ(v.size(), 1);
    EXPECT_EQ(v.front().value(), 0);
    EXPECT_EQ(v.back().value(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2)};
    ASSERT_EQ(v.size(), 3);
    EXPECT_EQ(v.front().value(), 0);
    EXPECT_EQ(v.back().value(), 2);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)};
    ASSERT_EQ(v.size(), 5);
    EXPECT_EQ(v.front().value(), 0);
    EXPECT_EQ(v.back().value(), 4);
  }
}

TEST(StaticVectorTest, Data) {
  fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2)};
  ASSERT_EQ(v.size(), 3);
  EXPECT_EQ(v.data(), &v[0]);
}

TEST(StaticVectorTest, PushFromCopy) {
  fbl::static_vector<Obj, kSize> v{};
  v.push_back(Obj(9));
  ASSERT_EQ(v.size(), 1);
  EXPECT_EQ(v[0].value(), 9);
}

TEST(StaticVectorTest, PushFromMove) {
  Obj obj(9);

  fbl::static_vector<Obj, kSize> v{};
  v.push_back(std::move(obj));
  EXPECT_EQ(obj.value(), 0);
  ASSERT_EQ(v.size(), 1);
  EXPECT_EQ(v[0].value(), 9);
}

TEST(StaticVectorTest, Pop) {
  fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2)};
  dtor_count = 0;
  v.pop_back();
  EXPECT_EQ(dtor_count, 1);
  ASSERT_EQ(v.size(), 2);
  EXPECT_EQ(v[0].value(), 0);
  EXPECT_EQ(v[1].value(), 1);
}

TEST(StaticVectorTest, Clear) {
  {
    fbl::static_vector<Obj, kSize> v{};
    dtor_count = 0;
    v.clear();
    EXPECT_EQ(dtor_count, 0);
    EXPECT_EQ(v.size(), 0);
  }
  {
    fbl::static_vector<Obj, kSize> v{Obj(0), Obj(1), Obj(2), Obj(3), Obj(4)};
    dtor_count = 0;
    v.clear();
    EXPECT_EQ(dtor_count, 5);
    EXPECT_EQ(v.size(), 0);
  }
}

}  // namespace
