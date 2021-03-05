// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/stdcompat/internal/span.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/string_view.h>
#include <lib/stdcompat/type_traits.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct WellFormed {
  int* data() { return nullptr; }
  const int* data() const { return nullptr; }
  size_t size() const { return 0; }
};

struct NoMutableDataOverload {
  const int* data() const { return nullptr; }
  size_t size() const { return 0; }
};

struct NoDataOverload {
  size_t size() const { return 0; }
};

struct NoConstDataOverload {
  int* data() { return nullptr; }
  size_t size() const { return 0; }
};

struct NoSizeOverload {
  int* data() { return nullptr; }
  const int* data() const { return nullptr; }
};

TEST(SpanCompatibleTraitTest, CheckForDataSizeAndConversionValidation) {
  using ::cpp20::internal::is_well_formed_data_and_size;

  static_assert(is_well_formed_data_and_size<std::vector<int>, int>::value == true, "");
  static_assert(is_well_formed_data_and_size<std::vector<int>, const int>::value == true, "");
  static_assert(is_well_formed_data_and_size<WellFormed, int>::value == true, "");
  static_assert(is_well_formed_data_and_size<NoMutableDataOverload, const int>::value == true, "");
  static_assert(is_well_formed_data_and_size<NoConstDataOverload, int>::value == true, "");
  static_assert(is_well_formed_data_and_size<const NoDataOverload, const int>::value == false, "");
  static_assert(is_well_formed_data_and_size<std::vector<const int>, int>::value == false, "");
  static_assert(is_well_formed_data_and_size<NoSizeOverload, const int>::value == false, "");
  static_assert(is_well_formed_data_and_size<WellFormed, std::vector<int>>::value == false, "");
  static_assert(is_well_formed_data_and_size<WellFormed, std::string>::value == false, "");
}

TEST(SpanCompatibleTraitTest, CheckForWellAndIllformedTypes) {
  using ::cpp20::internal::is_span_compatible_v;

  static_assert(is_span_compatible_v<std::vector<int>, int> == true, "");
  static_assert(is_span_compatible_v<std::vector<int>, const int> == true, "");
  static_assert(is_span_compatible_v<WellFormed, int> == true, "");
  static_assert(is_span_compatible_v<NoMutableDataOverload, const int> == true, "");
  static_assert(is_span_compatible_v<NoConstDataOverload, int> == true, "");
  static_assert(is_span_compatible_v<const NoDataOverload, const int> == false, "");
  static_assert(is_span_compatible_v<NoSizeOverload, const int> == false, "");

  // Discard array and span specializations.
  static_assert(is_span_compatible_v<cpp20::span<const int>, int> == false, "");
  static_assert(is_span_compatible_v<cpp20::span<int, 1>, int> == false, "");
  static_assert(is_span_compatible_v<std::array<int, 1>, int> == false, "");

  // Well formed with non convertible types.
  static_assert(is_span_compatible_v<std::vector<const int>, int> == false, "");
  static_assert(is_span_compatible_v<WellFormed, float> == false, "");
  static_assert(is_span_compatible_v<WellFormed, std::vector<int>> == false, "");
  static_assert(is_span_compatible_v<WellFormed, std::string> == false, "");
}

TEST(SpanTest, EmptyConstructorWithDynamicExtent) {
  constexpr cpp20::span<int> empty_span;

  static_assert(empty_span.extent == cpp20::dynamic_extent,
                "Empty span by default is dynamic extent.");
  static_assert(empty_span.data() == nullptr, "Empty span by default is dynamic extent.");
  static_assert(empty_span.size() == 0, "Empty span by default is dynamic extent.");
  static_assert(sizeof(empty_span) == sizeof(int*) + sizeof(size_t),
                "Dynamic extent span should include the size.");
}

TEST(SpanTest, EmptyConstructorWithZeroStaticExtent) {
  constexpr cpp20::span<int, 0> empty_span;

  static_assert(empty_span.extent == 0, "Empty span by default is static extent.");
  static_assert(empty_span.data() == nullptr, "Empty span by default is has nullptr.");
  static_assert(empty_span.size() == 0, "Empty span by default is has size 0.");
  static_assert(sizeof(empty_span) == sizeof(int*),
                "Static extent span should not store the size.");
}

