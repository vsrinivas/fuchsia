// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/fit/nullable.h>
#include <unittest/unittest.h>

namespace {

struct null_comparable_struct {
  int value = 0;

  constexpr bool operator==(decltype(nullptr)) const { return value == -1; }
};

struct nullable_struct {
  int value = 0;

  constexpr nullable_struct() = default;
  constexpr nullable_struct(int value) : value(value) {}
  constexpr explicit nullable_struct(decltype(nullptr)) : value(-1) {}
  constexpr nullable_struct(const nullable_struct& other) = default;
  constexpr nullable_struct(nullable_struct&& other) = default;

  constexpr int get() const { return value; }
  constexpr int increment() { return ++value; }

  constexpr bool operator==(decltype(nullptr)) const { return value == -1; }
  constexpr bool operator==(const nullable_struct& other) const { return value == other.value; }
  constexpr bool operator!=(const nullable_struct& other) const { return value != other.value; }

  constexpr nullable_struct& operator=(const nullable_struct& other) = default;
  constexpr nullable_struct& operator=(nullable_struct&& other) = default;
  constexpr nullable_struct& operator=(decltype(nullptr)) {
    value = -1;
    return *this;
  }
};

struct non_nullable_struct_missing_constructor {
  int value = 0;

  constexpr non_nullable_struct_missing_constructor() = default;
  constexpr non_nullable_struct_missing_constructor& operator=(decltype(nullptr)) {
    value = -1;
    return *this;
  }

  constexpr bool operator==(decltype(nullptr)) const { return value == -1; }
};

struct non_nullable_struct_missing_assignment {
  int value = 0;

  constexpr non_nullable_struct_missing_assignment() = default;
  constexpr explicit non_nullable_struct_missing_assignment(decltype(nullptr)) : value(-1) {}

  constexpr bool operator==(decltype(nullptr)) const { return value == -1; }
};

struct non_nullable_struct_missing_comparator {
  int value = 0;

  constexpr non_nullable_struct_missing_comparator() = default;
  constexpr explicit non_nullable_struct_missing_comparator(decltype(nullptr)) : value(-1) {}

  non_nullable_struct_missing_comparator& operator=(decltype(nullptr)) {
    value = -1;
    return *this;
  }
};

struct non_nullable_struct {
  int value = 0;

  constexpr non_nullable_struct() = default;
  constexpr non_nullable_struct(int value) : value(value) {}
  non_nullable_struct(const non_nullable_struct& other) = default;
  non_nullable_struct(non_nullable_struct&& other) = default;

  constexpr int get() const { return value; }
  constexpr int increment() { return ++value; }

  constexpr bool operator==(const non_nullable_struct& other) const { return value == other.value; }
  constexpr bool operator!=(const non_nullable_struct& other) const { return value != other.value; }

  non_nullable_struct& operator=(const non_nullable_struct& other) = default;
  non_nullable_struct& operator=(non_nullable_struct&& other) = default;
};

struct non_nullable_struct_with_non_bool_comparator {
  int value = 0;

