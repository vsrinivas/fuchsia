// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <algorithm>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

#include "tools/kazoo/alias_workaround.h"
#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"
#include "tools/kazoo/string_util.h"

using namespace std::literals;

namespace {

std::string ToSingular(const std::string& s) {
  if (s.size() > 1 && s[s.size() - 1] == 's') {
    return s.substr(0, s.size() - 1);
  }
  return s;
}

class Formatter {
 public:
  explicit Formatter(const SyscallLibrary* library) : library_(library) {}

  struct Names {
    std::string base_name;  // signals
    std::string type_name;  // zxio_signals_t
  };

  Names Format(const Alias& alias) {
    return Names{
        .base_name = alias.base_name(),
        .type_name = library_->name() + "_" + alias.base_name() + "_t",
    };
  }

  Names Format(const Enum& e) {
    return Names{
        .base_name = e.base_name(),
        .type_name = library_->name() + "_" + e.base_name() + "_t",
    };
  }

  struct StructNames {
    std::string base_name;      // signals
    std::string type_name;      // zxio_dirent_t
    std::string c_struct_name;  // zxio_dirent
  };

  StructNames Format(const Table& table) {
    return StructNames{
        .base_name = table.base_name(),
        .type_name = library_->name() + "_" + table.base_name() + "_t",
        .c_struct_name = library_->name() + "_" + table.base_name(),
    };
  }

  std::string FormatMember(const Enum& e, const std::string& member_name) {
    return ToUpperAscii(library_->name()) + "_" + ToUpperAscii(ToSingular(e.base_name())) + "_" +
           member_name;
  }

  std::string TypeName(const Type& type) {
    return std::visit(
        [this](auto&& type) -> std::string {
          using T = std::decay_t<decltype(type)>;
          if constexpr (std::is_same_v<T, TypeBool>) {
            return "bool";
          }
          if constexpr (std::is_same_v<T, TypeChar>) {
            return "char";
          }
          if constexpr (std::is_same_v<T, TypeInt8>) {
            return "int8_t";
          }
          if constexpr (std::is_same_v<T, TypeInt16>) {
            return "int16_t";
          }
          if constexpr (std::is_same_v<T, TypeInt32>) {
            return "int32_t";
          }
          if constexpr (std::is_same_v<T, TypeInt64>) {
            return "int64_t";
          }
          if constexpr (std::is_same_v<T, TypeSizeT>) {
            return "size_t";
          }
          if constexpr (std::is_same_v<T, TypeUint8>) {
            return "uint8_t";
          }
          if constexpr (std::is_same_v<T, TypeUint16>) {
            return "uint16_t";
          }
          if constexpr (std::is_same_v<T, TypeUint32>) {
            return "uint32_t";
          }
          if constexpr (std::is_same_v<T, TypeUint64>) {
            return "uint64_t";
          }
          if constexpr (std::is_same_v<T, TypeUintptrT>) {
            return "uintptr_t";
          }
          if constexpr (std::is_same_v<T, TypeVoid>) {
            return "void";
          }
          if constexpr (std::is_same_v<T, TypeZxBasicAlias>) {
            return type.name();
          }
          if constexpr (std::is_same_v<T, TypeAlias>) {
            return Format(type.alias_data()).type_name;
          }
          if constexpr (std::is_same_v<T, TypeEnum>) {
            return Format(type.enum_data()).type_name;
          }
          if constexpr (std::is_same_v<T, TypeHandle>) {
            return "zx_handle_t";
          }
          if constexpr (std::is_same_v<T, TypePointer>) {
            return TypeName(type.pointed_to_type()) + "*";
          }
          ZX_ASSERT(false && "Unhandled type in TypeName");
        },
        type.type_data());
  }

