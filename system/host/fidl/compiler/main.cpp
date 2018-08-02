// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fidl/c_generator.h>
#include <fidl/flat_ast.h>
#include <fidl/identifier_table.h>
#include <fidl/json_generator.h>
#include <fidl/lexer.h>
#include <fidl/library_zx.h>
#include <fidl/names.h>
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
           "   representation is JSON that conforms to a particular schema (located at\n"
           "   https://fuchsia.googlesource.com/zircon/+/master/system/host/fidl/schema.json).\n"
           "   The intermediate representation is used as input to the various backends.\n"
           "\n"
           " * `--name LIBRARY_NAME`. If present, this flag instructs `fidlc` to validate\n"
           "   that the library being compiled has the given name. This flag is useful to\n"
           "   cross-check between the library's declaration in a build system and the\n"
           "   actual contents of the library.\n"
           "\n"
           " * `--files [FIDL_FILE...]...`. Each `--file [FIDL_FILE...]` chunk of arguments\n"
           "   describes a library, all of which must share the same top-level library name\n"
           "   declaration. Libraries must be presented in dependency order, with later\n"
           "   libraries able to use declarations from preceding libraries but not vice versa.\n"
           "   Output is only generated for the final library, not for each of its dependencies.\n"
           "\n"
           " * `--help`. Prints this help, and exit immediately.\n"
           "\n"
           "All of the arguments can also be provided via a response file, denoted as\n"
           "`@responsefile`. The contents of the file at `responsefile` will be interpreted\n"
           "as a whitespace-delimited list of arguments. Response files cannot be nested,\n"
           "and must be the only argument.\n"
           "\n"
           "See <https://fuchsia.googlesource.com/zircon/+/master/docs/fidl/compiler.md>\n"
           "for more information.\n";
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
            Fail("Could not create directory %s for output file %s: error %s\n",
                 path.data(), filename.data(), strerror(errno));
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

class ArgvArguments : public Arguments {
public:
    ArgvArguments(int count, char** arguments)
        : count_(count), arguments_(const_cast<const char**>(arguments)) {}

    std::string Claim() override {
        if (count_ < 1) {
            FailWithUsage("Missing part of an argument\n");
        }
        std::string argument = arguments_[0];
        --count_;
        ++arguments_;
        return argument;
    }

    bool Remaining() const override { return count_ > 0; }

    bool HeadIsResponseFile() {
        if (count_ == 0) {
            return false;
        }
        return arguments_[0][0] == '@';
    }

private:
    int count_;
    const char** arguments_;
};

class ResponseFileArguments : public Arguments {
public:
    ResponseFileArguments(fidl::StringView filename)
        : file_(Open(filename, std::ios::in)) {
        ConsumeWhitespace();
    }

