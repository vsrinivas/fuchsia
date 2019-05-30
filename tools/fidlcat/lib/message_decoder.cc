// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message_decoder.h"

#include <src/lib/fxl/logging.h>

#include <ostream>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_object.h"
#include "tools/fidlcat/lib/wire_parser.h"
#include "tools/fidlcat/lib/wire_types.h"

namespace fidlcat {

MessageDecoder::MessageDecoder(const fidl::Message& message, bool output_errors)
    : start_byte_pos_(message.bytes().begin()),
      start_handle_pos_(message.handles().begin()),
      end_byte_pos_(message.bytes().end()),
      end_handle_pos_(message.handles().end()),
      byte_pos_(message.bytes().begin()),
      handle_pos_(message.handles().begin()),
      output_errors_(output_errors) {}

MessageDecoder::MessageDecoder(const MessageDecoder* container,
                               uint64_t num_bytes, uint64_t num_handles)
    : start_byte_pos_(container->byte_pos_),
      start_handle_pos_(container->handle_pos_),
      end_byte_pos_(container->byte_pos_ + num_bytes),
      end_handle_pos_(container->handle_pos_ + num_handles),
      byte_pos_(container->byte_pos_),
      handle_pos_(container->handle_pos_),
      output_errors_(container->output_errors_) {}

std::unique_ptr<Object> MessageDecoder::DecodeMessage(
    const Struct& message_format) {
  std::unique_ptr<Object> result =
      message_format.DecodeObject(this, /*name=*/"", /*type=*/nullptr,
                                  /*offset=*/0, /*nullable=*/false);
  GotoNextObjectOffset(message_format.size());
  for (size_t i = 0; i < secondary_objects_.size(); ++i) {
    secondary_objects_[i]->DecodeContent(this);
  }
  return result;
}

std::unique_ptr<Field> MessageDecoder::DecodeField(std::string_view name,
                                                   const Type* type) {
  std::unique_ptr<Field> result = type->Decode(this, name, 0);
  GotoNextObjectOffset(type->InlineSize());
  for (size_t i = 0; i < secondary_objects_.size(); ++i) {
    secondary_objects_[i]->DecodeContent(this);
  }
  return result;
}

std::string DocumentToString(rapidjson::Document& document) {
  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  document.Accept(writer);
  return output.GetString();
}

void DecodeMessage(
    LibraryLoader* loader,
    std::map<std::tuple<zx_handle_t, uint64_t>, Direction>* handle_directions,
    const DisplayOptions& options, uint64_t process_koid, zx_handle_t handle,
    const fidl::Message& message, bool read, std::ostream& os) {
  fidl_message_header_t header = message.header();
  const InterfaceMethod* method = loader->GetByOrdinal(header.ordinal);
  if (method == nullptr) {
    // Probably should print out raw bytes here instead.
    FXL_LOG(WARNING) << "Protocol method with ordinal " << header.ordinal
                     << " not found";
    return;
  }

  std::unique_ptr<Object> decoded_request;
  bool matched_request = DecodeRequest(method, message, &decoded_request);

  std::unique_ptr<Object> decoded_response;
  bool matched_response = DecodeResponse(method, message, &decoded_response);

  Direction direction = Direction::kUnknown;
  auto handle_direction =
      handle_directions->find(std::make_tuple(handle, process_koid));
  if (handle_direction != handle_directions->end()) {
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
    if ((process_koid == ULLONG_MAX) || (matched_request != matched_response)) {
      // We lanched the process or exactly one of request and response are
      // valid => we can determine the direction.
      // Currently, a process_koid of ULLONG_MAX means that we launched the
      // process.
      if (read) {
        (*handle_directions)[std::make_tuple(handle, process_koid)] =
            (method->request() != nullptr) ? Direction::kServer
                                           : Direction::kClient;
      } else {
        (*handle_directions)[std::make_tuple(handle, process_koid)] =
            (method->request() != nullptr) ? Direction::kClient
                                           : Direction::kServer;
      }
      direction = (*handle_directions)[std::make_tuple(handle, process_koid)];
    }
  }
  bool is_request = false;
  if (read == (direction == Direction::kServer)) {
    is_request = true;
  }
  if (direction != Direction::kUnknown) {
    if ((is_request && !matched_request) ||
        (!is_request && !matched_response)) {
      if ((is_request && matched_response) ||
          (!is_request && matched_request)) {
        // The first determination seems to be wrong. That is, we are expecting
        // a request but only a response has been successfully decoded or we are
        // expecting a response but only a request has been successfully
        // decoded.
        // Invert the deduction which should now be the right one.
        (*handle_directions)[std::make_tuple(handle, process_koid)] =
            (direction == Direction::kClient) ? Direction::kServer
                                              : Direction::kClient;
        is_request = !is_request;
      }
    }
  }

  rapidjson::Document actual_request;
  rapidjson::Document actual_response;
  if (!options.pretty_print) {
    if (decoded_request != nullptr) {
      decoded_request->ExtractJson(actual_request.GetAllocator(),
                                   actual_request);
    }

    if (decoded_response != nullptr) {
      decoded_response->ExtractJson(actual_response.GetAllocator(),
                                    actual_response);
    }
  }

  const Colors& colors = options.needs_colors ? WithColors : WithoutColors;

  int tabs = 0;
  if (direction == Direction::kUnknown) {
    os << colors.red << "Can't determine request/response." << colors.reset
       << " it can be:\n";
    ++tabs;
  }

  if (matched_request && (is_request || (direction == Direction::kUnknown))) {
    os << std::string(tabs * kTabSize, ' ') << colors.white_on_magenta
       << "request" << colors.reset << ' ' << colors.green
       << method->enclosing_interface().name() << '.' << method->name()
       << colors.reset << " = ";
    if (options.pretty_print) {
      decoded_request->PrettyPrint(os, colors, tabs, tabs * kTabSize,
                                   options.columns);
    } else {
      os << DocumentToString(actual_request);
    }
    os << '\n';
  }
  if (matched_response && (!is_request || (direction == Direction::kUnknown))) {
    os << std::string(tabs * kTabSize, ' ') << colors.white_on_magenta
       << "response" << colors.reset << ' ' << colors.green
       << method->enclosing_interface().name() << '.' << method->name()
       << colors.reset << " = ";
    if (options.pretty_print) {
      decoded_response->PrettyPrint(os, colors, tabs, tabs * kTabSize,
                                    options.columns);
    } else {
      os << DocumentToString(actual_response);
    }
    os << '\n';
  }
}

}  // namespace fidlcat
