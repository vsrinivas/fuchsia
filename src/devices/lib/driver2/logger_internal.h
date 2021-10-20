// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_LOGGER_INTERNAL_H_
#define SRC_DEVICES_LIB_DRIVER2_LOGGER_INTERNAL_H_
#include "src/devices/lib/driver2/logger.h"

namespace driver_internal {

static inline cpp17::optional<cpp17::string_view> FromCString(const char* value) {
  if (value) {
    return cpp17::optional(value);
  }
  return cpp17::nullopt;
}

template <typename... Args>
constexpr size_t ArgsSize(Args... args) {
  return sizeof...(args);
}

template <typename... Args>
struct Tuplet {
  std::tuple<Args...> tuple;
  size_t size;
  constexpr Tuplet(std::tuple<Args...> tuple, size_t size) : tuple(tuple), size(size) {}
};

template <typename Key, typename Value>
struct KeyValue {
  Key key;
  Value value;
  constexpr KeyValue(Key key, Value value) : key(key), value(value) {}
};

template <size_t i, size_t size>
constexpr bool ILessThanSize() {
  return i < size;
}

template <bool expr>
constexpr bool Not() {
  return !expr;
}

template <typename... LogArgs>
constexpr driver_internal::Tuplet<LogArgs...> Args(LogArgs... values) {
  return driver_internal::Tuplet<LogArgs...>(std::make_tuple(values...), sizeof...(values));
}

template <typename Key, typename Value>
constexpr driver_internal::KeyValue<Key, Value> KeyValueInternal(Key key, Value value) {
  return driver_internal::KeyValue<Key, Value>(key, value);
}

struct EncoderState {
  fuchsia_syslog::LogBuffer buffer;
  // Does nothing (enabled when we hit the last parameter that the user passed into us)
  template <size_t i, size_t size, typename... T,
            typename std::enable_if<Not<ILessThanSize<i, size>()>(), int>::type = 0>
  void Encode(Tuplet<T...> value) {}

  // Encodes an int64
  void Encode(KeyValue<const char*, int64_t> value) {
    buffer.WriteKeyValue(value.key, value.value);
  }

  // Encodes an int
  void Encode(KeyValue<const char*, int> value) {
    Encode(KeyValue<const char*, int64_t>(value.key, value.value));
  }

  // Encodes a NULL-terminated C-string.
  void Encode(KeyValue<const char*, const char*> value) {
    buffer.WriteKeyValue(value.key, value.value);
  }

  // Encodes a double floating point value
  void Encode(KeyValue<const char*, double> value) { buffer.WriteKeyValue(value.key, value.value); }

  // Encodes a floating point value
  void Encode(KeyValue<const char*, float> value) { buffer.WriteKeyValue(value.key, value.value); }

  // Encodes an arbitrary list of values recursively.
  template <size_t i, size_t size, typename... T,
            typename std::enable_if<ILessThanSize<i, size>(), int>::type = 0>
  void Encode(Tuplet<T...> value) {
    auto val = std::get<i>(value.tuple);
    Encode(val);
    Encode<i + 1, size>(value);
  }
};

template <typename Msg, typename... KeyValuePairs>
struct LogValue {
  constexpr LogValue(Msg msg, Tuplet<KeyValuePairs...> kvps) : msg(msg), kvps(kvps) {}
  void LogNew(driver::Logger& logger, FuchsiaLogSeverity severity, const char* file,
              unsigned int line, const char* condition) const {
    EncoderState state;
    uint32_t dropped = logger.GetAndResetDropped();
    logger.BeginRecord(state.buffer, severity, FromCString(file), line, FromCString(msg),
                       FromCString(condition), false, dropped);
    // https://bugs.llvm.org/show_bug.cgi?id=41093 -- Clang loses constexpr
    // even though this should be constexpr here.
    state.Encode<0, sizeof...(KeyValuePairs)>(kvps);
    logger.FlushRecord(state.buffer, dropped);
  }

  Msg msg;
  driver_internal::Tuplet<KeyValuePairs...> kvps;
};

template <typename Msg, typename... Args>
static auto MakeValue(Msg msg, driver_internal::Tuplet<Args...> args) {
  return LogValue<Msg, Args...>(msg, args);
}

template <size_t i, size_t size, typename... Values, typename... Tuple,
          typename std::enable_if<driver_internal::Not<driver_internal::ILessThanSize<i, size>()>(),
                                  int>::type = 0>
static auto MakeKV(std::tuple<Values...> value, std::tuple<Tuple...> tuple) {
  return driver_internal::Tuplet<Tuple...>(tuple, size);
}

template <size_t i, size_t size, typename... Values, typename... Tuple,
          typename std::enable_if<driver_internal::ILessThanSize<i, size>(), int>::type = 0>
static auto MakeKV(std::tuple<Values...> value, std::tuple<Tuple...> tuple) {
  // Key at index i, value at index i+1
  auto k = std::get<i>(value);
  auto v = std::get<i + 1>(value);
  auto new_tuple = std::tuple_cat(tuple, std::make_tuple(KeyValueInternal(k, v)));
  return MakeKV<i + 2, size, Values...>(value, new_tuple);
}

template <typename... Args, typename... EmptyTuple>
static auto MakeKV(std::tuple<Args...> args, std::tuple<EmptyTuple...> start_tuple) {
  return MakeKV<0, sizeof...(Args), Args..., EmptyTuple...>(args, start_tuple);
}

template <typename... Args>
static auto MakeKV(std::tuple<Args...> args) {
  return MakeKV(args, std::make_tuple());
}

template <typename Msg, typename... Args>
static void fx_slog(driver::Logger& logger, FuchsiaLogSeverity severity, const char* file, int line,
                    Msg msg, Args... args) {
  if (severity < logger.GetSeverity()) {
    return;
  }
  MakeValue(msg, MakeKV<Args...>(std::make_tuple(args...)))
      .LogNew(logger, severity, file, line, nullptr);
}

}  // namespace driver_internal

#endif  // SRC_DEVICES_LIB_DRIVER2_LOGGER_INTERNAL_H_