TEST(SpanTest, ConstructFromIteratorAndCountWithDynamicExtent) {
  constexpr cpp17::string_view kLiteral = "Some String";
  constexpr cpp20::span<const char> literal_span(kLiteral.begin(), 2);

  static_assert(literal_span.extent == cpp20::dynamic_extent,
                "Empty span by default is dynamic extent.");
  static_assert(literal_span.data() == kLiteral.data(), "Empty span by default is dynamic extent.");
  static_assert(literal_span.size() == 2, "Empty span by default is dynamic extent.");
}

TEST(SpanTest, ConstructFromIteratorAndCountWithStaticExtent) {
  constexpr cpp17::string_view kLiteral = "Some String";
  constexpr cpp20::span<const char, 2> literal_span(kLiteral.begin(), 2);

  static_assert(literal_span.extent == 2, "Empty span by default is dynamic extent.");
  static_assert(literal_span.data() == kLiteral.data(), "Empty span by default is dynamic extent.");
  static_assert(literal_span.size() == 2, "Empty span by default is dynamic extent.");
}

TEST(SpanTest, ConstructFromIteratorAndEndIterWithDynamicExtent) {
  static const constexpr std::array<int, 10> kContainer = {};
  static_assert(!std::is_convertible<decltype(kContainer.end()), size_t>::value,
                "kContainer::end() must not be convertible to |size_t|.");

  constexpr cpp20::span<const int> container_span(kContainer.begin(), kContainer.begin() + 2);

  static_assert(container_span.extent == cpp20::dynamic_extent,
                "Empty span by default is dynamic extent.");
  static_assert(container_span.data() == kContainer.data(),
                "Empty span by default is dynamic extent.");
  static_assert(container_span.size() == 2, "Empty span by default is dynamic extent.");
}

TEST(SpanTest, ConstructFromIteratorAndEndIterWithStaticExtent) {
  static const constexpr std::array<int, 10> kContainer = {};
  static_assert(!std::is_convertible<decltype(kContainer.end()), size_t>::value,
                "kContainer::end() must not be convertible to |size_t|.");

  constexpr cpp20::span<const int, 2> container_span(kContainer.begin(), kContainer.begin() + 2);

  static_assert(container_span.extent == 2, "Empty span by default is dynamic extent.");
  static_assert(container_span.data() == kContainer.data(),
                "Empty span by default is dynamic extent.");
  static_assert(container_span.size() == 2, "Empty span by default is dynamic extent.");
}

template <typename T, typename U>
constexpr bool are_pointers_equal(T* a, U* b) {
  return static_cast<const void*>(a) == static_cast<const void*>(b);
}

template <typename T, typename U>
constexpr bool are_pointers_equal(volatile T* a, volatile U* b) {
  return static_cast<const volatile void*>(a) == static_cast<const volatile void*>(b);
}

template <typename ElementType, size_t expected_extent, typename SpanElementType = ElementType>
constexpr bool check_construct_from_array() {
  ElementType container[10] = {};

  cpp20::span<SpanElementType, expected_extent> array_span(container);
  const cpp20::span<SpanElementType, expected_extent> const_array_span(container);

  auto check = [](auto& view, auto& container) {
    if (view.extent != expected_extent) {
      return false;
    }

    if (!are_pointers_equal(view.data(), std::begin(container))) {
      return false;
    }

    if (view.size() != 10) {
      return false;
    }

    return true;
  };
  return check(array_span, container) && check(const_array_span, container);
}

TEST(SpanTest, ConstructArrayRefWithDynamicExtent) {
  static_assert(check_construct_from_array<int, cpp20::dynamic_extent>(),
                "Span constructor with dynamic extent failed with array failed.");
}

TEST(SpanTest, ConstructFromArrayRefWithStaticExtent) {
  static_assert(check_construct_from_array<int, 10>(),
                "Span constructor with static extent failed with array failed.");
}

TEST(SpanTest, ConstructFromConstArrayRefWithDynamicExtent) {
  static_assert(check_construct_from_array<const int, cpp20::dynamic_extent>(),
                "Span constructor with dynamic extent failed with array failed.");
}

TEST(SpanTest, ConstructFromConstArrayRefWithStaticExtent) {
  static_assert(check_construct_from_array<const int, 10>(),
                "Span constructor with static extent failed with array failed.");
}

TEST(SpanTest, ConstructFromArrayRefWithDynamicExtentAndConvertibleType) {
  static_assert(check_construct_from_array<int, cpp20::dynamic_extent, const int>(),
                "Span constructor with dynamic extent failed with array failed.");

  static_assert(
      check_construct_from_array<volatile int, cpp20::dynamic_extent, const volatile int>(),
      "Span constructor with dynamic extent failed with array failed.");
}

