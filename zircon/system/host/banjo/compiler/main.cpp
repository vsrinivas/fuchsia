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

#include <banjo/ddk_generator.h>
#include <banjo/flat_ast.h>
#include <banjo/identifier_table.h>
#include <banjo/json_generator.h>
#include <banjo/lexer.h>
#include <banjo/library_zx.h>
#include <banjo/names.h>
#include <banjo/parser.h>
#include <banjo/source_manager.h>
#include <banjo/tables_generator.h>

namespace {

void Usage() {
    std::cout
        << "usage: banjoc [--ddk-header HEADER_PATH]\n"
           "              [--ddktl-header HEADER_PATH]\n"
           "              [--json JSON_PATH]\n"
           "              [--name LIBRARY_NAME]\n"
           "              [--files [BANJO_FILE...]...]\n"
           "              [--help]\n"
           "\n"
           " * `--ddk-header HEADER_PATH`. If present, this flag instructs `banjoc` to output\n"
           "   a C ddk header at the given path.\n"
           "\n"
           " * `--ddktl-header HEADER_PATH`. If present, this flag instructs `banjoc` to output\n"
           "   a C++ ddktl header at the given path.\n"
           "\n"
           " * `--json JSON_PATH`. If present, this flag instructs `banjoc` to output the\n"
           "   library's intermediate representation at the given path. The intermediate\n"
           "   representation is JSON that conforms to a particular schema (located at\n"
           "   https://fuchsia.googlesource.com/zircon/+/master/system/host/banjo/schema.json).\n"
           "   The intermediate representation is used as input to the various backends.\n"
           "\n"
           " * `--name LIBRARY_NAME`. If present, this flag instructs `banjoc` to validate\n"
           "   that the library being compiled has the given name. This flag is useful to\n"
           "   cross-check between the library's declaration in a build system and the\n"
           "   actual contents of the library.\n"
           "\n"
           " * `--files [BANJO_FILE...]...`. Each `--file [BANJO_FILE...]` chunk of arguments\n"
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
           "See <https://fuchsia.googlesource.com/zircon/+/master/docs/banjo/compiler.md>\n"
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

// Stream is a wrapper around std::fstream to ensure we delete files that we
// open for output but don't write anything to.
class Stream {
public:
    Stream() {}
    Stream(Stream&& other)
        : stream_(std::move(other.stream_)), filename_(other.filename_),
          written_to_(other.written_to_), out_(other.out_) {
        other.out_ = false;
    }

    auto eof() const { return stream_.eof(); }
    void flush() { stream_.flush(); }
    auto get() { return stream_.get(); }
    auto is_open() { return stream_.is_open(); }
    auto peek() { return stream_.peek(); }

    Stream& operator<<(const std::string& value) {
        written_to_ = true;
        stream_ << value;
        return *this;
    }

    void open(std::string filename,
              std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out) {
        out_ = mode & std::ios_base::out;
        stream_.open(filename, mode);
        filename_ = std::string(filename);
    }

    ~Stream() {
        stream_.close();
        if (out_ && !written_to_) {
            remove(filename_.c_str());
        }
    }

private:
    std::fstream stream_;
    std::string filename_;
    bool written_to_ = false;
    bool out_ = false;
};


Stream Open(std::string filename, std::ios::openmode mode) {
    if ((mode & std::ios::out) != 0) {
        MakeParentDirectory(filename);
    }

    Stream stream;
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
    ResponseFileArguments(banjo::StringView filename)
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

    Stream file_;
};

enum struct Behavior {
    kDdkHeader,
    kDdktlHeader,
    kDdktlInternalHeader,
    kJSON,
};

bool Parse(const banjo::SourceFile& source_file, banjo::IdentifierTable* identifier_table,
           banjo::ErrorReporter* error_reporter, banjo::flat::Library* library) {
    banjo::Lexer lexer(source_file, identifier_table);
    banjo::Parser parser(&lexer, error_reporter);
    auto ast = parser.Parse();
    if (!parser.Ok()) {
        return false;
    }
    if (!library->ConsumeFile(std::move(ast))) {
        return false;
    }
    return true;
}

void Write(std::ostringstream output, Stream file) {
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
        banjo::StringView response_file = response.data() + 1;
        response_file_args = std::make_unique<ResponseFileArguments>(response_file);
        args = response_file_args.get();
    }

    std::string library_name;

    std::map<Behavior, Stream> outputs;
    while (args->Remaining()) {
        // Try to parse an output type.
        std::string behavior_argument = args->Claim();
        Stream output_file;
        if (behavior_argument == "--help") {
            Usage();
            exit(0);
        } else if (behavior_argument == "--ddk-header") {
            outputs.emplace(Behavior::kDdkHeader, Open(args->Claim(), std::ios::out));
        } else if (behavior_argument == "--ddktl-header") {
            const auto path = args->Claim();
            outputs.emplace(Behavior::kDdktlHeader, Open(path, std::ios::out));
            // TODO(surajmalhotra): Create the internal header via a separate
            // build command (or expect it as another argument).
            const size_t dot = path.find_last_of(".");
            const std::string noext = (dot != std::string::npos)
                                          ? path.substr(0, dot)
                                          : path;
            outputs.emplace(Behavior::kDdktlInternalHeader,
                            Open(noext + "-internal.h", std::ios::out));
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
    std::vector<banjo::SourceManager> source_managers;
    source_managers.push_back(banjo::SourceManager());
    std::string library_zx_data(banjo::LibraryZX::kData, strlen(banjo::LibraryZX::kData) + 1);
    source_managers.back().AddSourceFile(
        std::make_unique<banjo::SourceFile>(banjo::LibraryZX::kFilename, std::move(library_zx_data)));
    source_managers.push_back(banjo::SourceManager());
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

    banjo::IdentifierTable identifier_table;
    banjo::ErrorReporter error_reporter;
    banjo::flat::Libraries all_libraries;
    const banjo::flat::Library* final_library = nullptr;
    for (const auto& source_manager : source_managers) {
        if (source_manager.sources().empty()) {
            continue;
        }
        auto library = std::make_unique<banjo::flat::Library>(&all_libraries, &error_reporter);
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
        if (!all_libraries.Insert(std::move(library))) {
            const auto& name = library->name();
            Fail("Mulitple libraries with the same name: '%s'\n",
                 NameLibrary(name).data());
        }
    }
    if (final_library == nullptr) {
        Fail("No library was produced.\n");
    }

    // Verify that the produced library's name matches the expected name.
    std::string final_name = NameLibrary(final_library->name());
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
        case Behavior::kDdkHeader: {
            banjo::DdkGenerator generator(final_library);
            Write(generator.ProduceHeader(), std::move(output_file));
            break;
        }
        case Behavior::kDdktlHeader: {
            banjo::DdktlGenerator generator(final_library);
            Write(generator.ProduceHeader(), std::move(output_file));
            break;
        }
        case Behavior::kDdktlInternalHeader: {
            banjo::DdktlGenerator generator(final_library);
            Write(generator.ProduceInternalHeader(), std::move(output_file));
            break;
        }
        case Behavior::kJSON: {
            banjo::JSONGenerator generator(final_library);
            Write(generator.Produce(), std::move(output_file));
            break;
        }
        }
    }
}
