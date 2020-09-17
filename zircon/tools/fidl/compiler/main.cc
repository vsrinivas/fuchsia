// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fidl/c_generator.h>
#include <fidl/experimental_flags.h>
#include <fidl/flat_ast.h>
#include <fidl/json_generator.h>
#include <fidl/json_schema.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/ordinals.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>
#include <fidl/tables_generator.h>

namespace {

void Usage() {
  std::cout
      << "usage: fidlc [--c-header HEADER_PATH]\n"
         "             [--c-client CLIENT_PATH]\n"
         "             [--c-server SERVER_PATH]\n"
         "             [--tables TABLES_PATH]\n"
         "             [--json JSON_PATH]\n"
         "             [--name LIBRARY_NAME]\n"
         "             [--experimental FLAG_NAME]\n"
         "             [--werror]\n"
         "             [--format=[text|json]]\n"
         "             [--json-schema]\n"
         "             [--files [FIDL_FILE...]...]\n"
         "             [--help]\n"
         "\n"
         " * `--c-header HEADER_PATH`. If present, this flag instructs `fidlc` to output\n"
         "   a C header at the given path.\n"
         "\n"
         " * `--c-client CLIENT_PATH`. If present, this flag instructs `fidlc` to output\n"
         "   the simple C client implementation at the given path.\n"
         "\n"
         " * `--c-server SERVER_PATH`. If present, this flag instructs `fidlc` to output\n"
         "   the simple C server implementation at the given path.\n"
         "\n"
         " * `--tables TABLES_PATH`. If present, this flag instructs `fidlc` to output\n"
         "   coding tables at the given path. The coding tables are required to encode and\n"
         "   decode messages from the C and C++ bindings.\n"
         "\n"
         " * `--json JSON_PATH`. If present, this flag instructs `fidlc` to output the\n"
         "   library's intermediate representation at the given path. The intermediate\n"
         "   representation is JSON that conforms to the schema available via --json-schema.\n"
         "   The intermediate representation is used as input to the various backends.\n"
         "\n"
         " * `--name LIBRARY_NAME`. If present, this flag instructs `fidlc` to validate\n"
         "   that the library being compiled has the given name. This flag is useful to\n"
         "   cross-check between the library's declaration in a build system and the\n"
         "   actual contents of the library.\n"
         "\n"
         " * `--experimental FLAG_NAME`. If present, this flag enables an experimental\n"
         "    feature of fidlc.\n"
         "\n"
         " * `--files [FIDL_FILE...]...`. Each `--file [FIDL_FILE...]` chunk of arguments\n"
         "   describes a library, all of which must share the same top-level library name\n"
         "   declaration. Libraries must be presented in dependency order, with later\n"
         "   libraries able to use declarations from preceding libraries but not vice versa.\n"
         "   Output is only generated for the final library, not for each of its dependencies.\n"
         "\n"
         " * `--json-schema`. If present, this flag instructs `fidlc` to output the\n"
         "   JSON schema of the intermediate representation.\n"
         "\n"
         " * `--format=[text|json]`. If present, this flag sets the output mode of `fidlc`.\n"
         "    This specifies whether to output errors and warnings, if compilation fails, in\n"
         "    plain text (the default), or as JSON.\n"
         "\n"
         " * `--werror`. Treats warnings as errors.\n"
         "\n"
         " * `--help`. Prints this help, and exit immediately.\n"
         "\n"
         "All of the arguments can also be provided via a response file, denoted as\n"
         "`@responsefile`. The contents of the file at `responsefile` will be interpreted\n"
         "as a whitespace-delimited list of arguments. Response files cannot be nested.\n"
         "\n"
         "See <https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler>\n"
         "for more information.\n";
  std::cout.flush();
}

void PrintJsonSchema() {
  std::cout << JsonSchema::schema() << "\n";
  std::cout.flush();
}

[[noreturn]] void FailWithUsage(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  Usage();
  exit(1);
}

[[noreturn]] void Fail(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(1);
}

void MakeParentDirectory(const std::string& filename) {
  std::string::size_type slash = 0;

  for (;;) {
    slash = filename.find('/', slash);
    if (slash == filename.npos) {
      return;
    }

    std::string path = filename.substr(0, slash);
    ++slash;
    if (path.size() == 0u) {
      // Skip creating "/".
      continue;
    }

    if (mkdir(path.data(), 0755) != 0 && errno != EEXIST) {
      Fail("Could not create directory %s for output file %s: error %s\n", path.data(),
           filename.data(), strerror(errno));
    }
  }
}

std::fstream Open(std::string filename, std::ios::openmode mode) {
  if ((mode & std::ios::out) != 0) {
    MakeParentDirectory(filename);
  }

  std::fstream stream;
  stream.open(filename, mode);
  if (!stream.is_open()) {
    Fail("Could not open file: %s\n", filename.data());
  }
  return stream;
}

class Arguments {
 public:
  virtual ~Arguments() {}