TEST(SpanTest, ConstructFromArrayRefWithStaticExtentAndConvertibleType) {
  static_assert(check_construct_from_array<int, 10, const int>(),
                "Span constructor with static extent failed with array failed.");

  static_assert(check_construct_from_array<volatile int, 10, const volatile int>(),
                "Span constructor with static extent failed with array failed.");
}

TEST(SpanTest, ConstructFromStdArrayWithDynamicExtent) {
  static constexpr std::array<int, 40> kArray = {};
  constexpr cpp20::span<const int> array_span(kArray);

  static_assert(std::is_convertible<decltype(kArray)&, cpp20::span<const int>>::value,
                "cpp20::span constructor for std::array should be implicit.");
  static_assert(array_span.data() == kArray.data(),
                "cpp20::span constructor from container R should match the container's data.");
  static_assert(array_span.size() == kArray.size(),
                "cpp20::span constructor from container R should match the container's size.");
}

TEST(SpanTest, ConstructFromStdArrayWithStaticExtent) {
  static constexpr std::array<int, 40> kArray = {};
  constexpr cpp20::span<const int, 40> array_span(kArray);

  static_assert(std::is_convertible<decltype(kArray)&, cpp20::span<const int, 40>>::value,
                "cpp20::span constructor for std::array should be implicit.");
  static_assert(array_span.data() == kArray.data(),
                "cpp20::span constructor from container R should match the container's data.");
  static_assert(array_span.size() == kArray.size(),
                "cpp20::span constructor from container R should match the container's size.");
}

TEST(SpanTest, ConstructFromContainerWithDynamicExtent) {
  const std::vector<int> container(40);
  const cpp20::span<const int> container_span(container);

  static_assert(std::is_convertible<decltype(container)&, cpp20::span<const int>>::value,
                "cpp20::span constructor for container R should be implicit for dynamic extent.");

  EXPECT_EQ(container_span.data(), container.data());
  EXPECT_EQ(container_span.size(), container.size());
}

TEST(SpanTest, ConstructFromContainerWithStaticExtent) {
  const std::vector<int> container(40);
  const cpp20::span<const int, 40> container_span(container);

  static_assert(!std::is_convertible<decltype(container)&, cpp20::span<const int, 40>>::value,
                "cpp20::span from a container R should be explicit for static extent.");

  EXPECT_EQ(container_span.data(), container.data());
  EXPECT_EQ(container_span.size(), container.size());
}

template <typename U, typename T>
constexpr bool check_span_from_another(T source_span) {
  U copy_span(source_span);

  if (!are_pointers_equal(copy_span.data(), source_span.data())) {
    return false;
  }
  if (copy_span.size() != source_span.size()) {
    return false;
  }
  return true;
}

TEST(SpanTest, ConstructFromAnotherSpan) {
  static constexpr std::array<int, 40> kContainer = {};
  constexpr cpp20::span<const int> dynamic_span(kContainer);
  constexpr cpp20::span<const int, 40> static_span(kContainer);

  static_assert(std::is_constructible<decltype(static_span), decltype(static_span)>::value);
  static_assert(std::is_constructible<decltype(static_span), decltype(dynamic_span)>::value);
  static_assert(std::is_constructible<decltype(dynamic_span), decltype(static_span)>::value);
  static_assert(std::is_constructible<decltype(dynamic_span), decltype(dynamic_span)>::value);

  static_assert(std::is_convertible<decltype(static_span), decltype(dynamic_span)>::value);
  static_assert(!std::is_convertible<decltype(dynamic_span), decltype(static_span)>::value);

  static_assert(check_span_from_another<decltype(dynamic_span)>(static_span),
                "cpp20::span with dynamic extent constructor from another span with static extent "
                "not initialized correctly.");
  static_assert(check_span_from_another<decltype(static_span)>(dynamic_span),
                "cpp20::span with static extent constructor from another span with dynamic extent "
                "not initialized correctly.");
}

TEST(SpanTest, CopyConstructor) {
  static constexpr std::array<int, 40> kContainer = {};
  constexpr cpp20::span<const int> dynamic_span(kContainer);
  constexpr cpp20::span<const int, 40> static_span(kContainer);

  static_assert(check_span_from_another<decltype(dynamic_span)>(dynamic_span),
                "cpp20::span with dynamic extent constructor from another span with dynamic extent "
                "not initialized correctly.");
  static_assert(check_span_from_another<decltype(static_span)>(static_span),
                "cpp20::span with static extent constructor from another span with static extent "
                "not initialized correctly.");
}