  constexpr void operator==(decltype(nullptr)) const {}
};

// Some values of type void* that we can compare.
int item_a = 0;
int item_b = 0;
constexpr void* void_a = &item_a;
constexpr void* void_b = &item_b;
constexpr void* void_null = nullptr;

// Function and lambda types are never nullable.
void function(float, bool) {}
auto lambda = [](float, bool) { return 0; };

// Test is_comparable_with_null.
static_assert(!fit::is_comparable_with_null<void>::value, "");
static_assert(!fit::is_comparable_with_null<int>::value, "");
static_assert(!fit::is_comparable_with_null<non_nullable_struct>::value, "");
static_assert(!fit::is_comparable_with_null<non_nullable_struct_missing_comparator>::value, "");
static_assert(fit::is_comparable_with_null<decltype(nullptr)>::value, "");
static_assert(fit::is_comparable_with_null<void*>::value, "");
static_assert(fit::is_comparable_with_null<null_comparable_struct>::value, "");
static_assert(fit::is_comparable_with_null<nullable_struct>::value, "");
static_assert(fit::is_comparable_with_null<fit::nullable<int>>::value, "");
static_assert(fit::is_comparable_with_null<fit::nullable<void*>>::value, "");
static_assert(fit::is_comparable_with_null<std::unique_ptr<int>>::value, "");
static_assert(fit::is_comparable_with_null<decltype(function)>::value, "");
static_assert(fit::is_comparable_with_null<decltype(&function)>::value, "");
static_assert(fit::is_comparable_with_null<decltype(lambda)>::value, "");
static_assert(fit::is_comparable_with_null<decltype(&lambda)>::value, "");

// Test is_nullable.
static_assert(!fit::is_nullable<void>::value, "");
static_assert(!fit::is_nullable<int>::value, "");
static_assert(!fit::is_nullable<non_nullable_struct>::value, "");
static_assert(!fit::is_nullable<non_nullable_struct_missing_constructor>::value, "");
static_assert(!fit::is_nullable<non_nullable_struct_missing_assignment>::value, "");
static_assert(!fit::is_nullable<non_nullable_struct_missing_comparator>::value, "");
static_assert(!fit::is_nullable<null_comparable_struct>::value, "");
static_assert(fit::is_nullable<decltype(nullptr)>::value, "");
static_assert(fit::is_nullable<void*>::value, "");
static_assert(fit::is_nullable<nullable_struct>::value, "");
static_assert(fit::is_nullable<fit::nullable<int>>::value, "");
static_assert(fit::is_nullable<fit::nullable<void*>>::value, "");
static_assert(fit::is_nullable<std::unique_ptr<int>>::value, "");
static_assert(!fit::is_nullable<decltype(function)>::value, "");
static_assert(fit::is_nullable<decltype(&function)>::value, "");
static_assert(!fit::is_nullable<decltype(lambda)>::value, "");
static_assert(fit::is_nullable<decltype(&lambda)>::value, "");

// Test is_null.
static_assert(fit::is_null(nullptr), "");
static_assert(fit::is_null(void_null), "");
static_assert(fit::is_null(fit::nullable<void*>(nullptr)), "");
static_assert(!fit::is_null(void_a), "");
static_assert(!fit::is_null(fit::nullable<void*>(void_a)), "");
static_assert(!fit::is_null(5), "");
static_assert(!fit::is_null(function), "");
static_assert(!fit::is_null(&function), "");
static_assert(!fit::is_null(lambda), "");
static_assert(!fit::is_null(&lambda), "");

// Test nullable::value_type.
static_assert(std::is_same<int, fit::nullable<int>::value_type>::value, "");
static_assert(std::is_same<void*, fit::nullable<void*>::value_type>::value, "");
static_assert(
    std::is_same<decltype(&function), fit::nullable<decltype(&function)>::value_type>::value, "");

// Test constexpr comparators for nullable.
static_assert(!fit::nullable<void*>(), "");
static_assert(!fit::nullable<void*>().has_value(), "");
static_assert(fit::nullable<void*>() == nullptr, "");
static_assert(!fit::nullable<void*>(nullptr), "");
static_assert(!fit::nullable<void*>(nullptr).has_value(), "");
static_assert(!fit::nullable<void*>(void_null), "");
static_assert(!fit::nullable<void*>(void_null).has_value(), "");
static_assert(fit::nullable<void*>(void_a), "");
static_assert(fit::nullable<void*>(void_a).has_value(), "");

static_assert(fit::nullable<void*>(void_null) == nullptr, "");
static_assert(nullptr == fit::nullable<void*>(void_null), "");
static_assert(!(fit::nullable<void*>(void_a) == nullptr), "");
static_assert(!(nullptr == fit::nullable<void*>(void_a)), "");

static_assert(!(fit::nullable<void*>(void_null) != nullptr), "");
static_assert(!(nullptr != fit::nullable<void*>(void_null)), "");
static_assert(fit::nullable<void*>(void_a) != nullptr, "");
static_assert(nullptr != fit::nullable<void*>(void_a), "");

static_assert(fit::nullable<void*>(void_a) == fit::nullable<void*>(void_a), "");
static_assert(fit::nullable<void*>(void_null) == fit::nullable<void*>(void_null), "");
static_assert(!(fit::nullable<void*>(void_a) == fit::nullable<void*>(void_b)), "");
static_assert(!(fit::nullable<void*>(void_a) == fit::nullable<void*>(void_null)), "");
static_assert(!(fit::nullable<void*>(void_null) == fit::nullable<void*>(void_a)), "");

static_assert(!(fit::nullable<void*>(void_a) != fit::nullable<void*>(void_a)), "");
static_assert(!(fit::nullable<void*>(void_null) != fit::nullable<void*>(void_null)), "");
static_assert(fit::nullable<void*>(void_a) != fit::nullable<void*>(void_b), "");
static_assert(fit::nullable<void*>(void_a) != fit::nullable<void*>(void_null), "");
static_assert(fit::nullable<void*>(void_null) != fit::nullable<void*>(void_a), "");

static_assert(fit::nullable<void*>(void_a) == void_a, "");
static_assert(fit::nullable<void*>(void_null) == void_null, "");
static_assert(!(fit::nullable<void*>(void_a) == void_b), "");
static_assert(!(fit::nullable<void*>(void_a) == void_null), "");
static_assert(!(fit::nullable<void*>(void_null) == void_a), "");

static_assert(void_a == fit::nullable<void*>(void_a), "");
static_assert(void_null == fit::nullable<void*>(void_null), "");
static_assert(!(void_a == fit::nullable<void*>(void_b)), "");
static_assert(!(void_a == fit::nullable<void*>(void_null)), "");
static_assert(!(void_null == fit::nullable<void*>(void_a)), "");

static_assert(!(fit::nullable<void*>(void_a) != void_a), "");
static_assert(!(fit::nullable<void*>(void_null) != void_null), "");
static_assert(fit::nullable<void*>(void_a) != void_b, "");
static_assert(fit::nullable<void*>(void_a) != void_null, "");
static_assert(fit::nullable<void*>(void_null) != void_a, "");

static_assert(!(void_a != fit::nullable<void*>(void_a)), "");
static_assert(!(void_null != fit::nullable<void*>(void_null)), "");
static_assert(void_a != fit::nullable<void*>(void_b), "");
static_assert(void_a != fit::nullable<void*>(void_null), "");
static_assert(void_null != fit::nullable<void*>(void_a), "");

static_assert(fit::nullable<nullable_struct>{nullptr}.has_value() == false, "");
static_assert(fit::nullable<nullable_struct>{1}.has_value() == true, "");
static_assert(fit::nullable<nullable_struct>{nullptr} == fit::nullable<nullable_struct>{nullptr},
              "");
static_assert(fit::nullable<nullable_struct>{1} == fit::nullable<nullable_struct>{1}, "");
static_assert(fit::nullable<nullable_struct>{nullptr} != fit::nullable<nullable_struct>{1}, "");
static_assert(fit::nullable<nullable_struct>{1} != fit::nullable<nullable_struct>{nullptr}, "");
static_assert(fit::nullable<nullable_struct>{2} != fit::nullable<nullable_struct>{1}, "");
static_assert(fit::nullable<nullable_struct>{1} != fit::nullable<nullable_struct>{2}, "");
static_assert(fit::nullable<nullable_struct>{nullptr} != nullable_struct{1}, "");
static_assert(fit::nullable<nullable_struct>{1} == nullable_struct{1}, "");
static_assert(fit::nullable<nullable_struct>{2} != nullable_struct{1}, "");
static_assert(nullable_struct{1} != fit::nullable<nullable_struct>{nullptr}, "");
static_assert(nullable_struct{1} == fit::nullable<nullable_struct>{1}, "");
static_assert(nullable_struct{1} != fit::nullable<nullable_struct>{2}, "");
static_assert(fit::nullable<nullable_struct>{1}.value() == nullable_struct{1}, "");
static_assert(fit::nullable<nullable_struct>{2}.value() != nullable_struct{1}, "");

bool is_null() {
  BEGIN_TEST;

  EXPECT_TRUE(fit::is_null(nullptr));

  null_comparable_struct ncf;
  EXPECT_FALSE(fit::is_null(ncf));

  null_comparable_struct nct{-1};
  EXPECT_TRUE(fit::is_null(nct));

  nullable_struct nf;
  EXPECT_FALSE(fit::is_null(nf));

  nullable_struct nt(nullptr);
  EXPECT_TRUE(fit::is_null(nt));

  fit::nullable<int> nif(1);
  EXPECT_FALSE(fit::is_null(nif));

  fit::nullable<int> nit(nullptr);
  EXPECT_TRUE(fit::is_null(nit));

  fit::nullable<void*> npf(&nit);
  EXPECT_FALSE(fit::is_null(npf));

  fit::nullable<void*> npt(nullptr);
  EXPECT_TRUE(fit::is_null(npt));

  non_nullable_struct nn;
  EXPECT_FALSE(fit::is_null(nn));

  non_nullable_struct_with_non_bool_comparator nbn;
  EXPECT_FALSE(fit::is_null(nbn));

  END_TEST;
}

template <typename T>
struct traits;

template <>
struct traits<nullable_struct> {
  static constexpr nullable_struct a{42};
  static constexpr nullable_struct b{55};
  static constexpr nullable_struct null{-1};
};

template <>
struct traits<non_nullable_struct> {
  static constexpr non_nullable_struct a{42};
  static constexpr non_nullable_struct b{55};
  static constexpr nullptr_t null = nullptr;
};

template <typename T>
bool construct_without_value() {
  BEGIN_TEST;

  fit::nullable<T> opt;
  EXPECT_FALSE(opt.has_value());
  EXPECT_FALSE(!!opt);

  EXPECT_EQ(42, opt.value_or(traits<T>::a).value);

  opt.reset();
  EXPECT_FALSE(opt.has_value());

  END_TEST;
}

template <typename T>
bool construct_with_value() {
  BEGIN_TEST;

  fit::nullable<T> opt(traits<T>::a);
  EXPECT_TRUE(opt.has_value());
  EXPECT_TRUE(!!opt);

  EXPECT_EQ(42, opt.value().value);
  EXPECT_EQ(42, opt.value_or(traits<T>::b).value);

  EXPECT_EQ(42, opt->get());
  EXPECT_EQ(43, opt->increment());
  EXPECT_EQ(43, opt->get());

  opt.reset();
  EXPECT_FALSE(opt.has_value());

  END_TEST;
}

template <typename T>
bool construct_copy() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  fit::nullable<T> b(a);
  fit::nullable<T> c;
  fit::nullable<T> d(c);
  fit::nullable<T> e(traits<T>::null);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(d.has_value());
  EXPECT_FALSE(e.has_value());

