// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <lib/boot-options/boot-options.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <variant>

#include <rapidjson/document.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

namespace {

#include "enum.h"

constexpr char kOptString[] = "j:ds:t::";
constexpr option kOptions[] = {
    {"defaults", optional_argument, nullptr, 'd'},
    {"json", required_argument, nullptr, 'j'},
    {"set", required_argument, nullptr, 's'},
    {"show", optional_argument, nullptr, 't'},
    {},
};

void usage(const char* progname) {
  fprintf(stderr, R"""(
Usage: %s OPTIONS...

  --defaults, -d              display all default values
  --json=FILE, -j FILE        write JSON description to FILE
  --set=CMDLINE, -s CMDLINE   set values from CMDLINE
  --show[=KEY], -t[KEY]       display KEY=VALUE (or all keys)

Each option is processed in turn.  Thus earlier --set options affect the output
of later --show or --json options.
)""",
          progname);
  exit(EXIT_FAILURE);
}

template <typename T>
void WriteJsonValue(rapidjson::PrettyWriter<rapidjson::FileWriteStream>& writer, const T& value) {
  if constexpr (std::is_same_v<T, bool>) {
    writer.Bool(value);
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    writer.Uint64(value);
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    writer.Uint(value);
  } else {
    char buffer[128];
    FILE* f = fmemopen(buffer, sizeof(buffer), "w");
    BootOptions::PrintValue(value, f);
    fclose(f);
    writer.String(buffer);
  }
}

struct Equal {
  template <typename EqT>
  constexpr bool operator()(const EqT& value, const EqT& init) const {
    return value == init;
  }

  template <typename... V>
  constexpr bool operator()(const std::variant<V...>& value, const std::variant<V...>& init) const {
    constexpr auto equal = [](const auto& value, const auto& init) {
      if constexpr (std::is_same_v<decltype(value), decltype(init)>) {
        return value == init;
      }
      return false;
    };
    return std::visit(equal, value, init);
  }
};

template <typename T>
void WriteJsonOption(rapidjson::PrettyWriter<rapidjson::FileWriteStream>& writer, const char* name,
                     const char* type, const char* member, std::string_view doc, const T& init,
                     const T& value) {
  writer.StartObject();

  writer.Key("name");
  writer.String(name);

  writer.Key("type");
  if constexpr (std::is_enum_v<T>) {
    writer.StartArray();
    Enum<T>(EnumEnumerator{[&writer](std::string_view name) {
                             writer.String(name.data(),
                                           static_cast<rapidjson::SizeType>(name.size()));
                           },
                           T{}});
    writer.EndArray();
  } else {
    writer.String(type);
  }

  // options.inc uses R"""(...)""" with line breaks at the start and end.
  ZX_ASSERT(doc.substr(0, 1) == "\n");
  doc.remove_prefix(1);
  ZX_ASSERT(doc.substr(doc.size() - 1) == "\n");
  doc.remove_suffix(1);

  writer.Key("documentation");
  writer.String(doc.data(), static_cast<rapidjson::SizeType>(doc.size()));

  writer.Key("default");
  WriteJsonValue<T>(writer, init);

  if (!Equal{}(value, init)) {
    writer.Key("value");
    WriteJsonValue<T>(writer, value);
  }

  writer.EndObject();
}

void WriteJson(const BootOptions& options, const char* json_output) {
  auto f = fopen(json_output, "w");
  if (!f) {
    perror(json_output);
    exit(1);
  }

  char buffer[BUFSIZ];
  rapidjson::FileWriteStream os(f, buffer, sizeof(buffer));
  rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);

  writer.StartObject();
#define DEFINE_OPTION(name, type, member, init, doc) \
  WriteJsonOption<type>(writer, name, #type, #member, doc, type init, options.member);

  writer.Key("common");
  writer.StartArray();
#include <lib/boot-options/options.inc>
  writer.EndArray();

  writer.Key("x86");
  writer.StartArray();
#include <lib/boot-options/x86.inc>
  writer.EndArray();

#undef DEFINE_OPTION
  writer.EndObject();

  fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
  BootOptions options;
  bool nop = true;
  int opt;
  while ((opt = getopt_long(argc, argv, kOptString, kOptions, nullptr)) != -1) {
    switch (opt) {
      case 'd':
        BootOptions{}.Show(false);
        break;

      case 'j':
        WriteJson(options, optarg);
        break;

      case 's':
        options.SetMany(optarg, stderr);
        break;

      case 't':
        if (optarg) {
          options.Show(optarg, false);
        } else {
          options.Show(false);
        }
        break;

      default:
        usage(argv[0]);
    }
    nop = false;
  }
  if (argc != optind || nop) {
    usage(argv[0]);
  }
  return 0;
}
