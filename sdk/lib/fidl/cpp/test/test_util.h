#include <lib/fidl/internal.h>

#include <iostream>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/clone.h"

namespace fidl {
namespace test {
namespace util {

bool cmp_payload(const uint8_t* actual, size_t actual_size, const uint8_t* expected,
                 size_t expected_size);

class EncoderFactoryV1 {
 public:
  static Encoder makeEncoder() {
    Encoder enc(0xfefefefe);
    return enc;
  }
};

template <class T, class Callback>
void WithCodingTableFor(const fidl::Encoder& enc, Callback callback) {
  const size_t encoded_size = fidl::CodingTraits<T>::inline_size_v1_no_ee;
  const size_t padding_size = FIDL_ALIGN(encoded_size) - encoded_size;

  const FidlStructField fake_interface_fields[] = {
      FidlStructField(T::FidlType, sizeof(fidl_message_header_t), padding_size),
  };

  const fidl_type_t fake_interface_struct = {
      .type_tag = kFidlTypeStruct,
      {.coded_struct = {.fields = fake_interface_fields,
                        .field_count = 1,
                        .size = static_cast<uint32_t>(sizeof(fidl_message_header_t) + encoded_size),
                        .max_out_of_line = UINT32_MAX,
                        .contains_union = true,
                        .name = "Input",
                        .alt_type = &fake_interface_struct}},
  };

  callback(encoded_size, &fake_interface_struct);
}

template <class Output, class Input>
Output RoundTrip(const Input& input) {
  fidl::Encoder enc(0xfefefefe);
  const char* err_msg = nullptr;
  Output output;

  WithCodingTableFor<Input>(enc, [&](size_t encoded_size, const fidl_type_t* type) {
    auto ofs = enc.Alloc(encoded_size);
    fidl::Clone(input).Encode(&enc, ofs);
    auto msg = enc.GetMessage();
    EXPECT_EQ(ZX_OK, msg.Validate(type, &err_msg)) << err_msg;

    WithCodingTableFor<Output>(enc, [&](size_t encoded_size, const fidl_type_t* type) {
      EXPECT_EQ(ZX_OK, msg.Decode(type, &err_msg)) << err_msg;
      fidl::Decoder dec(std::move(msg));
      Output::Decode(&dec, &output, ofs);
    });
  });

  return output;
}

template <class Output>
Output DecodedBytes(std::vector<uint8_t> input) {
  // Create a fake coded type for the input object with added header.
  FidlStructField fields_v1[] = {
      FidlStructField(Output::FidlType, sizeof(fidl_message_header_t), 0u)};
  assert(Output::FidlType->type_tag == kFidlTypeStruct);
  fidl_type_t obj_with_header_v1 = {
      .type_tag = kFidlTypeStruct,
      {.coded_struct = {
           .fields = fields_v1,
           .field_count = 1u,
           .size = static_cast<uint32_t>(sizeof(fidl_message_header_t) +
                                         FIDL_ALIGN(Output::FidlType->coded_struct.size)),
           .max_out_of_line = UINT32_MAX,
           .contains_union = true,
           .name = "",
           .alt_type = &obj_with_header_v1}}};
  Message message(BytePart(input.data(), input.capacity(), input.size()), HandlePart());

  const char* error = nullptr;
  EXPECT_EQ(ZX_OK, message.Decode(&obj_with_header_v1, &error)) << error;

  fidl::Decoder decoder(std::move(message));
  Output output;
  Output::Decode(&decoder, &output, sizeof(fidl_message_header_t));

  return output;
}

template <class Input, class EncoderFactory = EncoderFactoryV1>
bool ValueToBytes(const Input& input, const std::vector<uint8_t>& expected) {
  auto enc = EncoderFactory::makeEncoder();
  auto offset = enc.Alloc(EncodingInlineSize<Input, fidl::Encoder>(&enc));
  fidl::Clone(input).Encode(&enc, offset);
  auto msg = enc.GetMessage();
  auto payload = msg.payload();
  return cmp_payload(reinterpret_cast<const uint8_t*>(payload.data()), payload.actual(),
                     reinterpret_cast<const uint8_t*>(expected.data()), expected.size());
}

template <class Output>
void CheckDecodeFailure(std::vector<uint8_t> input, const zx_status_t expected_failure_code) {
  Message message(BytePart(input.data(), input.capacity(), input.size()), HandlePart());

  const char* error = nullptr;
  EXPECT_EQ(expected_failure_code, message.Decode(Output::FidlType, &error)) << error;
}

template <class Input, class EncoderFactory = EncoderFactoryV1>
void CheckEncodeFailure(const Input& input, const zx_status_t expected_failure_code) {
  auto enc = EncoderFactory::makeEncoder();
  auto offset = enc.Alloc(EncodingInlineSize<Input, fidl::Encoder>(&enc));
  fidl::Clone(input).Encode(&enc, offset);
  auto msg = enc.GetMessage();
  const char* error = nullptr;
  EXPECT_EQ(expected_failure_code, msg.Validate(Input::FidlType, &error)) << error;
}

}  // namespace util
}  // namespace test
}  // namespace fidl
