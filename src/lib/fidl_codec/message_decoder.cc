// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message_decoder.h"

#include <ostream>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/status.h"
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

bool DecodedMessage::DecodeMessage(MessageDecoderDispatcher* dispatcher, uint64_t process_koid,
                                   zx_handle_t handle, const uint8_t* bytes, uint32_t num_bytes,
                                   const zx_handle_info_t* handles, uint32_t num_handles,
                                   SyscallFidlType type, std::ostream& os,
                                   std::string_view line_header, int tabs) {
  if (dispatcher->loader() == nullptr) {
    return false;
  }
  if ((bytes == nullptr) || (num_bytes < sizeof(fidl_message_header_t))) {
    os << line_header << std::string(tabs * kTabSize, ' ') << "not enough data for message\n";
    return false;
  }
  header_ = reinterpret_cast<const fidl_message_header_t*>(bytes);

  // Handle the epitaph header explicitly.
  if (header_->ordinal == kFidlOrdinalEpitaph) {
    if (num_bytes < sizeof(fidl_epitaph)) {
      os << line_header << std::string(tabs * kTabSize, ' ') << "not enough data for epitaph\n";
      return false;
    }
    switch (type) {
      case SyscallFidlType::kOutputRequest:
      case SyscallFidlType::kOutputMessage:
        message_direction_ = "sent ";
        break;
      case SyscallFidlType::kInputResponse:
      case SyscallFidlType::kInputMessage:
        message_direction_ = "received ";
        break;
    }
    return true;
  }

  const std::vector<const InterfaceMethod*>* methods =
      dispatcher->loader()->GetByOrdinal(header_->ordinal);
  if (methods == nullptr || methods->empty()) {
    os << line_header << std::string(tabs * kTabSize, ' ') << "Protocol method with ordinal 0x"
       << std::hex << header_->ordinal << " not found\n";
    return false;
  }

  method_ = (*methods)[0];

  matched_request_ = DecodeRequest(method_, bytes, num_bytes, handles, num_handles,
                                   &decoded_request_, request_error_stream_);
  matched_response_ = DecodeResponse(method_, bytes, num_bytes, handles, num_handles,
                                     &decoded_response_, response_error_stream_);

  direction_ = dispatcher->ComputeDirection(process_koid, handle, type, method_,
                                            matched_request_ != matched_response_);
  switch (type) {
    case SyscallFidlType::kOutputMessage:
      if (direction_ == Direction::kClient) {
        is_request_ = true;
      }
      message_direction_ = "sent ";
      break;
    case SyscallFidlType::kInputMessage:
      if (direction_ == Direction::kServer) {
        is_request_ = true;
      }
      message_direction_ = "received ";
      break;
    case SyscallFidlType::kOutputRequest:
      is_request_ = true;
      message_direction_ = "sent ";
      break;
    case SyscallFidlType::kInputResponse:
      message_direction_ = "received ";
      break;
  }
  if (direction_ != Direction::kUnknown) {
    if ((is_request_ && !matched_request_) || (!is_request_ && !matched_response_)) {
      if ((is_request_ && matched_response_) || (!is_request_ && matched_request_)) {
        if ((type == SyscallFidlType::kOutputRequest) ||
            (type == SyscallFidlType::kInputResponse)) {
          // We know the direction: we can't be wrong => we haven't been able to decode the message.
          // However, we can still display something.
          return true;
        }
        // The first determination seems to be wrong. That is, we are expecting
        // a request but only a response has been successfully decoded or we are
        // expecting a response but only a request has been successfully
        // decoded.
        // Invert the deduction which should now be the right one.
        dispatcher->UpdateDirection(
            process_koid, handle,
            (direction_ == Direction::kClient) ? Direction::kServer : Direction::kClient);
        is_request_ = !is_request_;
      }
    }
  }
  return true;
}

