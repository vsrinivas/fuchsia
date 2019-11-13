// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"
#include "tools/kazoo/string_util.h"

namespace {

// This is almost list SplitString() on spaces, but punctuation needs to be treated differently as
// it's broken on, but not discarded (for example, '-' in the middle of words, or '.' attached the
// last word of a sentence.)
//
// TODO(syscall-fidl-transition): 1) This is probably more particular than consumers of the .json
// file really are; 2) There may only be one consumer of this field anyway (update-docs-from-abigen)
// which is in-tree and can just be rewritten in some other way.
std::vector<std::string> BreakAsAbigenParser(const std::string& docstr) {
  std::vector<std::string> tokens;
  std::string tok;

  for (const auto& c : docstr) {
    if (isalnum(c) || c == '_') {
      tok += c;
    } else {
      if (!tok.empty())
        tokens.push_back(tok);
      tok.clear();
      if (ispunct(c))
        tokens.emplace_back(1, c);
    }
  }
  if (!tok.empty())
    tokens.push_back(tok);

  return tokens;
}

}  // namespace

bool JsonOutput(const SyscallLibrary& library, Writer* writer) {
  // Note, no comments allowed in plain json, so no copyright or "is generated" note.

  if (!writer->Puts("{\n")) {
    return false;
  }

  writer->Puts("  \"syscalls\": [\n");
  for (size_t j = 0; j < library.syscalls().size(); ++j) {
    const auto& syscall = *library.syscalls()[j];
    writer->Puts("    {\n");
    std::string indent("      ");

    auto iprintn = [&indent, &writer](const char* format, ...) {
      writer->Puts(indent.c_str());
      va_list ap;
      va_start(ap, format);
      std::string result = StringVPrintf((format + std::string("\n")).c_str(), ap);
      va_end(ap);
      return writer->Puts(result);
    };

    auto output_list = [&iprintn](const std::vector<std::string>& items) {
      for (size_t i = 0; i < items.size(); ++i) {
        iprintn("\"%s\"%s", items[i].c_str(), i == items.size() - 1 ? "" : ",");
      }
    };
    auto in = [&indent]() { indent += "  "; };
    auto out = [&indent]() { indent = indent.substr(2); };
    auto output_attributes_list = [&output_list](const auto& attrib_map) {
      std::vector<std::string> items;
      items.push_back("*");  // From abigen.
      for (const auto& it : attrib_map) {
        if (it.first == "Doc" || it.first == "ArgReorder") {
          continue;
        }
        items.push_back(CamelToSnake(it.first));
      }
      output_list(items);
    };

    iprintn("\"name\": \"%s\",", syscall.name().c_str());

    iprintn("\"attributes\": [");
    in();
    output_attributes_list(syscall.attributes());
    out();
    iprintn("],");

    iprintn("\"top_description\": [");
    in();
    const auto doc_split = BreakAsAbigenParser(syscall.short_description());
    if (!doc_split.empty()) {
      iprintn(("\"" + JoinStrings(doc_split, "\", \"") + "\"").c_str());
    }
    out();
    iprintn("],");

    iprintn("\"requirements\": [");
    in();
    for (size_t i = 0; i < syscall.rights_specs().size(); ++i) {
      std::string rights = syscall.rights_specs()[i];
      const bool last_right = i == syscall.rights_specs().size() - 1;
      iprintn("%s%s", ("\"" + JoinStrings(BreakAsAbigenParser(rights), "\", \"") + "\"").c_str(),
              last_right ? "" : ",");
    }
    out();
    iprintn("],");

    iprintn("\"arguments\": [");
    in();
    for (size_t i = 0; i < syscall.kernel_arguments().size(); ++i) {
      const auto& arg = syscall.kernel_arguments()[i];
      iprintn("{");
      in();
      iprintn("\"name\": \"%s\",", arg.name().c_str());
      auto type_info = GetJsonName(arg.type());
      iprintn("\"type\": \"%s\",", type_info.name.c_str());
      iprintn("\"is_array\": %s,", type_info.is_pointer ? "true" : "false");
      iprintn("\"attributes\": [");
      if (type_info.attribute == "IN") {
        iprintn("  \"IN\"");
      }
      iprintn("]");
      out();
      const bool last_arg = i == syscall.kernel_arguments().size() - 1;
      iprintn("}%s", last_arg ? "" : ",");
    }
    out();
    iprintn("],");

    iprintn("\"return_type\": \"%s\"", GetCUserModeName(syscall.kernel_return_type()).c_str());

    const bool last_syscall = j == library.syscalls().size() - 1;
    writer->Printf("    }%s\n", last_syscall ? "" : ",");
  }
  writer->Puts("  ]\n");
  writer->Puts("}\n");

  return true;
}
