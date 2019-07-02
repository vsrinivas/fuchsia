#include <lib/fidl/internal.h>

#include <iostream>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/clone.h"

namespace fidl {
namespace test {
namespace util {

bool cmp_payload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                 size_t expected_size);

template <class Output, class Input>
Output RoundTrip(const Input& input) {
  const size_t input_encoded_size = CodingTraits<Input>::encoded_size;
  const size_t input_padding_size = FIDL_ALIGN(input_encoded_size) - input_encoded_size;
  const ::fidl::FidlStructField fake_input_interface_fields[] = {
      ::fidl::FidlStructField(Input::FidlType, 16, input_padding_size),
  };
  const fidl_type_t fake_input_interface_struct{
      ::fidl::FidlCodedStruct(fake_input_interface_fields, 1, 16 + input_encoded_size, "Input")};
  const size_t output_encoded_size = CodingTraits<Input>::encoded_size;
  const size_t output_padding_size = FIDL_ALIGN(output_encoded_size) - output_encoded_size;
  const ::fidl::FidlStructField fake_output_interface_fields[] = {
      ::fidl::FidlStructField(Output::FidlType, 16, output_padding_size),
  };
  const fidl_type_t fake_output_interface_struct{
      ::fidl::FidlCodedStruct(fake_output_interface_fields, 1, 16 + output_encoded_size, "Output")};

  fidl::Encoder enc(0xfefefefe);
  auto ofs = enc.Alloc(input_encoded_size);
  fidl::Clone(input).Encode(&enc, ofs);
  auto msg = enc.GetMessage();

  const char* err_msg = nullptr;
  EXPECT_EQ(ZX_OK, msg.Validate(&fake_input_interface_struct, &err_msg)) << err_msg;
  EXPECT_EQ(ZX_OK, msg.Decode(&fake_output_interface_struct, &err_msg)) << err_msg;
  fidl::Decoder dec(std::move(msg));
  Output output;
  Output::Decode(&dec, &output, ofs);
  return output;
}

template <class Output>
Output DecodedBytes(std::vector<uint8_t> input) {
  Message message(BytePart(input.data(), input.capacity(), input.size()), HandlePart());

  const char* error = nullptr;
  EXPECT_EQ(ZX_OK, message.Decode(Output::FidlType, &error)) << error;

  fidl::Decoder decoder(std::move(message));
  Output output;
  Output::Decode(&decoder, &output, 0);

  return output;
}

template <class Input>
bool ValueToBytes(const Input& input, const std::vector<uint8_t>& expected) {
  fidl::Encoder enc(0xfefefefe);
  auto offset = enc.Alloc(CodingTraits<Input>::encoded_size);
  fidl::Clone(input).Encode(&enc, offset);
  auto msg = enc.GetMessage();
  auto payload = msg.payload();
  return cmp_payload(reinterpret_cast<const uint8_t*>(payload.data()), payload.actual(),
                     reinterpret_cast<const uint8_t*>(expected.data()), expected.size());
}

}  // namespace util
}  // namespace test
}  // namespace fidl