TEST(SpanTest, AssignmentOperator) {
  static constexpr std::array<int, 40> kContainer = {};
  constexpr cpp20::span<const int> dynamic_span(kContainer);
  constexpr cpp20::span<const int> new_dynamic_span = dynamic_span;

  static_assert(dynamic_span.data() == new_dynamic_span.data(),
                "cpp20::span::operator= must set data to other.data().");

  static_assert(dynamic_span.size() == new_dynamic_span.size(),
                "cpp20::span::operator= must set size to rhs.size().");

  constexpr cpp20::span<const int, 40> static_span(kContainer);
  constexpr cpp20::span<const int, 40> new_static_span = static_span;

  static_assert(static_span.data() == new_static_span.data(),
                "cpp20::span::operator= must set data to other.data().");

  static_assert(static_span.size() == new_static_span.size(),
                "cpp20::span::operator= must set size to rhs.size().");
}

template <typename T, size_t ContainerSize>
constexpr bool span_bracket_operator_check() {
  std::array<int, ContainerSize> container = {};
  T view(container);
  const T const_view(container);

  constexpr auto check = [](auto& view, auto& container) {
    if (!are_pointers_equal(&view[0], &view.front())) {
      return false;
    }

    if (!are_pointers_equal(&view[container.size() - 1], &view.back())) {
      return false;
    }

    for (size_t i = 0; i < view.size(); ++i) {
      if (!are_pointers_equal(&view[i], view.data() + i)) {
        return false;
      }
    }
    return true;
  };

  return check(view, container) && check(const_view, container);
}

TEST(SpanTest, BracketOperator) {
  static_assert(span_bracket_operator_check<cpp20::span<int>, 10>(),
                "bracket operator for dynamic extent span failed.");
  static_assert(span_bracket_operator_check<cpp20::span<const int>, 10>(),
                "bracket operator for dynamic extent span failed.");

  static_assert(span_bracket_operator_check<cpp20::span<int, 10>, 10>(),
                "bracket operator for static extent span failed.");
  static_assert(span_bracket_operator_check<cpp20::span<const int, 10>, 10>(),
                "bracket operator for static extent span failed.");
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_iterator_check() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);
  const cpp20::span<T, Extent> const_view(kContainer);

  // No assumption on iterator type, which is why they are dereferenced and then taken the address
  // of, if someone would choose to wrap the iterators with a type.
  constexpr auto check = [](auto& view) {
    for (size_t i = 0; i < view.size(); ++i) {
      if (!are_pointers_equal(&*(view.begin() + i), &view[i])) {
        return false;
      }

      if (!are_pointers_equal(&*(view.end() - i - 1), &view[view.size() - i - 1])) {
        return false;
      }

      if (!are_pointers_equal(&*(view.rbegin() + i), &view[view.size() - i - 1])) {
        return false;
      }

      if (!are_pointers_equal(&*(view.rend() - i - 1), &view[i])) {
        return false;
      }
    }

    return true;
  };

  return check(view) && check(const_view);
}

TEST(SpanTest, IteratorCheck) {
  static_assert(span_iterator_check<int, cpp20::dynamic_extent, 40>(),
                "Iterators for span with dynamic extent are not set correctly.");
  static_assert(span_iterator_check<const int, cpp20::dynamic_extent, 40>(),
                "Iterators for span with dynamic extent are not set correctly.");

  static_assert(span_iterator_check<int, 40>(),
                "Iterators for span with dynamic extent are not set correctly.");
  static_assert(span_iterator_check<const int, 40>(),
                "Iterators for span with dynamic extent are not set correctly.");
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_front_check() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);
  const cpp20::span<T, Extent> const_view(kContainer);

  return are_pointers_equal(&view.front(), &view[0]) &&
         are_pointers_equal(&const_view.front(), &const_view[0]);
}

TEST(SpanTest, FrontPointsToFirstElement) {
  static_assert(span_front_check<int, cpp20::dynamic_extent, 40>(),
                "cpp20::span::front must return reference to data[0].");
  static_assert(span_front_check<const int, cpp20::dynamic_extent, 40>(),
                "cpp20::span::front must return reference to data[0].");

  static_assert(span_front_check<int, 40>(),
                "cpp20::span::front must return reference to data[0].");
  static_assert(span_front_check<const int, 40>(),
                "cpp20::span::front must return reference to data[0].");
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_back_check() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);
  const cpp20::span<T, Extent> const_view(kContainer);

  constexpr auto check = [](auto& view) {
    return are_pointers_equal(&view.back(), &view[view.size() - 1]);
  };

  return check(view) && check(const_view);
}