  virtual std::string Claim() = 0;
  virtual bool Remaining() const = 0;
};

class ResponseFileArguments : public Arguments {
 public:
  ResponseFileArguments(std::string_view filename)
      : file_(Open(std::string(filename), std::ios::in)) {
    ConsumeWhitespace();
  }

  std::string Claim() override {
    std::string argument;
    while (Remaining() && !IsWhitespace()) {
      argument.push_back(static_cast<char>(file_.get()));
    }
    ConsumeWhitespace();
    return argument;
  }

  bool Remaining() const override { return !file_.eof(); }

 private:
  bool IsWhitespace() {
    switch (file_.peek()) {
      case ' ':
      case '\n':
      case '\r':
      case '\t':
        return true;
      default:
        return false;
    }
  }

  void ConsumeWhitespace() {
    while (Remaining() && IsWhitespace()) {
      file_.get();
    }
  }

  std::fstream file_;
};

class ArgvArguments : public Arguments {
 public:
  ArgvArguments(int count, char** arguments)
      : count_(count), arguments_(const_cast<const char**>(arguments)) {}

  std::string Claim() override {
    if (response_file_.get()) {
      if (response_file_->Remaining()) {
        return response_file_->Claim();
      }
      response_file_.reset();
    }
    if (count_ < 1) {
      FailWithUsage("Missing part of an argument\n");
    }
    std::string argument = arguments_[0];
    --count_;
    ++arguments_;
    if (argument.size() == 0 || argument[0] != '@') {
      return argument;
    }

    std::string_view rsp_file = argument.data() + 1;
    response_file_ = std::make_unique<ResponseFileArguments>(rsp_file);
    return Claim();
  }

  bool Remaining() const override {
    if (response_file_.get() && response_file_->Remaining())
      return true;

    return count_ > 0;
  }

 private:
  int count_;
  const char** arguments_;
  std::unique_ptr<ResponseFileArguments> response_file_;
};

enum struct Behavior {
  kCHeader,
  kCClient,
  kCServer,
  kTables,
  kJSON,
};

bool Parse(const fidl::SourceFile& source_file, fidl::Reporter* reporter,
           fidl::flat::Library* library, const fidl::ExperimentalFlags& experimental_flags) {
  fidl::Lexer lexer(source_file, reporter);
  fidl::Parser parser(&lexer, reporter, experimental_flags);
  auto ast = parser.Parse();
  if (!parser.Success()) {
    return false;
  }
  if (!library->ConsumeFile(std::move(ast))) {
    return false;
  }
  return true;
}

void Write(std::ostringstream output_stream, const std::string file_path) {
  std::fstream file = Open(file_path, std::ios::out);
  file << output_stream.str();
  file.flush();
  if (file.fail()) {
    Fail("Failed to flush output to file: %s\n", file_path.c_str());
  }
}

}  // namespace

// TODO(pascallouis): remove forward declaration, this was only introduced to
// reduce diff size while breaking things up.
int compile(fidl::Reporter* reporter, fidl::flat::Typespace* typespace, std::string library_name,
            std::vector<std::pair<Behavior, std::string>> outputs,
            const std::vector<fidl::SourceManager>& source_managers,
            fidl::ExperimentalFlags experimental_flags);