  std::string FormatConstant(const Enum& e, uint64_t raw) {
    return std::visit(
        [raw](auto&& type) -> std::string {
          std::stringstream stream;
          stream << std::hex << raw;
          std::string num_str = "0x" + stream.str();
          using T = std::decay_t<decltype(type)>;
          if constexpr (std::is_same_v<T, TypeUint8>) {
            return num_str;
          }
          if constexpr (std::is_same_v<T, TypeUint16>) {
            return num_str;
          }
          if constexpr (std::is_same_v<T, TypeUint32>) {
            return num_str + "u";
          }
          if constexpr (std::is_same_v<T, TypeUint64>) {
            return num_str + "ul";
          }
          if constexpr (std::is_same_v<T, TypeChar>) {
            return num_str;
          }
          if constexpr (std::is_same_v<T, TypeInt8>) {
            return num_str;
          }
          if constexpr (std::is_same_v<T, TypeInt16>) {
            return num_str;
          }
          if constexpr (std::is_same_v<T, TypeInt32>) {
            return num_str;
          }
          if constexpr (std::is_same_v<T, TypeInt64>) {
            return num_str + "l";
          }
          ZX_PANIC("Unhandled primitive type");
        },
        e.underlying_type().type_data());
  }

 private:
  const SyscallLibrary* library_;
};

std::string MakeTitleLine(const std::string& base_name) {
  std::vector<std::string> words = SplitString(base_name, '_', kTrimWhitespace);
  std::for_each(words.begin(), words.end(), [](std::string& s) { s[0] = ToUpperASCII(s[0]); });
  std::string title = JoinStrings(words, " ");
  // Pad up to 80 columns. 4 is to account for space characters around.
  int trailing_length = 80 - static_cast<int>(title.size()) - 4;
  if (trailing_length > 0) {
    return "// " + title + " " + std::string(trailing_length, '-');
  }
  return "// " + title;
}

void PrintDocComments(const std::vector<std::string>& lines, Writer* writer,
                      uint32_t indent_level = 0) {
  if (!lines.empty()) {
    writer->PrintSpacerLine();
  }
  std::string indent(2 * indent_level, ' ');
  for (const auto& line : lines) {
    if (!line.empty()) {
      writer->Printf("%s// %s\n", indent.c_str(), line.c_str());
    } else {
      writer->Printf("%s//\n", indent.c_str());
    }
  }
}

}  // namespace

bool CUlibHeaderOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  std::string prelude = R"(
#ifndef LIB_ZXIO_TYPES_H_
#define LIB_ZXIO_TYPES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>

// This header defines the public types used in the zxio and zxio_ops interface.