bool DecodedMessage::Display(const Colors& colors, bool pretty_print, int columns, std::ostream& os,
                             std::string_view line_header, int tabs) {
  if (header_->ordinal == kFidlOrdinalEpitaph) {
    auto epitaph = reinterpret_cast<const fidl_epitaph_t*>(header_);
    PrettyPrinter printer(os, colors, line_header, columns, tabs);
    printer << WhiteOnMagenta << message_direction_ << "epitaph" << ResetColor << ' ' << Red
            << StatusName(epitaph->error) << ResetColor << "\n";
    return true;
  }

  if (direction_ == Direction::kUnknown) {
    if (matched_request_ || matched_response_) {
      os << line_header << std::string(tabs * kTabSize, ' ') << colors.red
         << "Can't determine request/response." << colors.reset << " it can be:\n";
    } else {
      os << line_header << std::string(tabs * kTabSize, ' ') << colors.red
         << "Can't decode message." << colors.reset << '\n';
    }

    ++tabs;
  }

  if (matched_request_ && (is_request_ || (direction_ == Direction::kUnknown))) {
    PrettyPrinter printer(os, colors, line_header, columns, /*header_on_every_line=*/true, tabs);
    printer << WhiteOnMagenta << message_direction_ << "request" << ResetColor << ' ' << Green
            << method_->enclosing_interface().name() << '.' << method_->name() << ResetColor
            << " = ";
    if (pretty_print) {
      decoded_request_->PrettyPrint(nullptr, printer);
      printer << '\n';
    } else {
      rapidjson::Document actual_request;
      if (decoded_request_ != nullptr) {
        decoded_request_->ExtractJson(actual_request.GetAllocator(), actual_request);
      }
      os << DocumentToString(&actual_request) << '\n';
    }
  }
  if (matched_response_ && (!is_request_ || (direction_ == Direction::kUnknown))) {
    PrettyPrinter printer(os, colors, line_header, columns, /*header_on_every_line=*/true, tabs);
    printer << WhiteOnMagenta << message_direction_ << "response" << ResetColor << ' ' << Green
            << method_->enclosing_interface().name() << '.' << method_->name() << ResetColor
            << " = ";
    if (pretty_print) {
      decoded_response_->PrettyPrint(nullptr, printer);
      printer << '\n';
    } else {
      rapidjson::Document actual_response;
      if (decoded_response_ != nullptr) {
        decoded_response_->ExtractJson(actual_response.GetAllocator(), actual_response);
      }
      os << DocumentToString(&actual_response) << '\n';
    }
  }
  if (matched_request_ || matched_response_) {
    return true;
  }
  std::string request_errors = request_error_stream_.str();
  if (!request_errors.empty()) {
    PrettyPrinter printer(os, colors, line_header, columns, /*header_on_every_line=*/true, tabs);
    printer << Red << message_direction_ << "request errors" << ResetColor << ":\n";
    {
      Indent indent(printer);
      printer << request_errors;
    }
    if (decoded_request_ != nullptr) {
      printer << WhiteOnMagenta << message_direction_ << "request" << ResetColor << ' ' << Green
              << method_->enclosing_interface().name() << '.' << method_->name() << ResetColor
              << " = ";
      decoded_request_->PrettyPrint(nullptr, printer);
      printer << '\n';
    }
  }
  std::string response_errors = response_error_stream_.str();
  if (!response_errors.empty()) {
    PrettyPrinter printer(os, colors, line_header, columns, /*header_on_every_line=*/true, tabs);
    printer << Red << message_direction_ << "response errors" << colors.reset << ":\n";
    {
      Indent indent(printer);
      printer << response_errors;
    }
    if (decoded_response_ != nullptr) {
      printer << WhiteOnMagenta << message_direction_ << "response" << ResetColor << ' ' << Green
              << method_->enclosing_interface().name() << '.' << method_->name() << ResetColor
              << " = ";
      decoded_response_->PrettyPrint(nullptr, printer);
      printer << '\n';
    }
  }
  return false;
}

bool MessageDecoderDispatcher::DecodeMessage(uint64_t process_koid, zx_handle_t handle,
                                             const uint8_t* bytes, uint32_t num_bytes,
                                             const zx_handle_info_t* handles, uint32_t num_handles,
                                             SyscallFidlType type, std::ostream& os,
                                             std::string_view line_header, int tabs) {
  DecodedMessage message;
  if (!message.DecodeMessage(this, process_koid, handle, bytes, num_bytes, handles, num_handles,
                             type, os, line_header, tabs)) {
    return false;
  }
  return message.Display(colors_, display_options_.pretty_print, display_options_.columns, os,
                         line_header, tabs);
}

