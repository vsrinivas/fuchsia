// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/type_support.h>

// match_cv tests:
static_assert(mxtl::is_same<mxtl::match_cv<int, void>::type, void>::value, "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<const int, void>::type, const void>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<volatile void, char>::type, volatile char>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<const int, const char>::type, const char>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<const int, volatile char>::type, const char>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<char, const volatile void>::type, void>::value,
              "wrong type");


// is_class tests
namespace is_class_tests {
static_assert(!mxtl::is_class<int>::value, "'int' should not pass the is_class<> test!");

class A { };
static_assert(mxtl::is_class<A>::value, "'class' should pass the is_class<> test!");

struct B { };
static_assert(mxtl::is_class<B>::value, "'struct' should pass the is_class<> test!");

union C { int a; float b; };
static_assert(!mxtl::is_class<C>::value, "'union' should not pass the is_class<> test!");

enum D { D_ENUM_VALUE };
static_assert(!mxtl::is_class<D>::value, "'enum' should not pass the is_class<> test!");

enum class E { VALUE };
static_assert(!mxtl::is_class<E>::value, "'enum class' should not pass the is_class<> test!");
}  // namespace is_class_tests

// is_base_of tests
namespace is_base_of_tests {

static_assert(!mxtl::is_base_of<int, int>::value,
              "scalar types should not be bases of scalar types");

class A { };
static_assert(mxtl::is_base_of<A, A>::value, "A should be a base of A!");

class B : public A { };
static_assert( mxtl::is_base_of<B, B>::value, "B should be a base of B!");
static_assert( mxtl::is_base_of<A, B>::value, "A should be a base of B!");
static_assert(!mxtl::is_base_of<B, A>::value, "B should not be a base of A!");

class C : public B { };
static_assert( mxtl::is_base_of<C, C>::value, "C should be a base of C!");
static_assert( mxtl::is_base_of<B, C>::value, "B should be a base of C!");
static_assert( mxtl::is_base_of<A, C>::value, "A should be a base of C!");
static_assert(!mxtl::is_base_of<C, B>::value, "C should not be a base of B!");
static_assert(!mxtl::is_base_of<C, A>::value, "C should not be a base of A!");

class D { };
class E : public B, public D { };
static_assert( mxtl::is_base_of<D, D>::value, "D should be a base of D!");
static_assert( mxtl::is_base_of<E, E>::value, "E should be a base of E!");
static_assert( mxtl::is_base_of<A, E>::value, "A should be a base of E!");
static_assert( mxtl::is_base_of<B, E>::value, "B should be a base of E!");
static_assert(!mxtl::is_base_of<C, E>::value, "C should not be a base of E!");
static_assert( mxtl::is_base_of<D, E>::value, "D should be a base of E!");
static_assert(!mxtl::is_base_of<E, A>::value, "E should not be a base of A!");
static_assert(!mxtl::is_base_of<E, B>::value, "E should not be a base of B!");
static_assert(!mxtl::is_base_of<E, C>::value, "E should not be a base of C!");
static_assert(!mxtl::is_base_of<E, D>::value, "E should not be a base of D!");

struct sA { };
static_assert(mxtl::is_base_of<sA, sA>::value, "sA should be a base of sA!");

struct sB : public sA { };
static_assert( mxtl::is_base_of<sB, sB>::value, "sB should be a base of sB!");
static_assert( mxtl::is_base_of<sA, sB>::value, "sA should be a base of sB!");
static_assert(!mxtl::is_base_of<sB, sA>::value, "sB should not be a base of sA!");

struct sC : public sB { };
static_assert( mxtl::is_base_of<sC, sC>::value, "sC should be a base of sC!");
static_assert( mxtl::is_base_of<sB, sC>::value, "sB should be a base of sC!");
static_assert( mxtl::is_base_of<sA, sC>::value, "sA should be a base of sC!");
static_assert(!mxtl::is_base_of<sC, sB>::value, "sC should not be a base of sB!");
static_assert(!mxtl::is_base_of<sC, sA>::value, "sC should not be a base of sA!");

struct sD { };
struct sE : public sB, public sD { };
static_assert( mxtl::is_base_of<sD, sD>::value, "sD should be a base of sD!");
static_assert( mxtl::is_base_of<sE, sE>::value, "sE should be a base of sE!");
static_assert( mxtl::is_base_of<sA, sE>::value, "sA should be a base of sE!");
static_assert( mxtl::is_base_of<sB, sE>::value, "sB should be a base of sE!");
static_assert(!mxtl::is_base_of<sC, sE>::value, "sC should not be a base of sE!");
static_assert( mxtl::is_base_of<sD, sE>::value, "sD should be a base of sE!");
static_assert(!mxtl::is_base_of<sE, sA>::value, "sE should not be a base of sA!");
static_assert(!mxtl::is_base_of<sE, sB>::value, "sE should not be a base of sB!");
static_assert(!mxtl::is_base_of<sE, sC>::value, "sE should not be a base of sC!");
static_assert(!mxtl::is_base_of<sE, sD>::value, "sE should not be a base of sD!");

}  // namespace is_base_of_tests

