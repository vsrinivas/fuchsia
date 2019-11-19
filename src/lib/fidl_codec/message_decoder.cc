// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message_decoder.h"

#include <ostream>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_parser.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

std::string DocumentToString(rapidjson::Document* document) {
  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  document->Accept(writer);
  return output.GetString();
}

bool MessageDecoderDispatcher::DecodeMessage(uint64_t process_koid, zx_handle_t handle,
                                             const uint8_t* bytes, uint32_t num_bytes,
                                             const zx_handle_info_t* handles, uint32_t num_handles,
                                             SyscallFidlType type, std::ostream& os,
                                             std::string_view line_header, int tabs) {
  if (loader_ == nullptr) {
    return false;
  }
  if ((bytes == nullptr) || (num_bytes < sizeof(fidl_message_header_t))) {
    os << line_header << std::string(tabs * kTabSize, ' ') << "not enough data for message\n";
    return false;
  }
  auto header = reinterpret_cast<const fidl_message_header_t*>(bytes);
  const std::vector<const InterfaceMethod*>* methods = loader_->GetByOrdinal(header->ordinal);
  if (methods == nullptr || methods->empty()) {
    os << line_header << std::string(tabs * kTabSize, ' ') << "Protocol method with ordinal 0x"
       << std::hex << header->ordinal << " not found\n";
    return false;
  }

  const InterfaceMethod* method = (*methods)[0];

  std::unique_ptr<Object> decoded_request;
  std::stringstream request_error_stream;
  bool matched_request = DecodeRequest(method, bytes, num_bytes, handles, num_handles,
                                       &decoded_request, request_error_stream);

  std::unique_ptr<Object> decoded_response;
  std::stringstream response_error_stream;
  bool matched_response = DecodeResponse(method, bytes, num_bytes, handles, num_handles,
                                         &decoded_response, response_error_stream);

  Direction direction = Direction::kUnknown;
  auto handle_direction = handle_directions_.find(std::make_tuple(handle, process_koid));
  if (handle_direction != handle_directions_.end()) {
    direction = handle_direction->second;
  } else {
    // This is the first read or write we intercept for this handle/koid. If we
    // launched the process, we suppose we intercepted the very first read or
    // write.
    // If this is not an event (which would mean method->request() is null), a
    // write means that we are watching a client (a client starts by writing a
    // request) and a read means that we are watching a server (a server starts
    // by reading the first client request).
    // If we attached to a running process, we can only determine correctly if
    // we are watching a client or a server if we have only one matched_request
    // or one matched_response.
    if (IsLaunchedProcess(process_koid) || (matched_request != matched_response)) {
      // We launched the process or exactly one of request and response are
      // valid => we can determine the direction.
      switch (type) {
        case SyscallFidlType::kOutputMessage:
          handle_directions_[std::make_tuple(handle, process_koid)] =
              (method->request() != nullptr) ? Direction::kClient : Direction::kServer;
          break;
        case SyscallFidlType::kInputMessage:
          handle_directions_[std::make_tuple(handle, process_koid)] =
              (method->request() != nullptr) ? Direction::kServer : Direction::kClient;
          break;
        case SyscallFidlType::kOutputRequest:
        case SyscallFidlType::kInputResponse:
          handle_directions_[std::make_tuple(handle, process_koid)] = Direction::kClient;
      }
      direction = handle_directions_[std::make_tuple(handle, process_koid)];
    }
  }
  bool is_request = false;
  const char* message_direction = "";
  switch (type) {
    case SyscallFidlType::kOutputMessage:
      if (direction == Direction::kClient) {
        is_request = true;
      }
      message_direction = "sent ";
      break;
    case SyscallFidlType::kInputMessage:
      if (direction == Direction::kServer) {
        is_request = true;
      }
      message_direction = "received ";
      break;
    case SyscallFidlType::kOutputRequest:
      is_request = true;
      message_direction = "sent ";
      break;
    case SyscallFidlType::kInputResponse:
      message_direction = "received ";
      break;
  }
  if (direction != Direction::kUnknown) {
    if ((is_request && !matched_request) || (!is_request && !matched_response)) {
      if ((is_request && matched_response) || (!is_request && matched_request)) {
        if ((type == SyscallFidlType::kOutputRequest) ||
            (type == SyscallFidlType::kInputResponse)) {
          // We know the direction: we can't be wrong => we haven't been able to decode the message.
          return false;
        }
        // The first determination seems to be wrong. That is, we are expecting
        // a request but only a response has been successfully decoded or we are
        // expecting a response but only a request has been successfully
        // decoded.
        // Invert the deduction which should now be the right one.
        handle_directions_[std::make_tuple(handle, process_koid)] =
            (direction == Direction::kClient) ? Direction::kServer : Direction::kClient;
        is_request = !is_request;
      }
    }
  }

  rapidjson::Document actual_request;
  rapidjson::Document actual_response;
  if (!display_options_.pretty_print) {
    if (decoded_request != nullptr) {
      decoded_request->ExtractJson(actual_request.GetAllocator(), actual_request);
    }

    if (decoded_response != nullptr) {
      decoded_response->ExtractJson(actual_response.GetAllocator(), actual_response);
    }
  }

  if (direction == Direction::kUnknown) {
    os << line_header << std::string(tabs * kTabSize, ' ') << colors_.red
       << "Can't determine request/response." << colors_.reset << " it can be:\n";
    ++tabs;
  }

  if (matched_request && (is_request || (direction == Direction::kUnknown))) {
    os << line_header << std::string(tabs * kTabSize, ' ') << colors_.white_on_magenta
       << message_direction << "request" << colors_.reset << ' ' << colors_.green
       << method->enclosing_interface().name() << '.' << method->name() << colors_.reset << " = ";
    if (display_options_.pretty_print) {
      decoded_request->PrettyPrint(os, colors_, header, line_header, tabs, tabs * kTabSize,
                                   display_options_.columns);
    } else {
      os << DocumentToString(&actual_request);
    }
    os << '\n';
  }
  if (matched_response && (!is_request || (direction == Direction::kUnknown))) {
    os << line_header << std::string(tabs * kTabSize, ' ') << colors_.white_on_magenta
       << message_direction << "response" << colors_.reset << ' ' << colors_.green
       << method->enclosing_interface().name() << '.' << method->name() << colors_.reset << " = ";
    if (display_options_.pretty_print) {
      decoded_response->PrettyPrint(os, colors_, header, line_header, tabs, tabs * kTabSize,
                                    display_options_.columns);
    } else {
      os << DocumentToString(&actual_response);
    }
    os << '\n';
  }
  if (matched_request || matched_response) {
    return true;
  }
  std::string request_errors = request_error_stream.str();
  if (!request_errors.empty()) {
    os << line_header << std::string(tabs * kTabSize, ' ') << colors_.red << message_direction
       << "request errors" << colors_.reset << ":\n"
       << request_errors;
    if (decoded_request != nullptr) {
      os << line_header << std::string(tabs * kTabSize, ' ') << colors_.white_on_magenta
         << message_direction << "request" << colors_.reset << ' ' << colors_.green
         << method->enclosing_interface().name() << '.' << method->name() << colors_.reset << " = ";
      decoded_request->PrettyPrint(os, colors_, header, line_header, tabs, tabs * kTabSize,
                                   display_options_.columns);
      os << '\n';
    }
  }
  std::string response_errors = response_error_stream.str();
  if (!response_errors.empty()) {
    os << line_header << std::string(tabs * kTabSize, ' ') << colors_.red << message_direction
       << "response errors" << colors_.reset << ":\n"
       << response_errors;
    if (decoded_response != nullptr) {
      os << line_header << std::string(tabs * kTabSize, ' ') << colors_.white_on_magenta
         << message_direction << "request" << colors_.reset << ' ' << colors_.green
         << method->enclosing_interface().name() << '.' << method->name() << colors_.reset << " = ";
      decoded_response->PrettyPrint(os, colors_, header, line_header, tabs, tabs * kTabSize,
                                    display_options_.columns);
      os << '\n';
    }
  }
  return false;
}