    std::string Claim() override {
        std::string argument;
        while (Remaining() && !IsWhitespace()) {
            argument.push_back(file_.get());
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

enum struct Behavior {
    kCHeader,
    kCClient,
    kCServer,
    kTables,
    kJSON,
};

bool Parse(const fidl::SourceFile& source_file, fidl::IdentifierTable* identifier_table,
           fidl::ErrorReporter* error_reporter, fidl::flat::Library* library) {
    fidl::Lexer lexer(source_file, identifier_table);
    fidl::Parser parser(&lexer, error_reporter);
    auto ast = parser.Parse();
    if (!parser.Ok()) {
        return false;
    }
    if (!library->ConsumeFile(std::move(ast))) {
        return false;
    }
    return true;
}

void Write(std::ostringstream output, std::fstream file) {
    file << output.str();
    file.flush();
}

} // namespace

int main(int argc, char* argv[]) {
    auto argv_args = std::make_unique<ArgvArguments>(argc, argv);

    // Parse the program name.
    argv_args->Claim();

    if (!argv_args->Remaining()) {
        Usage();
        exit(0);
    }

    // Check for a response file. After this, |args| is either argv or
    // the response file contents.
    Arguments* args = argv_args.get();
    std::unique_ptr<ResponseFileArguments> response_file_args;
    if (argv_args->HeadIsResponseFile()) {
        std::string response = args->Claim();
        if (argv_args->Remaining()) {
            // Response file must be the only argument.
            FailWithUsage("Response files must be the only argument to %s.\n", argv[0]);
        }
        // Drop the leading '@'.
        fidl::StringView response_file = response.data() + 1;
        response_file_args = std::make_unique<ResponseFileArguments>(response_file);
        args = response_file_args.get();
    }

    std::string library_name;

    std::map<Behavior, std::fstream> outputs;
    while (args->Remaining()) {
        // Try to parse an output type.
        std::string behavior_argument = args->Claim();
        std::fstream output_file;
        if (behavior_argument == "--help") {
            Usage();
            exit(0);
        } else if (behavior_argument == "--c-header") {
            outputs.emplace(Behavior::kCHeader, Open(args->Claim(), std::ios::out));
        } else if (behavior_argument == "--c-client") {
            outputs.emplace(Behavior::kCClient, Open(args->Claim(), std::ios::out));
        } else if (behavior_argument == "--c-server") {
            outputs.emplace(Behavior::kCServer, Open(args->Claim(), std::ios::out));
        } else if (behavior_argument == "--tables") {
            outputs.emplace(Behavior::kTables, Open(args->Claim(), std::ios::out));
        } else if (behavior_argument == "--json") {
            outputs.emplace(Behavior::kJSON, Open(args->Claim(), std::ios::out));
        } else if (behavior_argument == "--name") {
            library_name = args->Claim();
        } else if (behavior_argument == "--files") {
            // Start parsing filenames.
            break;
        } else {
            FailWithUsage("Unknown argument: %s\n", behavior_argument.data());
        }
    }

    // Parse libraries.
    std::vector<fidl::SourceManager> source_managers;
    source_managers.push_back(fidl::SourceManager());
    std::string library_zx_data(fidl::LibraryZX::kData, strlen(fidl::LibraryZX::kData) + 1);
    source_managers.back().AddSourceFile(
        std::make_unique<fidl::SourceFile>(fidl::LibraryZX::kFilename, std::move(library_zx_data)));
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

    fidl::IdentifierTable identifier_table;
    fidl::ErrorReporter error_reporter;
    std::map<std::vector<fidl::StringView>, std::unique_ptr<fidl::flat::Library>> compiled_libraries;
    const fidl::flat::Library* final_library = nullptr;
    std::vector<fidl::StringView> final_library_name;
    for (const auto& source_manager : source_managers) {
        if (source_manager.sources().empty()) {
            continue;
        }
        auto library = std::make_unique<fidl::flat::Library>(&compiled_libraries, &error_reporter);
        for (const auto& source_file : source_manager.sources()) {
            if (!Parse(*source_file, &identifier_table, &error_reporter, library.get())) {
                error_reporter.PrintReports();
                return 1;
            }
        }
        if (!library->Compile()) {
            error_reporter.PrintReports();
            return 1;
        }
        final_library = library.get();
        std::vector<fidl::StringView> library_name = library->name();
        final_library_name = library_name;
        auto name_and_library = std::make_pair(std::move(library_name), std::move(library));
        auto iter = compiled_libraries.insert(std::move(name_and_library));
        if (!iter.second) {
            const auto& name = iter.first->first;
            Fail("Mulitple libraries with the same name: '%s'\n",
                 NameLibrary(name).data());
        }
    }
    if (final_library == nullptr) {
        Fail("No library was produced.\n");
    }

    // Verify that the produced library's name matches the expected name.
    std::string final_name = NameLibrary(final_library_name);
    if (!library_name.empty() && final_name != library_name) {
        Fail("Generated library '%s' did not match --name argument: %s\n",
             final_name.data(), library_name.data());
    }

    // We recompile dependencies, and only emit output for the final
    // library.
    for (auto& output : outputs) {
        auto& behavior = output.first;
        auto& output_file = output.second;

        switch (behavior) {
        case Behavior::kCHeader: {
            fidl::CGenerator generator(final_library);
            Write(generator.ProduceHeader(), std::move(output_file));
            break;
        }
        case Behavior::kCClient: {
            fidl::CGenerator generator(final_library);
            Write(generator.ProduceClient(), std::move(output_file));
            break;
        }
        case Behavior::kCServer: {
            fidl::CGenerator generator(final_library);
            Write(generator.ProduceServer(), std::move(output_file));
            break;
        }
        case Behavior::kTables: {
            fidl::TablesGenerator generator(final_library);
            Write(generator.Produce(), std::move(output_file));
            break;
        }
        case Behavior::kJSON: {
            fidl::JSONGenerator generator(final_library);
            Write(generator.Produce(), std::move(output_file));
            break;
        }
        }
    }
}