TEST(SpanTest, BackPointsToLastElement) {
  static_assert(span_back_check<int, cpp20::dynamic_extent, 40>(),
                "cpp20::span::front must return reference to data[0].");
  static_assert(span_back_check<const int, cpp20::dynamic_extent, 40>(),
                "cpp20::span::front must return reference to data[0].");

  static_assert(span_back_check<int, 40>(), "cpp20::span::front must return reference to data[0].");
  static_assert(span_back_check<const int, 40>(),
                "cpp20::span::front must return reference to data[0].");
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_size_bytes_check() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);

  return view.size_bytes() == sizeof(T) * ElementCount;
}

TEST(SpanTest, SizeBytesReflectsSizeOfType) {
  static_assert(span_size_bytes_check<int, cpp20::dynamic_extent, 15>(),
                "cpp20::span::size_bytes should reflect the number of bytes in the span.");
  static_assert(span_size_bytes_check<const int, cpp20::dynamic_extent, 15>(),
                "cpp20::span::size_bytes should reflect the number of bytes in the span.");

  static_assert(span_size_bytes_check<int, 15>(),
                "cpp20::span::size_bytes should reflect the number of bytes in the span.");
  static_assert(span_size_bytes_check<const int, 15>(),
                "cpp20::span::size_bytes should reflect the number of bytes in the span.");
}

TEST(SpanTest, EmptyWhenSizeIsZero) {
  constexpr cpp20::span<int> kEmpty;

  static_assert(kEmpty.data() == nullptr, "default constructed span should have nullptr as data.");
  static_assert(kEmpty.size() == 0, "default constructed span should have 0 as size.");
  static_assert(kEmpty.empty(), "default constructed span should be considered empty.");

  static constexpr std::array<int, 40> kContainer = {};
  // TODO(fxbug.dev/70669): Remove cast when libc++'s std::span gets constraints correct
  constexpr cpp20::span<const int> view(kContainer.data(), static_cast<size_t>(0));

  static_assert(view.data() == kContainer.data(),
                "default constructed span should have nullptr as data.");
  static_assert(view.empty(),
                "a span should be considered empty even if data is not nullptr but size is zero.");
}

// Checks partitioning a subspan that the contents match.
// Essentially that each subspan created within this range has the correct properties.
template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_subspan_dynamic_extent_check() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);
  const cpp20::span<const T, Extent> const_view(kContainer);

  constexpr auto check = [](auto& view) {
    for (size_t i = 0; i < view.size(); ++i) {
      {
        auto subview = view.subspan(i);
        static_assert(decltype(subview)::extent == cpp20::dynamic_extent,
                      "cpp20::subspan(size_t, size_t) should always return a cpp20::span<T, "
                      "dynamic_extent>.");
        if (subview.empty()) {
          if (i != view.size() - 1) {
            return false;
          }
          continue;
        }

        // This is ill-formed for an empty span.
        if (!are_pointers_equal(&subview.front(), &view[i])) {
          return false;
        }

        if (subview.size() != view.size() - i) {
          return false;
        }
      }
      for (size_t j = 0; j < view.size() - i; ++j) {
        auto subview = view.subspan(i, j);

        if (subview.empty()) {
          if (j != 0 && i + j != view.size()) {
            return false;
          }
          continue;
        }

        if (!are_pointers_equal(&subview.front(), &view[i])) {
          return false;
        }

        if (subview.size() != j) {
          return false;
        }
      }
    }
    return true;
  };

  return check(view) && check(const_view);
}

TEST(SpanTest, SubspanWithDynamicExtent) {
  static_assert(span_subspan_dynamic_extent_check<int, cpp20::dynamic_extent, 10>(),
                "cpp20::subspan check failed.");
  static_assert(span_subspan_dynamic_extent_check<const int, cpp20::dynamic_extent, 10>(),
                "cpp20::subspan check failed.");
  static_assert(span_subspan_dynamic_extent_check<int, 10>(), "cpp20::subspan check failed.");
  static_assert(span_subspan_dynamic_extent_check<const int, 10>(), "cpp20::subspan check failed.");
}