namespace has_virtual_destructor_tests {

struct A            {         ~A() { } };
struct B            { virtual ~B() { } };
struct C : public A {         ~C() { } };
struct D : public B {         ~D() { } };
struct E : public A { virtual ~E() { } };

static_assert(!mxtl::has_virtual_destructor<A>::value, "A should have no virtual destructor");
static_assert( mxtl::has_virtual_destructor<B>::value, "B should have a virtual destructor");
static_assert(!mxtl::has_virtual_destructor<C>::value, "C should have no virtual destructor");
static_assert( mxtl::has_virtual_destructor<D>::value, "D should have a virtual destructor");
static_assert( mxtl::has_virtual_destructor<E>::value, "E should have a virtual destructor");

}  // namespace has_virtual_destructor_tests

namespace is_pointer_tests {

struct StructType   {
    void MemberFunction();
    int member_variable;

    static void StaticMemberFunction();
    static int static_member_variable;
};

void SomeGlobalFunc();  // note; "global" in air-quotes
static void SomeStaticFunc();

class  ClassType    { };
enum   EnumType     { One, Two };
union  UnionType    { int a; double b; };

static_assert(!mxtl::is_pointer<StructType>::value,         "StructType is not a pointer!");
static_assert( mxtl::is_pointer<StructType*>::value,        "StructType* is a pointer!");
static_assert( mxtl::is_pointer<StructType**>::value,       "StructType** is a pointer!");
static_assert(!mxtl::is_pointer<ClassType>::value,          "ClassType is not a pointer!");
static_assert( mxtl::is_pointer<ClassType*>::value,         "ClassType* is a pointer!");
static_assert( mxtl::is_pointer<ClassType**>::value,        "ClassType** is a pointer!");
static_assert(!mxtl::is_pointer<EnumType>::value,           "EnumType is not a pointer!");
static_assert( mxtl::is_pointer<EnumType*>::value,          "EnumType* is a pointer!");
static_assert( mxtl::is_pointer<EnumType**>::value,         "EnumType** is a pointer!");
static_assert(!mxtl::is_pointer<UnionType>::value,          "UnionType is not a pointer!");
static_assert( mxtl::is_pointer<UnionType*>::value,         "UnionType* is a pointer!");
static_assert( mxtl::is_pointer<UnionType**>::value,        "UnionType** is a pointer!");
static_assert(!mxtl::is_pointer<int>::value,                "int is not a pointer!");
static_assert(!mxtl::is_pointer<int[]>::value,              "int[] is not a pointer!");
static_assert( mxtl::is_pointer<int*>::value,               "int* is a pointer!");
static_assert( mxtl::is_pointer<int**>::value,              "int** is a pointer!");

static_assert(!mxtl::is_pointer<const int>::value,          "const int is not a pointer!");
static_assert(!mxtl::is_pointer<volatile int>::value,       "volatile int is not a pointer!");
static_assert(!mxtl::is_pointer<const volatile int>::value, "const volatile int is not a pointer!");

static_assert( mxtl::is_pointer<const int*>::value,          "const int* is a pointer!");
static_assert( mxtl::is_pointer<volatile int*>::value,       "volatile int* is a pointer!");
static_assert( mxtl::is_pointer<const volatile int*>::value, "const volatile int* is a pointer!");

static_assert( mxtl::is_pointer<int* const >::value,          "int* const is a pointer!");
static_assert( mxtl::is_pointer<int* volatile >::value,       "int* volatile is a pointer!");
static_assert( mxtl::is_pointer<int* const volatile >::value, "int* const volatile is a pointer!");

static_assert( mxtl::is_pointer<const int* const >::value,    "const int* const is a pointer!");
static_assert( mxtl::is_pointer<const int* volatile >::value, "const int* volatile is a pointer!");
static_assert( mxtl::is_pointer<const int* const volatile>::value,
        "const int* const volatile is a pointer!");

static_assert( mxtl::is_pointer<volatile int* const >::value, "volatile int* const is a pointer!");
static_assert( mxtl::is_pointer<volatile int* volatile >::value,
        "volatile int* volatile is a pointer!");
static_assert( mxtl::is_pointer<volatile int* const volatile>::value,
        "volatile int* const volatile is a pointer!");

static_assert( mxtl::is_pointer<const volatile int* const >::value,
        "const volatile int* const is a pointer!");
static_assert( mxtl::is_pointer<const volatile int* volatile >::value,
        "const volatile int* volatile is a pointer!");
static_assert( mxtl::is_pointer<const volatile int* const volatile>::value,
        "const volatile int* const volatile is a pointer!");

static_assert(!mxtl::is_pointer<decltype(&StructType::MemberFunction)>::value,
              "pointer to StructType::MemberFunction is not a pointer!");
static_assert(!mxtl::is_pointer<decltype(&StructType::member_variable)>::value,
              "pointer to StructType::member_variable is not a pointer!");

static_assert( mxtl::is_pointer<decltype(&StructType::StaticMemberFunction)>::value,
              "pointer to StructType::MemberFunction is a pointer!");
static_assert( mxtl::is_pointer<decltype(&StructType::static_member_variable)>::value,
              "pointer to StructType::static_member_variable is a pointer!");

static_assert( mxtl::is_pointer<decltype(&SomeGlobalFunc)>::value,
              "pointer to SomeGlobalFunc is a pointer!");
static_assert( mxtl::is_pointer<decltype(&SomeStaticFunc)>::value,
              "pointer to SomeStaticFunc is a pointer!");
static_assert(!mxtl::is_pointer<decltype(nullptr)>::value,
              "decltype(nullptr) (aka nullptr_t) is not a pointer (because C++)");
}  // namespace is_pointer_tests;

