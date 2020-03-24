// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Internal implementation of <trace/event_args.h>.
// This is not part of the public API: use <trace/event_args.h> instead.

#ifndef ZIRCON_SYSTEM_ULIB_LIB_TRACE_INTERNAL_EVENT_ARGS_H_
#define ZIRCON_SYSTEM_ULIB_LIB_TRACE_INTERNAL_EVENT_ARGS_H_

#include <assert.h>

#include <zircon/compiler.h>

#include <lib/trace-engine/context.h>
#include <lib/trace-engine/types.h>
#include <lib/trace/internal/pairs_internal.h>

#define TRACE_INTERNAL_NUM_ARGS(variable_name) (sizeof(variable_name) / sizeof((variable_name)[0]))

// Note: Argument names are processed in two steps. The first step is here
// where we store the string in |name_ref.inline_string|. The second step is
// done by |trace_internal_complete_args()| which is normally called by the
// helper routines that finish recording the event.
#define TRACE_INTERNAL_COUNT_ARGS(...) TRACE_INTERNAL_COUNT_PAIRS(__VA_ARGS__)
#define TRACE_INTERNAL_ALLOCATE_ARGS(var_name, args...)  \
  trace_arg_t var_name[TRACE_INTERNAL_COUNT_ARGS(args)]; \
  static_assert(TRACE_INTERNAL_NUM_ARGS(var_name) <= TRACE_MAX_ARGS, "too many args")

#ifdef __cplusplus

#define TRACE_INTERNAL_SCOPE_ARG_LABEL(var_name, idx) __trace_arg_##var_name##idx

#define TRACE_INTERNAL_HOLD_ARG(var_name, idx, name_literal, arg_value) \
  const auto& TRACE_INTERNAL_SCOPE_ARG_LABEL(var_name, idx) = (arg_value);
#define TRACE_INTERNAL_MAKE_ARG(var_name, idx, name_literal, arg_value)                          \
  {                                                                                              \
    .name_ref = {.encoded_value = 0, .inline_string = (name_literal)},                           \
    .value = ::trace::internal::MakeArgumentValue(TRACE_INTERNAL_SCOPE_ARG_LABEL(var_name, idx)) \
  }

#define TRACE_INTERNAL_DECLARE_ARGS(context, var_name, args...)                    \
  TRACE_INTERNAL_APPLY_PAIRWISE(TRACE_INTERNAL_HOLD_ARG, var_name, args)           \
  trace_arg_t var_name[] = {                                                       \
      TRACE_INTERNAL_APPLY_PAIRWISE_CSV(TRACE_INTERNAL_MAKE_ARG, var_name, args)}; \
  static_assert(TRACE_INTERNAL_NUM_ARGS(var_name) <= TRACE_MAX_ARGS, "too many args")

#define TRACE_INTERNAL_ASSIGN_ARG(var_name, idx, name_literal, arg_value) \
  var_name[idx - 1].name_ref.encoded_value = 0;                           \
  var_name[idx - 1].name_ref.inline_string = (name_literal);              \
  var_name[idx - 1].value =                                               \
      ::trace::internal::MakeArgumentValue(TRACE_INTERNAL_SCOPE_ARG_LABEL(var_name, idx));
#define TRACE_INTERNAL_INIT_ARGS(var_name, args...)                      \
  TRACE_INTERNAL_APPLY_PAIRWISE(TRACE_INTERNAL_HOLD_ARG, var_name, args) \
  TRACE_INTERNAL_APPLY_PAIRWISE(TRACE_INTERNAL_ASSIGN_ARG, var_name, args)

#else

#define TRACE_INTERNAL_MAKE_ARG(var_name, idx, name_literal, arg_value) \
  { .name_ref = {.encoded_value = 0, .inline_string = (name_literal)}, .value = (arg_value) }

#define TRACE_INTERNAL_DECLARE_ARGS(context, var_name, args...)                    \
  trace_arg_t var_name[] = {                                                       \
      TRACE_INTERNAL_APPLY_PAIRWISE_CSV(TRACE_INTERNAL_MAKE_ARG, var_name, args)}; \
  static_assert(TRACE_INTERNAL_NUM_ARGS(var_name) <= TRACE_MAX_ARGS, "too many args")

#define TRACE_INTERNAL_ASSIGN_ARG(var_name, idx, name_literal, arg_value) \
  var_name[idx - 1].name_ref.encoded_value = 0;                           \
  var_name[idx - 1].name_ref.inline_string = (name_literal);              \
  var_name[idx - 1].value = (arg_value);
#define TRACE_INTERNAL_INIT_ARGS(var_name, args...) \
  TRACE_INTERNAL_APPLY_PAIRWISE(TRACE_INTERNAL_ASSIGN_ARG, var_name, args)
