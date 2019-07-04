#include <utf_conversion/utf_conversion.h>

#include <cstdint>

static uint8_t dstBuffer[4 * 1024 * 1024] = {0};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  //The amount of bytes uint16_t can be is always even.
  if (Size % 2 == 1) {
    return 0;
  }
  //utf16_to_utf8 expects the number of code units. In utf16, a code unit is 2 bytes.
  size_t code_units = Size / 2;
  const uint16_t* src = reinterpret_cast<const uint16_t*>(Data);
  size_t dst_len = sizeof(dstBuffer);
  utf16_to_utf8(src, code_units, dstBuffer, &dst_len);
  return 0;
}