__BEGIN_CDECLS
)";
  writer->Printf("%s\n", TrimString(prelude, "\n").c_str());
  writer->Puts("\n");

  Formatter formatter(&library);
  for (const auto& bits : library.bits()) {
    auto names = formatter.Format(*bits);
    std::string title_line = MakeTitleLine(names.base_name);
    writer->Printf("%s\n", title_line.c_str());
    writer->Puts("\n");
    PrintDocComments(bits->description(), writer);
    writer->Printf("typedef %s %s;\n", formatter.TypeName(bits->underlying_type()).c_str(),
                   names.type_name.c_str());
    writer->Puts("\n");
    writer->Printf("#define %s ((%s)%s)\n", formatter.FormatMember(*bits, "NONE").c_str(),
                   names.type_name.c_str(), formatter.FormatConstant(*bits, 0).c_str());
    writer->Puts("\n");
    uint64_t all = 0;
    for (const auto& k : bits->members()) {
      auto v = bits->ValueForMember(k);
      PrintDocComments(v.description, writer);
      writer->Printf("#define %s ((%s)%s)\n", formatter.FormatMember(*bits, k).c_str(),
                     names.type_name.c_str(), formatter.FormatConstant(*bits, v.value).c_str());
      all |= v.value;
    }
    writer->Puts("\n");
    writer->Printf("#define %s ((%s)%s)\n", formatter.FormatMember(*bits, "ALL").c_str(),
                   names.type_name.c_str(), formatter.FormatConstant(*bits, all).c_str());
    writer->Puts("\n");
  }

  for (const auto& e : library.enums()) {
    if (e->id() == "zx/obj_type") {
      // TODO(fxbug.dev/51001): This will emit a correct, but not yet
      // wanted duplicate definition of ZX_OBJ_TYPE_xyz.
      continue;
    }
    auto names = formatter.Format(*e);
    std::string title_line = MakeTitleLine(names.base_name);
    writer->Printf("%s\n", title_line.c_str());
    writer->Puts("\n");
    PrintDocComments(e->description(), writer);
    writer->Printf("typedef %s %s;\n", formatter.TypeName(e->underlying_type()).c_str(),
                   names.type_name.c_str());
    writer->Printf("\n");
    for (const auto& k : e->members()) {
      auto v = e->ValueForMember(k);
      PrintDocComments(v.description, writer);
      writer->Printf("#define %s ((%s)%s)\n", formatter.FormatMember(*e, k).c_str(),
                     names.type_name.c_str(), formatter.FormatConstant(*e, v.value).c_str());
    }
    writer->Puts("\n");
  }

  for (const auto& alias : library.aliases()) {
    Type workaround_type;
    if (AliasWorkaround(alias->original_name(), library, &workaround_type)) {
      // Hide workaround types
      continue;
    }

    auto names = formatter.Format(*alias);
    PrintDocComments(alias->description(), writer);
    writer->Printf("typedef %s %s;\n",
                   formatter.TypeName(library.TypeFromName(alias->partial_type_ctor())).c_str(),
                   names.type_name.c_str());
    writer->Puts("\n");
  }

  for (const auto& table : library.tables()) {
    bool all_required = true;
    for (const auto& member : table->members()) {
      if (member.required() == Required::kNo) {
        all_required = false;
        break;
      }
    }

    auto names = formatter.Format(*table);
    std::string setter_macro_name = ToUpperAscii(names.c_struct_name) + "_SET";
    PrintDocComments(table->description(), writer);
    if (!all_required) {
      writer->Printf(R"(//
// Optional fields have corresponding presence indicators. When creating
// a new object, it is desirable to use the %s helper macro
// to set the fields, to avoid forgetting to change the presence indicator.
)",
                     setter_macro_name.c_str());
    }
    writer->Printf("typedef struct %s {", names.c_struct_name.c_str());
    writer->Puts("\n");
    // Pack optional fields together
    for (const auto& member : table->members()) {
      if (member.required() == Required::kYes) {
        continue;
      }
      PrintDocComments(member.description(), writer, 1);
      writer->Printf("  %s %s;\n", formatter.TypeName(member.type()).c_str(),
                     member.name().c_str());
    }

    if (!all_required) {
      std::string presence_bits_name = names.c_struct_name + "_has_t";
      writer->Printf(R"(
  // Presence indicator for these fields.
  //
  // If a particular field is absent, it should be set to zero/none,
  // and the corresponding presence indicator will be false.
  // Therefore, a completely empty |%s| may be conveniently
  // obtained via value-initialization e.g. `%s a = {};`.
)",
                     names.type_name.c_str(), names.type_name.c_str());
      writer->Printf("  struct %s {\n", presence_bits_name.c_str());
      for (const auto& member : table->members()) {
        if (member.required() == Required::kYes) {
          continue;
        }
        writer->Printf("    bool %s;\n", member.name().c_str());
      }
      writer->Printf("  } has;\n");
    }

    // Followed by required fields
    for (const auto& member : table->members()) {
      if (member.required() == Required::kNo) {
        continue;
      }
      PrintDocComments(member.description(), writer, 1);
      writer->Printf("  %s %s;\n", formatter.TypeName(member.type()).c_str(),
                     member.name().c_str());
    }

    writer->Printf("} %s;\n", names.type_name.c_str());
    writer->Printf(R"(
#define %s(%s, field_name, value) \
  do { \
    %s* _tmp_%s= &(%s); \
    _tmp_%s->field_name = value; \
    _tmp_%s->has.field_name = true; \
  } while (0)
)",
                   setter_macro_name.c_str(), names.base_name.c_str(), names.type_name.c_str(),
                   names.base_name.c_str(), names.base_name.c_str(), names.base_name.c_str(),
                   names.base_name.c_str());
    writer->Puts("\n");
  }

  std::string epilogue = R"(
__END_CDECLS

#endif  // LIB_ZXIO_TYPES_H_
)";
  writer->Printf("%s\n", TrimString(epilogue, "\n").c_str());

  return true;
}
