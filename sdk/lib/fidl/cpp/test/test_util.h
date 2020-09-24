#include <lib/fidl/internal.h>

#include <cstdint>
#include <ios>
#include <iostream>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/clone.h"

namespace fidl {
namespace test {
namespace util {

template <typename T>
bool cmp_payload(const T* actual, size_t actual_size, const T* expected, size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      std::cout << std::dec << "element[" << i << "]: " << std::hex << "actual=0x" << +actual[i]
                << " "
                << "expected=0x" << +expected[i] << "\n";
    }
  }
  if (actual_size != expected_size) {
    pass = false;
    std::cout << std::dec << "element[...]: "
              << "actual.size=" << +actual_size << " "
              << "expected.size=" << +expected_size << "\n";
  }
  return pass;
}

template <class Output, class Input>
Output RoundTrip(const Input& input) {
  fidl::Encoder encoder(fidl::Encoder::NoHeader::NO_HEADER);
  auto offset = encoder.Alloc(EncodingInlineSize<Input, fidl::Encoder>(&encoder));
  fidl::Clone(input).Encode(&encoder, offset);
  auto msg = encoder.GetMessage();
  const char* err_msg = nullptr;
  EXPECT_EQ(ZX_OK, msg.Validate(Output::FidlType, &err_msg)) << err_msg;

  EXPECT_EQ(ZX_OK, msg.Decode(Output::FidlType, &err_msg)) << err_msg;
  fidl::Decoder decoder(std::move(msg));
  Output output;
  Output::Decode(&decoder, &output, 0);
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

template <class Output>
Output DecodedBytes(std::vector<uint8_t> bytes, std::vector<zx_handle_t> handles) {
  Message message(BytePart(bytes.data(), bytes.capacity(), bytes.size()),
                  HandlePart(handles.data(), handles.capacity(), handles.size()));

  const char* error = nullptr;
  EXPECT_EQ(ZX_OK, message.Decode(Output::FidlType, &error)) << error;

  fidl::Decoder decoder(std::move(message));
  Output output;
  Output::Decode(&decoder, &output, 0);

  return output;
}

template <class Input>
bool ValueToBytes(const Input& input, const std::vector<uint8_t>& expected) {
  fidl::Encoder enc(fidl::Encoder::NoHeader::NO_HEADER);
  auto offset = enc.Alloc(EncodingInlineSize<Input, fidl::Encoder>(&enc));
  fidl::Clone(input).Encode(&enc, offset);
  auto msg = enc.GetMessage();
  return cmp_payload(reinterpret_cast<const uint8_t*>(msg.bytes().data()), msg.bytes().actual(),
                     reinterpret_cast<const uint8_t*>(expected.data()), expected.size());
}

template <class Input>
bool ValueToBytes(Input input, const std::vector<uint8_t>& bytes,
                  std::vector<zx_handle_t>& handles) {
  fidl::Encoder enc(fidl::Encoder::NoHeader::NO_HEADER);
  auto offset = enc.Alloc(EncodingInlineSize<Input, fidl::Encoder>(&enc));
  input.Encode(&enc, offset);
  auto msg = enc.GetMessage();
  auto bytes_match =
      cmp_payload(reinterpret_cast<const uint8_t*>(msg.bytes().data()), msg.bytes().actual(),
                  reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  auto handles_match =
      cmp_payload(reinterpret_cast<const uint8_t*>(msg.handles().data()), msg.handles().actual(),
                  reinterpret_cast<const uint8_t*>(handles.data()), handles.size());
  return bytes_match && handles_match;
}

template <class Output>
void CheckDecodeFailure(std::vector<uint8_t> input, const zx_status_t expected_failure_code) {
  Message message(BytePart(input.data(), input.capacity(), input.size()), HandlePart());

  const char* error = nullptr;
  EXPECT_EQ(expected_failure_code, message.Decode(Output::FidlType, &error)) << error;
}

template <class Input>
void CheckEncodeFailure(const Input& input, const zx_status_t expected_failure_code) {
  fidl::Encoder enc(fidl::Encoder::NoHeader::NO_HEADER);
  auto offset = enc.Alloc(EncodingInlineSize<Input, fidl::Encoder>(&enc));
  fidl::Clone(input).Encode(&enc, offset);
  auto msg = enc.GetMessage();
  const char* error = nullptr;
  EXPECT_EQ(expected_failure_code, msg.Validate(Input::FidlType, &error)) << error;
}

}  // namespace util
}  // namespace test
}  // namespace fidl