template <size_t Offset, size_t Count = cpp20::dynamic_extent, typename T>
constexpr bool span_subspan_with_static_extent_instance_check(T view) {
  auto subview = view.template subspan<Offset, Count>();
  auto actual_count = (Count == cpp20::dynamic_extent) ? view.size() - Offset : Count;

  if (subview.empty()) {
    return !static_cast<bool>(actual_count != 0 && Offset + actual_count != view.size());
  }

  if (!are_pointers_equal(&subview.front(), &view[Offset])) {
    return false;
  }

  if (Count != cpp20::dynamic_extent && subview.size() != Count) {
    return false;
  }

  if (Count == cpp20::dynamic_extent && subview.size() != view.size() - Offset) {
    return false;
  }

  // Extent check.
  if (Count != cpp20::dynamic_extent && subview.extent != Count) {
    return false;
  }

  if (Count == cpp20::dynamic_extent && T::extent != cpp20::dynamic_extent &&
      subview.extent != T::extent - Offset) {
    return false;
  }

  if (Count == cpp20::dynamic_extent && T::extent == cpp20::dynamic_extent &&
      subview.extent != cpp20::dynamic_extent) {
    return false;
  }

  return true;
}

template <size_t Offset, size_t Count, typename T,
          typename std::enable_if<(Offset + Count > 5), bool>::type = true>
constexpr bool span_subspan_static_extent_check_unfold(T view) {
  return true;
}

template <size_t Offset, size_t Count, typename T,
          typename std::enable_if<Offset + Count <= 5, bool>::type = true>
constexpr bool span_subspan_static_extent_check_unfold(T view) {
  if (!span_subspan_with_static_extent_instance_check<Offset, 5 - Offset>(view)) {
    return false;
  }

  return span_subspan_static_extent_check_unfold<Offset, Count + 1>(view);
}

template <typename T, size_t Extent>
constexpr bool span_subspan_static_extent_check() {
  std::array<T, 5> container = {};
  cpp20::span<T, Extent> view(container);
  const cpp20::span<const T, Extent> const_view(container);

  constexpr auto check = [](auto& view) {
    if (!span_subspan_with_static_extent_instance_check<0>(view)) {
      return span_subspan_static_extent_check_unfold<0, 0>(view);
    }

    if (!span_subspan_with_static_extent_instance_check<1>(view)) {
      return span_subspan_static_extent_check_unfold<1, 0>(view);
    }

    if (!span_subspan_with_static_extent_instance_check<2>(view)) {
      return span_subspan_static_extent_check_unfold<2, 0>(view);
    }

    if (!span_subspan_with_static_extent_instance_check<3>(view)) {
      return span_subspan_static_extent_check_unfold<3, 0>(view);
    }

    if (!span_subspan_with_static_extent_instance_check<4>(view)) {
      return span_subspan_static_extent_check_unfold<4, 0>(view);
    }

    if (!span_subspan_with_static_extent_instance_check<5>(view)) {
      return span_subspan_static_extent_check_unfold<5, 0>(view);
    }

    return true;
  };

  return check(view) && check(const_view);
}

TEST(SpanTest, SubspanWithStaticExtent) {
  static_assert(span_subspan_static_extent_check<int, cpp20::dynamic_extent>(), "");
  static_assert(span_subspan_static_extent_check<const int, cpp20::dynamic_extent>());
  static_assert(span_subspan_static_extent_check<int, 5>());
  static_assert(span_subspan_static_extent_check<const int, 5>());
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_first_check_with_dynamic_extent() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);
  const cpp20::span<const T, Extent> const_view(kContainer);

  constexpr auto check = [](auto& view) {
    for (auto i = 0u; i < view.size(); ++i) {
      auto first_view = view.first(i);

      if (!first_view.empty() && !are_pointers_equal(&first_view.front(), &view.front())) {
        return false;
      }

      if (first_view.size() != i) {
        return false;
      }
    }
    return true;
  };

  return check(view) && check(const_view);
}

TEST(SpanTest, FirstWithDynamicExtent) {
  static_assert(span_first_check_with_dynamic_extent<int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_first_check_with_dynamic_extent<const int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_first_check_with_dynamic_extent<int, 20>(), "");
  static_assert(span_first_check_with_dynamic_extent<const int, 20>(), "");
}

template <size_t Count, typename T, typename std::enable_if<Count == 0, bool>::type = true>
constexpr bool span_first_check_with_static_extent_impl(T& view) {
  auto subview = view.template first<Count>();
  return are_pointers_equal(subview.data(), view.data()) && subview.size() == 0 &&
         subview.extent == 0;
}

