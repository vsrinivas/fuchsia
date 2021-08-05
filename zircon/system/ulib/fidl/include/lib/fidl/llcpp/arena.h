// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ARENA_H_
#define LIB_FIDL_LLCPP_ARENA_H_

#include <lib/fidl/llcpp/traits.h>
#include <zircon/assert.h>

#include <functional>
#include <type_traits>

namespace fidl {

// |AnyArena| is the base class of all of the |Arena| classes. It is independent
// of the initial buffer size. All the implementation is done here. The |Arena|
// specializations only exist to define the initial buffer size.
//
// The arena owns all the data which are allocated. That means that the
// allocated data can be used by pure views. The allocated data are freed when
// the arena is freed.
//
// Users cannot directly call an arena's methods. Instead, they must do so via
// |ObjectView|, |StringView| and |VectorView| classes, as well as generated
// wire domain objects such as tables and unions. The allocation is first made
// within the initial buffer. When the initial buffer is full (or, at least, the
// next allocation doesn't fit in the remaining space), the arena allocates
// extra buffers on the heap. If one allocation is bigger than the capacity of a
// standard extra buffer, a tailored buffer is allocated which only contains the
// allocation.
//
// Allocations are put one after the other in the buffers. When a buffer can't
// fit the next allocation, the remaining space is lost an another buffer is
// allocated on the heap. Each allocation respects |FIDL_ALIGNMENT|. For
// allocations which don't need a destructor, we only allocate the requested
// size within the buffer. For allocations with a non trivial destructor, we
// also allocate some space for a |struct Destructor| which is stored before the
// requested data.
//
// The constructor and destructor of |AnyArena| are private to disallow direct
// instantiation. The desired style of using |AnyArena| is to pass a reference
// when a function does not care about the specific initial size of the arena:
//
//     // This function constructs a |Bar| object using the arena.
//     // The returned |Bar| will outlive the scope of the |GetBar| function,
//     // and is only destroyed when the supplied arena goes away.
//     fidl::ObjectView<Bar> GetBar(fidl::AnyArena& arena);
//
// This allows us to smoothly transition the |AnyArena| class into a pure
// virtual interface with polymorphic allocation behavior in the future if the
// need arise e.g. if we need many different kinds of arenas to interop.
class AnyArena {
 private:
  AnyArena(uint8_t* next_data_available, size_t available_size)
      : next_data_available_(next_data_available), available_size_(available_size) {}

  ~AnyArena();

  // Struct used to store the data needed to deallocate an allocation (to call
  // the destructor).
  struct Destructor {
    Destructor(Destructor* next, size_t count, void (*destructor)(uint8_t*, size_t))
        : next(next), count(count), destructor(destructor) {}

    Destructor* const next;
    const size_t count;
    void (*const destructor)(uint8_t*, size_t);
  };

  // Struct used to have more allocation buffers on the heap (when the initial
  // buffer is full).
  struct ExtraBlock {
   public:
    // In most cases, the size in big enough to only need an extra allocation.
    // It's also small enough to not use too much heap memory. The actual
    // allocated size for the ExtraBlock struct will be 16 KiB.
    static constexpr size_t kExtraSize = 16 * 1024 - FIDL_ALIGN(sizeof(ExtraBlock*));

    explicit ExtraBlock(ExtraBlock* next_block) : next_block_(next_block) {}

    ExtraBlock* next_block() const { return next_block_; }
    uint8_t* data() { return data_; }

   private:
    // Next block to deallocate (block allocated before this one).
    ExtraBlock* next_block_;
    // The usable data.
    alignas(FIDL_ALIGNMENT) uint8_t data_[kExtraSize];
  };

  // Deallocate anything allocated by the arena. Any data previously allocated
  // must not be accessed anymore.
  void Clean();

  // Deallocate anything allocated by the arena. After this call, the arena is
  // in the exact same state it was after the construction. Any data previously
  // allocated must not be accessed anymore.
  void Reset(uint8_t* next_data_available, size_t available_size) {
    Clean();
    next_data_available_ = next_data_available;
    available_size_ = available_size;
  }

  // Allocates and default constructs an instance of T. Used by
  // |fidl::ObjectView|.
  template <typename T, typename... Args>
  T* Allocate(Args&&... args) {
    return new (Allocate(sizeof(T), 1,
                         std::is_trivially_destructible<T>::value ? nullptr : ObjectDestructor<T>))
        T(std::forward<Args>(args)...);
  }

  // Allocates and default constructs a vector of T. Used by fidl::VectorView
  // and StringView. All the |count| vector elements are constructed.
  template <typename T>
  T* AllocateVector(size_t count) {
    return new (Allocate(sizeof(T), count,
                         std::is_trivially_destructible<T>::value ? nullptr : VectorDestructor<T>))
        typename std::remove_const<T>::type[count];
  }

  // Method which can deallocate an instance of T.
  template <typename T>
  static void ObjectDestructor(uint8_t* data, size_t count) {
    T* object = reinterpret_cast<T*>(data);
    object->~T();
  }

  // Method which can deallocate a vector of T.
  template <typename T>
  static void VectorDestructor(uint8_t* data, size_t count) {
    T* object = reinterpret_cast<T*>(data);
    T* end = object + count;
    while (object < end) {
      object->~T();
      ++object;
    }
  }

  // The actual allocation implementation.
  //
  // |Allocate| allocates the requested elements and eventually records the
  // destructor to call during the arena destruction if |destructor_function| is
  // not null. The allocated data is not initialized (it will be initialized by
  // the caller).
  uint8_t* Allocate(size_t item_size, size_t count,
                    void (*destructor_function)(uint8_t* data, size_t count));

  // Pointer to the next available data.
  uint8_t* next_data_available_;
  // Size of the data available at next_data_available_.
  size_t available_size_;
  // Linked list of the destructors to call starting with the last allocation.
  Destructor* last_destructor_ = nullptr;
  // Linked list of the extra blocks used for the allocation.
  ExtraBlock* last_extra_block_ = nullptr;

  template <typename T>
  friend class ObjectView;

  template <typename T>
  friend class VectorView;

  template <size_t>
  friend class Arena;
};

// Class which supports arena allocation of data for the views (ObjectView,
// StringView, VectorView). See |AnyArena| for general FIDL arena behavior.
template <size_t initial_capacity = 512>
class Arena : public AnyArena {
 public:
  // Can't move because destructor pointers can point within |initial_buffer_|.
  Arena(Arena&& to_move) = delete;
  // Copying an arena doesn't make sense.
  Arena(Arena& to_copy) = delete;

  Arena() : AnyArena(initial_buffer_, initial_capacity) {}

  // Deallocate anything allocated by the arena. After this call, the arena is
  // in the exact same state it was after the construction. Any data previously
  // allocated must not be accessed anymore.
  void Reset() { AnyArena::Reset(initial_buffer_, initial_capacity); }

 private:
  alignas(FIDL_ALIGNMENT) uint8_t initial_buffer_[initial_capacity];
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ARENA_H_