#endif  // __cplusplus

__BEGIN_CDECLS
void trace_internal_complete_args(trace_context_t* context, trace_arg_t* args, size_t num_args);
__END_CDECLS

#define TRACE_INTERNAL_COMPLETE_ARGS(context, args, num_args)    \
  do {                                                           \
    trace_internal_complete_args((context), (args), (num_args)); \
  } while (0)

#ifdef __cplusplus

#include <lib/trace/internal/string_traits.h>
#include <type_traits>

namespace trace {
namespace internal {

template <typename T>
struct is_bool : public std::is_same<std::remove_cv_t<T>, bool> {};

// Helps construct trace argument values using SFINAE to coerce types.
template <typename T, typename Enable = void>
struct ArgumentValueMaker;

template <>
struct ArgumentValueMaker<trace_arg_value_t> {
  static trace_arg_value_t Make(trace_arg_value_t value) { return value; }
};

template <>
struct ArgumentValueMaker<decltype(nullptr)> {
  static trace_arg_value_t Make(decltype(nullptr) value) { return trace_make_null_arg_value(); }
};

template <typename T>
struct ArgumentValueMaker<T, typename std::enable_if<is_bool<T>::value>::type> {
  static trace_arg_value_t Make(bool value) { return trace_make_bool_arg_value(value); }
};

template <typename T>
struct ArgumentValueMaker<
    T, typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value &&
                               (sizeof(T) <= sizeof(int32_t))>::type> {
  static trace_arg_value_t Make(int32_t value) { return trace_make_int32_arg_value(value); }
};

template <typename T>
struct ArgumentValueMaker<
    T, typename std::enable_if<!is_bool<T>::value && std::is_unsigned<T>::value &&
                               !std::is_enum<T>::value &&
                               (sizeof(T) <= sizeof(uint32_t))>::type> {
  static trace_arg_value_t Make(uint32_t value) { return trace_make_uint32_arg_value(value); }
};

template <typename T>
struct ArgumentValueMaker<
    T, typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value &&
                               (sizeof(T) > sizeof(int32_t)) &&
                               (sizeof(T) <= sizeof(int64_t))>::type> {
  static trace_arg_value_t Make(int64_t value) { return trace_make_int64_arg_value(value); }
};

template <typename T>
struct ArgumentValueMaker<
    T, typename std::enable_if<std::is_unsigned<T>::value &&
                               !std::is_enum<T>::value &&
                               (sizeof(T) > sizeof(uint32_t)) &&
                               (sizeof(T) <= sizeof(uint64_t))>::type> {
  static trace_arg_value_t Make(uint64_t value) { return trace_make_uint64_arg_value(value); }
};

template <typename T>
struct ArgumentValueMaker<T, typename std::enable_if<std::is_enum<T>::value>::type> {
  using UnderlyingType = typename std::underlying_type<T>::type;
  static trace_arg_value_t Make(UnderlyingType value) {
    return ArgumentValueMaker<UnderlyingType>::Make(value);
  }
};

template <typename T>
struct ArgumentValueMaker<T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static trace_arg_value_t Make(double value) { return trace_make_double_arg_value(value); }
};

template <size_t n>
struct ArgumentValueMaker<char[n]> {
  static trace_arg_value_t Make(const char* value) {
    return trace_make_string_arg_value(
        trace_make_inline_string_ref(value, value[n - 1] ? n : n - 1));
  }
};

template <>
struct ArgumentValueMaker<const char*> {
  static trace_arg_value_t Make(const char* value) {
    return trace_make_string_arg_value(trace_make_inline_c_string_ref(value));
  }
};

// Works with various string types including fbl::String, fbl::StringView,
// std::string, and std::string_view.
template <typename T>
struct ArgumentValueMaker<
    T, typename std::enable_if<::trace::internal::is_string_like<T>::value>::type> {
  static trace_arg_value_t Make(const T& value) {
    return trace_make_string_arg_value(trace_make_inline_string_ref(
        ::trace::internal::GetStringData(value), ::trace::internal::GetStringLength(value)));
  }
};

template <typename T>
struct ArgumentValueMaker<T*> {
  static trace_arg_value_t Make(const T* pointer) {
    return trace_make_pointer_arg_value(reinterpret_cast<uintptr_t>(pointer));
  }
};

template <typename T>
trace_arg_value_t MakeArgumentValue(const T& value) {
  return ArgumentValueMaker<T>::Make(value);
}

}  // namespace internal
}  // namespace trace

#endif  // __cplusplus

#endif  // ZIRCON_SYSTEM_ULIB_LIB_TRACE_INTERNAL_EVENT_ARGS_H_