  END_TEST;
}

template <typename T>
bool construct_move() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  fit::nullable<T> b(std::move(a));
  fit::nullable<T> c;
  fit::nullable<T> d(std::move(c));
  EXPECT_TRUE(a.has_value());
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(d.has_value());

  END_TEST;
}

template <typename T>
bool accessors() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  T& value = a.value();
  EXPECT_EQ(42, value.value);

  const T& const_value = const_cast<const decltype(a)&>(a).value();
  EXPECT_EQ(42, const_value.value);

  T rvalue = fit::nullable<T>(traits<T>::a).value();
  EXPECT_EQ(42, rvalue.value);

  T const_rvalue = const_cast<const fit::nullable<T>&&>(fit::nullable<T>(traits<T>::a)).value();
  EXPECT_EQ(42, const_rvalue.value);

  END_TEST;
}

template <typename T>
bool assign() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);

  a = traits<T>::b;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);

  a.reset();
  EXPECT_FALSE(a.has_value());

  a = traits<T>::a;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);

  a = nullptr;
  EXPECT_FALSE(a.has_value());

  a = traits<T>::a;
  a = traits<T>::null;
  EXPECT_FALSE(a.has_value());

  END_TEST;
}

template <typename T>
bool assign_copy() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  fit::nullable<T> b(traits<T>::b);
  fit::nullable<T> c;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_FALSE(c.has_value());

  a = b;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);

  b = c;
  EXPECT_FALSE(b.has_value());
  EXPECT_FALSE(c.has_value());

  b = a;
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, b.value().value);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

  b = b;
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);

  c = c;
  EXPECT_FALSE(c.has_value());

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  b = traits<T>::null;
  EXPECT_FALSE(b.has_value());

  END_TEST;
}

