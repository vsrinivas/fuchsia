#include "printer.h"

#include <sstream>

#include "gtest/gtest.h"

namespace fidl_codec {

TEST(PrettyPrinter, uint64_print) {
  std::stringstream out;
  PrettyPrinter printer(out, WithoutColors, "", 100, false);
  // We use variables to get the proper type to <<.
  constexpr uint64_t n = 255;
  constexpr uint64_t zero = 0;
  constexpr uint64_t sixteen = 16;
  constexpr uint64_t ten = 10;
  ASSERT_EQ(printer.remaining_size(), 100);
  printer << n;
  ASSERT_EQ(printer.remaining_size(), 97);
  printer << zero;
  ASSERT_EQ(printer.remaining_size(), 96);
  printer << std::hex << n;
  ASSERT_EQ(printer.remaining_size(), 94);
  printer << zero;
  ASSERT_EQ(printer.remaining_size(), 93);
  printer << sixteen;
  ASSERT_EQ(printer.remaining_size(), 91);
  printer << std::dec << ten;
  ASSERT_EQ(printer.remaining_size(), 89);
  ASSERT_EQ(out.str(), "2550ff01010");
}
}  // namespace fidl_codec