int main(int argc, char* argv[]) {
  auto args = std::make_unique<ArgvArguments>(argc, argv);

  // Parse the program name.
  args->Claim();
  if (!args->Remaining()) {
    Usage();
    exit(0);
  }

  std::string library_name;

  bool warnings_as_errors = false;
  std::string format = "text";
  std::vector<std::pair<Behavior, std::string>> outputs;
  fidl::ExperimentalFlags experimental_flags;
  while (args->Remaining()) {
    // Try to parse an output type.
    std::string behavior_argument = args->Claim();
    if (behavior_argument == "--help") {
      Usage();
      exit(0);
    } else if (behavior_argument == "--json-schema") {
      PrintJsonSchema();
      exit(0);
    } else if (behavior_argument == "--werror") {
      warnings_as_errors = true;
    } else if (behavior_argument.rfind("--format") == 0) {
      const auto equals = behavior_argument.rfind("=");
      if (equals == std::string::npos) {
        FailWithUsage("Unknown value for flag `format`\n");
      }
      const auto format_value = behavior_argument.substr(equals + 1, behavior_argument.length());
      if (format_value != "text" && format_value != "json") {
        FailWithUsage("Unknown value `%s` for flag `format`\n", format_value.data());
      }
      format = format_value;
    } else if (behavior_argument == "--c-header") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kCHeader, path));
    } else if (behavior_argument == "--c-client") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kCClient, path));
    } else if (behavior_argument == "--c-server") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kCServer, path));
    } else if (behavior_argument == "--tables") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kTables, path));
    } else if (behavior_argument == "--json") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kJSON, path));
    } else if (behavior_argument == "--name") {
      library_name = args->Claim();
    } else if (behavior_argument == "--experimental") {
      std::string flag = args->Claim();
      if (!experimental_flags.SetFlagByName(flag)) {
        FailWithUsage("Unknown experimental flag %s\n", flag.data());
      }
    } else if (behavior_argument == "--files") {
      // Start parsing filenames.
      break;
    } else {
      FailWithUsage("Unknown argument: %s\n", behavior_argument.data());
    }
  }

  // Prepare source files.
  std::vector<fidl::SourceManager> source_managers;
  source_managers.push_back(fidl::SourceManager());
  while (args->Remaining()) {
    std::string arg = args->Claim();
    if (arg == "--files") {
      source_managers.emplace_back();
    } else {
      if (!source_managers.back().CreateSource(arg.data())) {
        Fail("Couldn't read in source data from %s\n", arg.data());
      }
    }
  }

  // Ready. Set. Go.
  bool enable_color = !std::getenv("NO_COLOR") && isatty(fileno(stderr));
  fidl::Reporter reporter(warnings_as_errors, enable_color);
  auto typespace = fidl::flat::Typespace::RootTypes(&reporter);
  auto status = compile(&reporter, &typespace, library_name, std::move(outputs), source_managers,
                        std::move(experimental_flags));
  if (format == "json") {
    reporter.PrintReportsJson();
  } else {
    reporter.PrintReports();
  }
  return status;
}

int compile(fidl::Reporter* reporter, fidl::flat::Typespace* typespace, std::string library_name,
            std::vector<std::pair<Behavior, std::string>> outputs,
            const std::vector<fidl::SourceManager>& source_managers,
            fidl::ExperimentalFlags experimental_flags) {
  fidl::flat::Libraries all_libraries;
  const fidl::flat::Library* final_library = nullptr;
  for (const auto& source_manager : source_managers) {
    if (source_manager.sources().empty()) {
      continue;
    }
    auto library = std::make_unique<fidl::flat::Library>(&all_libraries, reporter, typespace,
                                                         fidl::ordinals::GetGeneratedOrdinal64,
                                                         experimental_flags);
    for (const auto& source_file : source_manager.sources()) {
      if (!Parse(*source_file, reporter, library.get(), experimental_flags)) {
        return 1;
      }
    }
    if (!library->Compile()) {
      return 1;
    }
    final_library = library.get();
    auto library_name = fidl::NameLibrary(library->name());
    if (!all_libraries.Insert(std::move(library))) {
      Fail("Multiple libraries with the same name: '%s'\n", library_name.c_str());
    }
  }
  if (!final_library) {
    Fail("No library was produced.\n");
  }
  auto unused_libraries_names = all_libraries.Unused(final_library);
  // Because the sources of library zx are unconditionally included, we filter
  // out this library here. We can remove this logic when zx is used in source
  // like other libraries.
  unused_libraries_names.erase(std::vector<std::string_view>{"zx"});
  if (unused_libraries_names.size() != 0) {
    std::string message = "Unused libraries provided via --files: ";
    bool first = true;
    for (const auto& name : unused_libraries_names) {
      if (first) {
        first = false;
      } else {
        message.append(", ");
      }
      message.append(fidl::NameLibrary(name));
    }
    message.append("\n");
    Fail(message.data());
  }

  // Verify that the produced library's name matches the expected name.
  std::string final_name = fidl::NameLibrary(final_library->name());
  if (!library_name.empty() && final_name != library_name) {
    Fail("Generated library '%s' did not match --name argument: %s\n", final_name.data(),
         library_name.data());
  }

  // We recompile dependencies, and only emit output for the final
  // library.
  for (auto& output : outputs) {
    auto& behavior = output.first;
    auto& file_path = output.second;

    switch (behavior) {
      case Behavior::kCHeader: {
        fidl::CGenerator generator(final_library);
        Write(generator.ProduceHeader(), file_path);
        break;
      }
      case Behavior::kCClient: {
        fidl::CGenerator generator(final_library);
        Write(generator.ProduceClient(), file_path);
        break;
      }
      case Behavior::kCServer: {
        fidl::CGenerator generator(final_library);
        Write(generator.ProduceServer(), file_path);
        break;
      }
      case Behavior::kTables: {
        fidl::TablesGenerator generator(final_library);
        Write(generator.Produce(), file_path);
        break;
      }
      case Behavior::kJSON: {
        fidl::JSONGenerator generator(final_library);
        Write(generator.Produce(), file_path);
        break;
      }
    }
  }
  return 0;
}