template <typename T>
bool assign_move() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  fit::nullable<T> b(traits<T>::b);
  fit::nullable<T> c;
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(42, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_FALSE(c.has_value());

  a = std::move(b);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);
  EXPECT_TRUE(b.has_value());

  b = std::move(c);
  EXPECT_FALSE(b.has_value());
  EXPECT_FALSE(c.has_value());

  c = std::move(b);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(b.has_value());

  b = std::move(a);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);
  EXPECT_TRUE(a.has_value());

  b = std::move(b);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(55, b.value().value);

  a = std::move(a);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);

  c = std::move(c);
  EXPECT_FALSE(c.has_value());

  END_TEST;
}

template <typename T>
bool invoke() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  EXPECT_EQ(42, a->get());
  EXPECT_EQ(43, a->increment());
  EXPECT_EQ(43, (*a).value);

  END_TEST;
}

template <typename T>
bool comparisons() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  fit::nullable<T> b(traits<T>::b);
  fit::nullable<T> c(traits<T>::a);
  fit::nullable<T> d;
  fit::nullable<T> e(traits<T>::null);

  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == d);
  EXPECT_TRUE(d == e);
  EXPECT_FALSE(d == a);

  EXPECT_FALSE(a == nullptr);
  EXPECT_FALSE(nullptr == a);
  EXPECT_TRUE(a == traits<T>::a);
  EXPECT_TRUE(traits<T>::a == a);
  EXPECT_FALSE(a == traits<T>::b);
  EXPECT_FALSE(traits<T>::b == a);
  EXPECT_FALSE(d == traits<T>::a);
  EXPECT_FALSE(traits<T>::b == d);
  EXPECT_TRUE(d == nullptr);
  EXPECT_TRUE(nullptr == d);

  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a != c);
  EXPECT_TRUE(a != d);
  EXPECT_FALSE(d != e);
  EXPECT_TRUE(d != a);

  EXPECT_TRUE(a != nullptr);
  EXPECT_TRUE(nullptr != a);
  EXPECT_FALSE(a != traits<T>::a);
  EXPECT_FALSE(traits<T>::a != a);
  EXPECT_TRUE(a != traits<T>::b);
  EXPECT_TRUE(traits<T>::b != a);
  EXPECT_TRUE(d != traits<T>::a);
  EXPECT_TRUE(traits<T>::a != d);
  EXPECT_FALSE(d != nullptr);
  EXPECT_FALSE(nullptr != d);

  END_TEST;
}

