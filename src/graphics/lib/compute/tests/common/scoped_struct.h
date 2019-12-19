// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SCOPED_STRUCT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SCOPED_STRUCT_H_

#include <new>
#include <utility>

// ScopedStruct<> is used to wrap a simple POD data type into a move-only,
// scoped-lifetime C++ object.
//
// This supports the following cases:
//
//   1) The type is POD, but has init() and reset() methods used to manage
//      initialization and finalization of its fields. The init() method(s)
//      must return void. Then use ScopedStructWithInitAndReset<> as in:
//
//           struct Foo {
//              void init(... some parameters);
//              void init(... some other parameters);
//              void reset();
//           };
//
//           ScopedStruct<Foo> foo(<args>);
//
//   2) The type uses custom functions for initialization, destruction and
//      move operations. Create a traits structure that defines the following:
//
//          struct FooTraits {
//             static constexpr Foo kDefault = <default-value>;
//             static void init(Foo* obj, ...) noexcept;        // initializer
//             static void destroy(Foo* obj) noexcept;          // finalizer
//             static void move(Foo* obj, Foo* from) noexcept;  // move.
//          };
//
//          ScopedStruct<Foo, FooTraits> foo(<args>);
//
// One can do the following with a ScopedStruct<Foo, ..> instance:
//
//   * Create new instance, all arguments are passed to the init method(s):
//       ScopedStruct<Foo, FooTraits> foo(init_value);
//
//   * Destruction calls the reset() method (or the destroy() trait function)
//     automatically:
//       {
//         ScopedFoo foo = ...;
//       }  // Destroys struct fields here.
//
//   * Reset an instance's content in-place:
//        ScopedStruct<Foo> foo = ...;
//        foo.reset(<init-args>);  // finalize previous content + initialize new one.
//        foo.reset();             // reset to default value.
//
//   * Move instances (e.g. into standard containers):
//       std::vector<ScopedFoo> foos;
//       foos.push_back(ScopedFoo(42));
//       ...
//       ScopedFoo foo2 = std::move(foo);   // foo automatically reset to default value.
//
//   * Move values into ScopedStruct<> instances with makeScopedStruct:
//
//       Foo foo0 = ...;
//       ScopedStruct<Foo> scoped_foo = makeScopedStruct(std::move(foo0));
//       // Not that this calls reset() on |foo0|.
//
//   * Copy operations are forbidden:
//       ScopedFoo foo = foo2;   // Error: deleted copy-constructor!
//
//   * Automatic cast to references:
//       ScopedFoo scoped_foo = ...;
//       Foo& foo_ref = scoped_foo;
//
//   * Access fields using pointer dereference:
//       scoped_foo->x = ...;
//
//   * Dereference the struct to access the data directly:
//       Foo copy = *scoped_foo;   // direct struct copy.
//
//   * Take the address of the underlying data:
//       Foo* foo_ptr = &scoped_foo;
//

// The default struct traits assumes that type T has:
//
//   - A default value, identified by {}.
//   - A set of init() methods, that all return void, used
//     to initialize an instance from scratch.
//   - A single reset() method to finalize an instance.
//
template <typename T>
struct ScopedStructDefaultTraits
{
  static constexpr T kDefault = {};

  template <typename... ARGS>
  static void
  init(T * obj, ARGS... args) noexcept
  {
    *obj = kDefault;
    obj->init(std::forward<ARGS>(args)...);
  }
  static void
  destroy(T * obj) noexcept
  {
    obj->reset();
  }
  static void
  move(T * obj, T * from) noexcept
  {
    obj->reset();
    *obj  = *from;
    *from = kDefault;
  }
};

template <typename T, typename TRAITS = ScopedStructDefaultTraits<T>>
class ScopedStruct {
 public:
  // Constructor passes all arguments to the init() function.
  template <typename... ARGS>
  ScopedStruct(ARGS... args)
  {
    TRAITS::init(&data_, std::forward<ARGS>(args)...);
  }

  // Constructor that takes a T value directly and moves it.
  ScopedStruct(T init_value) noexcept
  {
    TRAITS::move(&data_, &init_value);
  }

  // Static function to move a T value into a ScopedStruct instance.
  static ScopedStruct
  makeFrom(T && value)
  {
    ScopedStruct result;
    TRAITS::move(&result.data_, &value);
    return result;
  }

  // Destructor invokes the destroy() function.
  ~ScopedStruct()
  {
    TRAITS::destroy(&data_);
  }

  // Copy operations are not allowed.
  ScopedStruct(const ScopedStruct &) = delete;

  ScopedStruct &
  operator=(const ScopedStruct &) = delete;

  // Move operations are allowed.
  ScopedStruct(ScopedStruct && other) noexcept
  {
    TRAITS::move(&data_, &other.data_);
  }

  ScopedStruct &
  operator=(ScopedStruct && other) noexcept
  {
    if (this != std::addressof(other))
      {
        TRAITS::move(&data_, &other.data_);
      }
    return *this;
  }

  // reset() and reset(<params>) can be used to reset a given instance to
  // new content. The old content is destroyed, and a new instance is
  // initialized in-place.
  template <typename... ARGS>
  void
  reset(ARGS... args)
  {
    this->~ScopedStruct();
    new (this) ScopedStruct(std::forward<ARGS>(args)...);
  }

  // Cast to reference
  operator const T &() const
  {
    return data_;
  }

  operator T &()
  {
    return data_;
  }

  // Dereference operators
  T *
  operator->()
  {
    return &data_;
  }

  const T *
  operator->() const
  {
    return &data_;
  }

  T &
  operator*()
  {
    return data_;
  }

  const T &
  operator*() const
  {
    return data_;
  }

  // Address-of operator
  const T *
  operator&() const
  {
    return &data_;
  }
  T *
  operator&()
  {
    return &data_;
  }

  // Comparison operators. Just enough to store these under standard
  // containers.
  bool
  operator==(const ScopedStruct & other) const
  {
    return data_ == other.data_;
  }

  bool
  operator!=(const ScopedStruct & other) const
  {
    return data_ != other.data_;
  }

  bool
  operator<(const ScopedStruct & other) const
  {
    return data_ < other.data_;
  }

  // Hashing for std containers. See std::hash<> specialization below.
  size_t
  hash() const noexcept
  {
    return std::hash<T>()(&data_);
  }

 protected:
  T data_ = TRAITS::kDefault;
};

// Ensure proper std::swap behaviour.
template <typename T, typename TRAITS>
void
swap(ScopedStruct<T, TRAITS> & a, ScopedStruct<T, TRAITS> & b)
{
  using std::swap;
  swap(*a, *b);
}

// Static function used to move a T instance into a ScopedStruct instance.
// When using the default traits, template parameter type deduction can be
// used to write:
//
//    Foo foo = { .. };
//    auto scoped_foo = makeScopedStruct(std::move(foo));
//
template <typename T, typename TRAITS = ScopedStructDefaultTraits<T>>
ScopedStruct<T, TRAITS>
makeScopedStruct(T && value)
{
  return ScopedStruct<T, TRAITS>::makeFrom(std::move(value));
}

// Specializes struct std::hash<ScopedStruct<T>> for use in standard containers.
namespace std {

template <typename T>
struct hash
{
  size_t
  operator()(const ScopedStruct<T> & obj) const noexcept
  {
    return obj.hash();
  }
};

}  // namespace std

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SCOPED_STRUCT_H_