template <size_t Count, typename T, typename std::enable_if<Count != 0, bool>::type = true>
constexpr bool span_first_check_with_static_extent_impl(T& view) {
  auto subview = view.template first<Count>();

  if (subview.size() != Count || subview.extent != Count) {
    return false;
  }

  if (!are_pointers_equal(subview.data(), view.data())) {
    return false;
  }

  return span_first_check_with_static_extent_impl<Count - 1>(view);
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_first_check_with_static_extent() {
  std::array<T, ElementCount> container = {};
  cpp20::span<T, Extent> view(container);
  const cpp20::span<T, Extent> const_view(container);

  return span_first_check_with_static_extent_impl<ElementCount>(view) &&
         span_first_check_with_static_extent_impl<ElementCount>(const_view);
}

TEST(SpanTest, FirstWithStaticExtent) {
  static_assert(span_first_check_with_static_extent<int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_first_check_with_static_extent<const int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_first_check_with_static_extent<int, 20>(), "");
  static_assert(span_first_check_with_static_extent<const int, 20>(), "");
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_last_check_with_dynamic_extent() {
  std::array<T, ElementCount> kContainer = {};
  cpp20::span<T, Extent> view(kContainer);
  const cpp20::span<const T, Extent> const_view(kContainer);

  constexpr auto check = [](auto& view) {
    for (auto i = 0u; i < view.size(); ++i) {
      auto last_view = view.last(i);

      if (!last_view.empty() && (!are_pointers_equal(&last_view.back(), &view.back()) ||
                                 !are_pointers_equal(&last_view.front(), &*(view.end() - i)))) {
        return false;
      }

      if (last_view.size() != i) {
        return false;
      }
    }
    return true;
  };

  return check(view) && check(const_view);
}

TEST(SpanTest, LastWithDynamicExtent) {
  static_assert(span_last_check_with_dynamic_extent<int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_last_check_with_dynamic_extent<const int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_last_check_with_dynamic_extent<int, 20>(), "");
  static_assert(span_last_check_with_dynamic_extent<const int, 20>(), "");
}

template <size_t Count, typename T, typename std::enable_if<Count == 0, bool>::type = true>
constexpr bool span_last_check_with_static_extent_impl(T& view) {
  // TODO(fxbug.dev/70523): switch to cpp20::to_address when upstream is fixed
  auto subview = view.template last<Count>();
  return are_pointers_equal(subview.data(), cpp17::addressof(*view.end())) && subview.size() == 0 &&
         subview.extent == 0;
}

template <size_t Count, typename T, typename std::enable_if<Count != 0, bool>::type = true>
constexpr bool span_last_check_with_static_extent_impl(T& view) {
  auto subview = view.template last<Count>();

  if (subview.size() != Count || subview.extent != Count) {
    return false;
  }

  // TODO(fxbug.dev/70523): switch to cpp20::to_address when upstream is fixed
  if (!are_pointers_equal(subview.data(), cpp17::addressof(*(view.end() - Count)))) {
    return false;
  }

  return span_last_check_with_static_extent_impl<Count - 1>(view);
}

template <typename T, size_t Extent, size_t ElementCount = Extent>
constexpr bool span_last_check_with_static_extent() {
  std::array<T, ElementCount> container = {};
  cpp20::span<T, Extent> view(container);
  const cpp20::span<T, Extent> const_view(container);

  return span_last_check_with_static_extent_impl<ElementCount>(view) &&
         span_last_check_with_static_extent_impl<ElementCount>(const_view);
}

TEST(SpanTest, LastWithStaticExtent) {
  static_assert(span_last_check_with_static_extent<int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_last_check_with_static_extent<const int, cpp20::dynamic_extent, 40>(), "");
  static_assert(span_last_check_with_static_extent<int, 20>(), "");
  static_assert(span_last_check_with_static_extent<const int, 20>(), "");
}

TEST(SpanTest, AsWriteableBytesWithDynamicExtent) {
  std::vector<int> a = {1, 2, 3, 5};
  cpp20::span<int> view = a;
  auto byte_view = cpp20::as_writable_bytes(view);

  static_assert(std::is_same<decltype(byte_view), cpp20::span<cpp17::byte>>::value, "");

  EXPECT_TRUE(are_pointers_equal(byte_view.data(), view.data()));
  EXPECT_EQ(byte_view.size(), view.size_bytes());
}

TEST(SpanTest, AsWriteableBytesWithStaticExtent) {
  std::vector<int> a = {1, 2, 3, 5};
  cpp20::span<int, 4> view(a);
  auto byte_view = cpp20::as_writable_bytes(view);

  static_assert(std::is_same<decltype(byte_view), cpp20::span<cpp17::byte, 4 * sizeof(int)>>::value,
                "");

  EXPECT_TRUE(are_pointers_equal(byte_view.data(), view.data()));
  EXPECT_EQ(byte_view.size(), view.size_bytes());
}

TEST(SpanTest, AsBytesWithDynamicExtent) {
  std::vector<int> a = {1, 2, 3, 5};
  cpp20::span<int> view = a;
  auto byte_view = cpp20::as_bytes(view);

  static_assert(std::is_same<decltype(byte_view), cpp20::span<const cpp17::byte>>::value, "");

  EXPECT_TRUE(are_pointers_equal(byte_view.data(), view.data()));
  EXPECT_EQ(byte_view.size(), view.size_bytes());
}

TEST(SpanTest, AsBytesWithStaticExtent) {
  std::vector<int> a = {1, 2, 3, 5};
  cpp20::span<int, 4> view(a);
  auto byte_view = cpp20::as_bytes(view);

  static_assert(
      std::is_same<decltype(byte_view), cpp20::span<const cpp17::byte, 4 * sizeof(int)>>::value,
      "");

  EXPECT_TRUE(are_pointers_equal(byte_view.data(), view.data()));
  EXPECT_EQ(byte_view.size(), view.size_bytes());
}

#if __cpp_lib_span >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(SpanTest, IsAliasWhenStdIsAvailable) {
  static_assert(std::is_same_v<cpp20::span<int>, std::span<int>>,
                "cpp20::span must an alias of std::span when provided.");
  static_assert(&cpp20::dynamic_extent == &std::dynamic_extent);
  {
    constexpr cpp20::span<cpp17::byte> (*cpp20_as_writeable_bytes)(cpp20::span<int>) =
        &cpp20::as_writable_bytes;
    constexpr std::span<cpp17::byte> (*std_as_writeable_bytes)(std::span<int>) =
        &std::as_writable_bytes;
    static_assert(cpp20_as_writeable_bytes == std_as_writeable_bytes, "");
  }
  {
    constexpr cpp20::span<cpp17::byte, sizeof(int)> (*cpp20_as_writeable_bytes)(
        cpp20::span<int, 1>) = &cpp20::as_writable_bytes;
    constexpr std::span<cpp17::byte, sizeof(int)> (*std_as_writeable_bytes)(std::span<int, 1>) =
        &std::as_writable_bytes;
    static_assert(cpp20_as_writeable_bytes == std_as_writeable_bytes, "");
  }

  {
    constexpr cpp20::span<const cpp17::byte> (*cpp20_as_bytes)(cpp20::span<int>) = &cpp20::as_bytes;
    constexpr std::span<const cpp17::byte> (*std_as_bytes)(std::span<int>) = &std::as_bytes;
    static_assert(cpp20_as_bytes == std_as_bytes, "");
  }
  {
    constexpr cpp20::span<const cpp17::byte, sizeof(int)> (*cpp20_as_bytes)(cpp20::span<int, 1>) =
        &cpp20::as_bytes;
    constexpr std::span<const cpp17::byte, sizeof(int)> (*std_as_bytes)(std::span<int, 1>) =
        &std::as_bytes;
    static_assert(cpp20_as_bytes == std_as_bytes, "");
  }
}

#endif

#if __cpp_deduction_guides >= 201703L

TEST(SpanTest, DeductionGuideCheck) {
  std::vector<int> a = {1, 2, 3, 4};
  int b[4] = {};
  std::array<int, 4> c = {};
  const std::array<int, 4>& d = {};

  [[gnu::unused]] cpp20::span it_end_or_size{a.data(), a.size()};
  static_assert(std::is_same<decltype(it_end_or_size), cpp20::span<int>>::value, "");

  [[gnu::unused]] cpp20::span carray{b};
  static_assert(std::is_same<decltype(carray), cpp20::span<int, 4>>::value, "");

  [[gnu::unused]] cpp20::span typed_array{c};
  static_assert(std::is_same<decltype(typed_array), cpp20::span<int, 4>>::value, "");

  [[gnu::unused]] cpp20::span const_typed_array{d};
  static_assert(std::is_same<decltype(const_typed_array), cpp20::span<const int, 4>>::value, "");

  [[gnu::unused]] cpp20::span container_with_data_and_size{a};
  static_assert(std::is_same<decltype(container_with_data_and_size), cpp20::span<int>>::value, "");
}

#endif

}  // namespace