Direction MessageDecoderDispatcher::ComputeDirection(uint64_t process_koid, zx_handle_t handle,
                                                     SyscallFidlType type,
                                                     const InterfaceMethod* method,
                                                     bool only_one_valid) {
  auto handle_direction = handle_directions_.find(std::make_tuple(handle, process_koid));
  if (handle_direction != handle_directions_.end()) {
    return handle_direction->second;
  }
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
  if (IsLaunchedProcess(process_koid) || only_one_valid) {
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
    return handle_directions_[std::make_tuple(handle, process_koid)];
  }
  return Direction::kUnknown;
}

MessageDecoder::MessageDecoder(const uint8_t* bytes, uint32_t num_bytes,
                               const zx_handle_info_t* handles, uint32_t num_handles,
                               std::ostream& error_stream)
    : num_bytes_(num_bytes),
      start_byte_pos_(bytes),
      end_handle_pos_(handles + num_handles),
      handle_pos_(handles),
      error_stream_(error_stream) {}

MessageDecoder::MessageDecoder(MessageDecoder* container, uint64_t offset, uint64_t num_bytes,
                               uint64_t num_handles)
    : absolute_offset_(container->absolute_offset() + offset),
      num_bytes_(num_bytes),
      start_byte_pos_(container->start_byte_pos_ + offset),
      end_handle_pos_(container->handle_pos_ + num_handles),
      handle_pos_(container->handle_pos_),
      error_stream_(container->error_stream_) {
  container->handle_pos_ += num_handles;
}

std::unique_ptr<StructValue> MessageDecoder::DecodeMessage(const Struct& message_format) {
  // Set the offset for the next object (just after this one).
  SkipObject(message_format.size());
  // Decode the message.
  std::unique_ptr<StructValue> message = DecodeStruct(message_format, 0);
  // It's an error if we didn't use all the bytes in the buffer.
  if (next_object_offset_ != num_bytes_) {
    AddError() << "Message not fully decoded (decoded=" << next_object_offset_
               << ", size=" << num_bytes_ << ")\n";
  }
  // It's an error if we didn't use all the handles in the buffer.
  if (GetRemainingHandles() != 0) {
    AddError() << "Message not fully decoded (remain " << GetRemainingHandles() << " handles)\n";
  }
  return message;
}