MessageDecoder::MessageDecoder(const uint8_t* bytes, uint32_t num_bytes,
                               const zx_handle_info_t* handles, uint32_t num_handles,
                               std::ostream& error_stream)
    : num_bytes_(num_bytes),
      start_byte_pos_(bytes),
      end_handle_pos_(handles + num_handles),
      handle_pos_(handles),
      unions_are_xunions_(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(bytes))),
      error_stream_(error_stream) {}

MessageDecoder::MessageDecoder(MessageDecoder* container, uint64_t offset, uint64_t num_bytes,
                               uint64_t num_handles)
    : absolute_offset_(container->absolute_offset() + offset),
      num_bytes_(num_bytes),
      start_byte_pos_(container->start_byte_pos_ + offset),
      end_handle_pos_(container->handle_pos_ + num_handles),
      handle_pos_(container->handle_pos_),
      unions_are_xunions_(container->unions_are_xunions_),
      error_stream_(container->error_stream_) {
  container->handle_pos_ += num_handles;
}

std::unique_ptr<Object> MessageDecoder::DecodeMessage(const Struct& message_format) {
  // Set the offset for the next object (just after this one).
  SkipObject(message_format.Size(this));
  // Decode the object.
  std::unique_ptr<Object> object = message_format.DecodeObject(this, /*type=*/nullptr,
                                                               /*offset=*/0, /*nullable=*/false);
  // It's an error if we didn't use all the bytes in the buffer.
  if (next_object_offset_ != num_bytes_) {
    AddError() << "Message not fully decoded (decoded=" << next_object_offset_
               << ", size=" << num_bytes_ << ")\n";
  }
  // It's an error if we didn't use all the handles in the buffer.
  if (GetRemainingHandles() != 0) {
    AddError() << "Message not fully decoded (remain " << GetRemainingHandles() << " handles)\n";
  }
  return object;
}

std::unique_ptr<Value> MessageDecoder::DecodeValue(const Type* type) {
  if (type == nullptr) {
    return nullptr;
  }
  // Set the offset for the next object (just after this one).
  SkipObject(type->InlineSize(this));
  // Decode the envelope.
  std::unique_ptr<Value> result = type->Decode(this, 0);
  // It's an error if we didn't use all the bytes in the buffer.
  if (next_object_offset_ != num_bytes_) {
    AddError() << "Message envelope not fully decoded (decoded=" << next_object_offset_
               << ", size=" << num_bytes_ << ")\n";
  }
  // It's an error if we didn't use all the handles in the buffer.
  if (GetRemainingHandles() != 0) {
    AddError() << "Message envelope not fully decoded (remain " << GetRemainingHandles()
               << " handles)\n";
  }
  return result;
}

}  // namespace fidl_codec
