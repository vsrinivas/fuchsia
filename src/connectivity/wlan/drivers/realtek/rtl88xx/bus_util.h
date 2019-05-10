// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_BUS_UTIL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_BUS_UTIL_H_

// This file contains some utilities for interfacing with a Bus instance.

#include <zircon/errors.h>
#include <zircon/types.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "bus.h"
#include "register.h"

namespace wlan {
namespace rtl88xx {

namespace internal {

// Utility template class which performs a lot of C++ template subtlety.
//
// This class, is templated on a functor type T, which takes an arbitrary number of non-const
// pointer arguments.  It then defines:
//
// * A std::tuple type TupleType, which contains an element for each of the pointed-to argument
//   types.
// * A function PtrArgsTuple<T>::Invoke(), which invokes a functor T instance on a TupleType
//   instance, supplying the address of each corresponding field in the TupleType as the argument to
//   the functor.
template <typename T> class PtrArgsTuple {
   private:
    // Private implementation classes to extract an argument type list, as a std::tuple, from a
    // functor.
    template <typename U> class PtrArgTuple : public PtrArgTuple<decltype(&U::operator())> {};
    template <typename U, typename R, typename... A> class PtrArgTuple<R (U::*)(A...) const> {
       public:
        using PtrArgTupleType = std::tuple<typename std::remove_pointer<A>::type...>;
    };

   public:
    // The std::tuple type used with Invoke().
    using TupleType = typename PtrArgTuple<T>::PtrArgTupleType;

    // Invoke a functor `f` using address of elements in tuple `t` as arguments.
    template <typename U> static decltype(auto) Invoke(U&& f, TupleType* t) {
        constexpr size_t kTupleSize = std::tuple_size<TupleType>::value;
        return InvokeImpl(std::forward<U>(f), t, std::make_index_sequence<kTupleSize>());
    }

   private:
    // Private implementation template for Invoke().
    template <typename U, size_t... S>
    static decltype(auto) InvokeImpl(U&& f, TupleType* t, std::index_sequence<S...>) {
        return std::forward<U>(f)(&std::get<S>(*t)...);
    }
};

}  // namespace internal

// Perform a read-modify-write cycle on a register from the bus. Modification is performed by the
// functor passed as an argument, which will take pointers to the register types to be updated. For
// example, this call reads three registers from the bus, modifies them, and writes them back:
//
//   zx_status_t status = ZX_OK;
//   if ((status = UpdateRegisters(bus, [](reg::REG_FOO* foo, reg::REG_BAR* bar,
//                                         reg::REG_BAZ* baz) {
//            foo->set_bit_1(1);
//            bar->set_bit_2(0);
//            baz->set_field_3(0xFF);
//        })) != ZX_OK) {
//        return status;
//   }
template <typename UpdateFunc> zx_status_t UpdateRegisters(Bus* bus, UpdateFunc&& func) {
    using PtrArgsTupleType = internal::PtrArgsTuple<typename std::decay<decltype(func)>::type>;
    typename PtrArgsTupleType::TupleType registers;
    zx_status_t status = ZX_OK;

    // Perform the initial read from the bus.
    if ((status = PtrArgsTupleType::Invoke(
             [bus](auto... regs) {
                 zx_status_t status = ZX_OK;
                 const bool result [[gnu::unused]] =
                     (((status = bus->ReadRegister(regs)) == ZX_OK) && ...);
                 return status;
             },
             &registers)) != ZX_OK) {
        return status;
    }

    // Invoke the modification functor on the registers.
    PtrArgsTupleType::Invoke(std::forward<UpdateFunc>(func), &registers);

    // Write the values back to the bus.
    if ((status = PtrArgsTupleType::Invoke(
             [bus](auto... regs) {
                 zx_status_t status = ZX_OK;
                 const bool result [[gnu::unused]] =
                     (((status = bus->WriteRegister(*regs)) == ZX_OK) && ...);
                 return status;
             },
             &registers)) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

}  // namespace rtl88xx
}  // namespace wlan
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_REALTEK_RTL88XX_BUS_UTIL_H_