template <typename T>
bool swapping() {
  BEGIN_TEST;

  fit::nullable<T> a(traits<T>::a);
  fit::nullable<T> b(traits<T>::b);
  fit::nullable<T> c;
  fit::nullable<T> d;

  swap(a, b);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(55, a.value().value);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(42, b.value().value);

  swap(a, c);
  EXPECT_FALSE(a.has_value());
  EXPECT_TRUE(c.has_value());
  EXPECT_EQ(55, c.value().value);

  swap(d, c);
  EXPECT_FALSE(c.has_value());
  EXPECT_TRUE(d.has_value());
  EXPECT_EQ(55, d.value().value);

  swap(c, a);
  EXPECT_FALSE(c.has_value());
  EXPECT_FALSE(a.has_value());

  swap(a, a);
  EXPECT_FALSE(a.has_value());

  swap(d, d);
  EXPECT_TRUE(d.has_value());
  EXPECT_EQ(55, d.value().value);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(nullable_tests)
RUN_TEST(is_null)
RUN_TEST(construct_without_value<nullable_struct>)
RUN_TEST(construct_without_value<non_nullable_struct>)
RUN_TEST(construct_with_value<nullable_struct>)
RUN_TEST(construct_with_value<non_nullable_struct>)
RUN_TEST(construct_copy<nullable_struct>)
RUN_TEST(construct_copy<non_nullable_struct>)
RUN_TEST(construct_move<nullable_struct>)
RUN_TEST(construct_move<non_nullable_struct>)
RUN_TEST(accessors<nullable_struct>)
RUN_TEST(accessors<non_nullable_struct>)
RUN_TEST(assign<nullable_struct>)
RUN_TEST(assign<non_nullable_struct>)
RUN_TEST(assign_copy<nullable_struct>)
RUN_TEST(assign_copy<non_nullable_struct>)
RUN_TEST(assign_move<nullable_struct>)
RUN_TEST(assign_move<non_nullable_struct>)
RUN_TEST(invoke<nullable_struct>)
RUN_TEST(invoke<non_nullable_struct>)
RUN_TEST(comparisons<nullable_struct>)
RUN_TEST(comparisons<non_nullable_struct>)
RUN_TEST(swapping<nullable_struct>)
RUN_TEST(swapping<non_nullable_struct>)
END_TEST_CASE(nullable_tests)