namespace is_convertible_tests {

class A { };
class B : public A { };
class C { };

template <typename From, typename To>
using icp = mxtl::is_convertible_pointer<From, To>;

static_assert( icp<B*, A*>::value, "Should convert B* --> A*");
static_assert(!icp<A*, B*>::value, "Should not convert A* --> B*");
static_assert(!icp<A,  B*>::value, "Should not convert A --> B*");
static_assert(!icp<A*, B>::value,  "Should not convert A* --> B");
static_assert(!icp<A,  B>::value,  "Should not convert A --> B");
static_assert(!icp<A*, C*>::value, "Should not convert A* --> C*");

static_assert( icp<int*, void*>::value,         "Should convert int* --> void*");
static_assert( icp<int*, const int*>::value,    "Should convert int* --> const int*");
static_assert( icp<int*, volatile int*>::value, "Should convert int* --> volatile int*");
static_assert(!icp<const int*, int*>::value,    "Should not convert const int* --> int*");
static_assert(!icp<volatile int*, int*>::value, "Should not convert volatile int* --> int*");
static_assert(!icp<unsigned int*, int*>::value, "Should not convert unsigned int* --> int*");
static_assert(!icp<int*, unsigned int*>::value, "Should not convert int* --> unsigned int*");
static_assert(!icp<float*, double*>::value,     "Should not convert float* --> double*");

}  // namespace is_convertible_tests

namespace conditional_tests {

static_assert(mxtl::is_same<mxtl::conditional<true, int, bool>::type, int>::value, "wrong type");
static_assert(mxtl::is_same<mxtl::conditional<false, int, bool>::type, bool>::value, "wrong type");

}  // namespace conditional_tests