std::unique_ptr<Value> MessageDecoder::DecodeValue(const Type* type) {
  if (type == nullptr) {
    return nullptr;
  }
  // Set the offset for the next object (just after this one).
  SkipObject(type->InlineSize());
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

std::unique_ptr<StructValue> MessageDecoder::DecodeStruct(const Struct& struct_definition,
                                                          uint64_t offset) {
  std::unique_ptr<StructValue> result = std::make_unique<StructValue>(struct_definition);
  for (const auto& member : struct_definition.members()) {
    std::unique_ptr<Value> value = member->type()->Decode(this, offset + member->offset());
    result->AddField(member.get(), std::move(value));
  }
  return result;
}

bool MessageDecoder::DecodeNullableHeader(uint64_t offset, uint64_t size, bool* is_null,
                                          uint64_t* nullable_offset) {
  uintptr_t data;
  if (!GetValueAt(offset, &data)) {
    return false;
  }

  if (data == FIDL_ALLOC_ABSENT) {
    *is_null = true;
    *nullable_offset = 0;
    return true;
  }
  if (data != FIDL_ALLOC_PRESENT) {
    AddError() << std::hex << (absolute_offset() + offset) << std::dec << ": Invalid value <"
               << std::hex << data << std::dec << "> for nullable\n";
    return false;
  }
  *is_null = false;
  *nullable_offset = next_object_offset();
  // Set the offset for the next object (just after this one).
  SkipObject(size);
  return true;
}

std::unique_ptr<Value> MessageDecoder::DecodeEnvelope(uint64_t offset, const Type* type) {
  FXL_DCHECK(type != nullptr);
  uint32_t envelope_bytes;
  GetValueAt(offset, &envelope_bytes);
  offset += sizeof(envelope_bytes);
  uint32_t envelope_handles;
  GetValueAt(offset, &envelope_handles);
  offset += sizeof(envelope_handles);
  bool is_null;
  uint64_t nullable_offset;
  if (!DecodeNullableHeader(offset, envelope_bytes, &is_null, &nullable_offset)) {
    return std::make_unique<InvalidValue>();
  }
  if (is_null) {
    if (envelope_bytes != 0) {
      AddError() << std::hex << (absolute_offset() + offset) << std::dec
                 << ": Null envelope shouldn't have bytes\n";
    }
    if (envelope_handles != 0) {
      AddError() << std::hex << (absolute_offset() + offset) << std::dec
                 << ": Null envelope shouldn't have handles\n";
    }
    return std::make_unique<NullValue>();
  }
  if (envelope_bytes > num_bytes() - nullable_offset) {
    AddError() << std::hex << (absolute_offset() + nullable_offset) << std::dec
               << ": Not enough data to decode an envelope\n";
    return std::make_unique<InvalidValue>();
  }
  if (envelope_handles > GetRemainingHandles()) {
    AddError() << std::hex << (absolute_offset() + nullable_offset) << std::dec
               << ": Not enough handles to decode an envelope\n";
    return std::make_unique<InvalidValue>();
  }
  MessageDecoder envelope_decoder(this, nullable_offset, envelope_bytes, envelope_handles);
  return envelope_decoder.DecodeValue(type);
}

bool MessageDecoder::CheckNullEnvelope(uint64_t offset) {
  uint32_t envelope_bytes;
  GetValueAt(offset, &envelope_bytes);
  offset += sizeof(envelope_bytes);
  uint32_t envelope_handles;
  GetValueAt(offset, &envelope_handles);
  offset += sizeof(envelope_handles);
  bool is_null;
  uint64_t nullable_offset;
  if (!DecodeNullableHeader(offset, envelope_bytes, &is_null, &nullable_offset)) {
    return false;
  }
  if (!is_null) {
    AddError() << std::hex << (absolute_offset() + offset) << std::dec
               << ": Expecting null envelope\n";
    return false;
  }
  if (envelope_bytes != 0) {
    AddError() << std::hex << (absolute_offset() + offset) << std::dec
               << ": Null envelope shouldn't have bytes\n";
    return false;
  }
  if (envelope_handles != 0) {
    AddError() << std::hex << (absolute_offset() + offset) << std::dec
               << ": Null envelope shouldn't have handles\n";
    return false;
  }
  return true;
}

void MessageDecoder::SkipEnvelope(uint64_t offset) {
  uint32_t envelope_bytes;
  GetValueAt(offset, &envelope_bytes);
  offset += sizeof(envelope_bytes);
  uint32_t envelope_handles;
  GetValueAt(offset, &envelope_handles);
  offset += sizeof(envelope_handles);
  bool is_null;
  uint64_t nullable_offset;
  if (!DecodeNullableHeader(offset, envelope_bytes, &is_null, &nullable_offset)) {
    return;
  }
  if (is_null) {
    if (envelope_bytes != 0) {
      AddError() << std::hex << (absolute_offset() + offset) << std::dec
                 << ": Null envelope shouldn't have bytes\n";
    }
    if (envelope_handles != 0) {
      AddError() << std::hex << (absolute_offset() + offset) << std::dec
                 << ": Null envelope shouldn't have handles\n";
    }
    return;
  }
  if (envelope_bytes > num_bytes() - nullable_offset) {
    AddError() << std::hex << (absolute_offset() + nullable_offset) << std::dec
               << ": Not enough data to decode an envelope\n";
    return;
  }
  if (envelope_handles > GetRemainingHandles()) {
    AddError() << std::hex << (absolute_offset() + nullable_offset) << std::dec
               << ": Not enough handles to decode an envelope\n";
    return;
  }
}

}  // namespace fidl_codec
