// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libgen.h>

#include <fstream>
#include <map>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <re2/re2.h>
#include <src/lib/fxl/command_line.h>

using namespace rapidjson;

static constexpr char kArgAllowComments[] = "allow_comments";

static constexpr char kUsage[] = R"(
Usage: %s [--allow_comments] <schema> <file> [stamp]

Arguments:
  --allow_comments: Parses and ignores /* */ and // comments in the input file.
    This does not apply to the schema file.
)";

// Reads the content of a file into a JSON document.
//
// The template parameter |parse_flags| is passed to the RapidJSON Parse method and controls
// the JSON parsing behavior. This can be used to enable parsing non-standard JSON syntax,
// like comments.
template <unsigned parse_flags = kParseDefaultFlags>
bool ReadDocument(std::string file, Document* document) {
  std::ifstream file_stream(file);
  if (!file_stream.is_open()) {
    fprintf(stderr, "Error: unable to open file %s.\n", file.c_str());
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(file_stream)),
                      std::istreambuf_iterator<char>());
  file_stream.close();
  if (document->Parse<parse_flags>(content).HasParseError()) {
    fprintf(stderr, "Error: unable to parse JSON in file %s.\n", file.c_str());
    return false;
  }
  return true;
}

// A schema provider that can find schemas specified as URIs/paths relative to
// the main schema.
class LocalSchemaProvider : public IRemoteSchemaDocumentProvider {
 public:
  explicit LocalSchemaProvider(const std::string directory)
      : directory_(directory), has_errors_(false) {}
  ~LocalSchemaProvider() override {}

  const SchemaDocument* GetRemoteDocument(const char* uri, SizeType length) override {
    std::string input(uri, length);
    std::string file_name;
    re2::RE2 pattern("^(file:)?([^/#:]+)$");
    if (!re2::RE2::PartialMatch(input, pattern, nullptr, &file_name)) {
      fprintf(stderr, "Error: could not find schema %s.\n", input.c_str());
      has_errors_ = true;
      return nullptr;
    }
    if (documents_[file_name]) {
      return documents_[file_name].get();
    }
    Document schema_document;
    std::string file_path = directory_ + "/" + file_name;
    if (!ReadDocument(file_path, &schema_document)) {
      // ReadDocument already prints a useful error message.
      has_errors_ = true;
      return nullptr;
    }
    documents_[file_name] = std::make_unique<SchemaDocument>(schema_document);
    return documents_[file_name].get();
  }

  // Returns true if some schemas could not be resolved.
  // By default, missing schemas are just ignored.
  bool HasErrors() { return has_errors_; }

 private:
  // Map of resolved documents.
  std::map<std::string, std::unique_ptr<SchemaDocument>> documents_;
  // Base directory for schema paths.
  const std::string directory_;
  // Whether some schema references could not be resolved.
  bool has_errors_;
};

// Returns the base directory of a given file.
std::string BaseDir(const std::string file) {
  char* file_copy = strdup(file.c_str());
  char* base = dirname(file_copy);
  std::string result(base);
  free(file_copy);
  return result;
}

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& positional_args = command_line.positional_args();

  if (positional_args.size() < 2 || positional_args.size() > 3) {
    printf(kUsage, argv[0]);
    return 1;
  }

  const std::string schema_path = positional_args[0];
  const std::string file_path = positional_args[1];
  const bool allow_comments = command_line.HasOption(kArgAllowComments);

  Document schema_document;
  if (!ReadDocument(schema_path, &schema_document)) {
    return 1;
  }

  Document file_document;
  bool file_read_ok = allow_comments ? ReadDocument<kParseCommentsFlag>(file_path, &file_document)
                                     : ReadDocument(file_path, &file_document);
  if (!file_read_ok) {
    return 1;
  }

  std::string schema_base = BaseDir(schema_path);
  LocalSchemaProvider provider(schema_base);
  SchemaDocument schema(schema_document, nullptr /* uri */, 0 /* uriLength */, &provider);
  SchemaValidator validator(schema);
  if (!file_document.Accept(validator)) {
    fprintf(stderr, "Error: the file %s is invalid according to schema %s.\n", file_path.c_str(),
            schema_path.c_str());
    StringBuffer buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(buffer);
    fprintf(stderr, " - location in schema     %s\n", buffer.GetString());
    fprintf(stderr, " - affected keyword       %s\n", validator.GetInvalidSchemaKeyword());
    buffer.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(buffer);
    fprintf(stderr, " - document reference     %s\n", buffer.GetString());
    buffer.Clear();
    rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
    validator.GetError().Accept(w);
    fprintf(stderr, " - full error %s\n", buffer.GetString());

    return 1;
  }
  if (provider.HasErrors()) {
    return 1;
  }

  if (positional_args.size() == 3) {
    // Write the stamp file if one was given.
    std::string stamp_path = positional_args[2];
    std::ofstream stamp(stamp_path);
    stamp.close();
  }

  return 0;
}
