// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/printer.h"

#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

const Colors WithoutColors("", "", "", "", "", "");
const Colors WithColors(/*new_reset=*/"\u001b[0m", /*new_red=*/"\u001b[31m",
                        /*new_green=*/"\u001b[32m", /*new_blue=*/"\u001b[34m",
                        /*new_white_on_magenta=*/"\u001b[45m\u001b[37m",
                        /*new_yellow_background=*/"\u001b[103m");

void PrettyPrinter::DisplayHandle(const zx_handle_info_t& handle) {
  fidl_codec::DisplayHandle(colors_, handle, os_);
}

void PrettyPrinter::IncrementTabulations() {
  ++tabulations_;
  if (need_to_print_header_) {
    remaining_size_ -= kTabSize;
  }
}

void PrettyPrinter::DecrementTabulations() {
  --tabulations_;
  if (need_to_print_header_) {
    remaining_size_ += kTabSize;
  }
}

void PrettyPrinter::NeedHeader() {
  remaining_size_ = max_line_size_ - line_header_.size() - tabulations_ * kTabSize;
  need_to_print_header_ = true;
}

void PrettyPrinter::PrintHeader(char first_character) {
  FXL_DCHECK(need_to_print_header_);
  need_to_print_header_ = false;
  os_ << line_header_;
  if (first_character != '\n') {
    for (int tab = tabulations_ * kTabSize; tab > 0; --tab) {
      os_ << ' ';
    }
  }
}

PrettyPrinter& PrettyPrinter::operator<<(std::string_view data) {
  if (data.empty()) {
    return *this;
  }
  if (need_to_print_header_) {
    PrintHeader(data[0]);
  }
  size_t end_of_line = data.find('\n', 0);
  if (end_of_line == std::string_view::npos) {
    os_ << data;
    remaining_size_ -= data.size();
    return *this;
  }
  size_t current = 0;
  for (;;) {
    std::string_view tmp = data.substr(current, end_of_line - current + 1);
    os_ << tmp;
    NeedHeader();
    current = end_of_line + 1;
    if (current >= data.size()) {
      return *this;
    }
    end_of_line = data.find('\n', current);
    if (end_of_line == std::string_view::npos) {
      os_ << data;
      remaining_size_ -= data.size();
      return *this;
    }
    PrintHeader(data[current]);
  }
}

}  // namespace fidl_codec
