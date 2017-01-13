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

namespace has_virtual_destructor {

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

}  // namespace has_virtual_destructor
