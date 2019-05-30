// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/bugreport/bug_report_client.h"

#include <fstream>

#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/schema.h>

#include "src/developer/bugreport/bug_report_schema.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace bugreport {

namespace {

template <typename JsonNode>
std::optional<std::string> PrettyPrintJson(const JsonNode& json_node) {
  if (json_node.IsString())
    return json_node.GetString();

  if (json_node.IsObject() || json_node.IsArray()) {
    rapidjson::StringBuffer buf;
    rapidjson::PrettyWriter json_writer(buf);
    json_node.Accept(json_writer);

    return buf.GetString();
  }

  FXL_LOG(ERROR) << "Json node is not a printable type.";
  return std::nullopt;
}

// Annotations are meant to be joined into a single target.
template <typename JsonNode>
std::optional<Target> ParseAnnotations(const JsonNode& annotations) {
  if (!annotations.IsObject()) {
    FXL_LOG(ERROR) << "Annotations are not an object.";
    return std::nullopt;
  }

  auto contents = PrettyPrintJson(annotations);
  if (!contents)
    return std::nullopt;

  Target target;
  target.name = "annotations.json";
  target.contents = std::move(*contents);

  return target;
}

// Each attachment is big enough to warrant its own target.
template <typename JsonNode>
std::optional<std::vector<Target>> ParseAttachments(
    const JsonNode& attachments) {
  if (!attachments.IsObject()) {
    FXL_LOG(ERROR) << "Attachments are not an object.";
    return std::nullopt;
  }

  std::vector<Target> targets;
  for (const auto& [key_obj, attachment] : attachments.GetObject()) {
    auto key = key_obj.GetString();
    if (!attachment.IsString()) {
      FXL_LOG(ERROR) << "Attachment " << key << " is not a string.";
      continue;
    }

    Target target;

    // If the document conforms to json, we can output the name as such.
    rapidjson::Document target_doc;
    target_doc.Parse(attachment.GetString());

    if (target_doc.HasParseError()) {
      // Simple string.
      target.name = fxl::StringPrintf("%s.txt", key);
      target.contents = attachment.GetString();
    } else {
      // It's a valid json object.
      target.name = fxl::StringPrintf("%s.json", key);
      auto content = PrettyPrintJson(target_doc);
      // If pretty printing failed, we add the incoming string.
      if (!content) {
        target.contents = attachment.GetString();
      } else {
        target.contents = *content;
      }
    }

    targets.push_back(std::move(target));
  }

  return targets;
}

std::optional<rapidjson::Document> ParseDocument(const std::string& input) {
  rapidjson::Document document;
  rapidjson::ParseResult result = document.Parse(input);
  if (!result) {
    FXL_LOG(ERROR) << "Error parsing json: "
                   << rapidjson::GetParseError_En(result.Code()) << "("
                   << result.Offset() << ").";
    return std::nullopt;
  }

  return document;
}

bool Validate(const rapidjson::Document& document,
              const std::string& schema_str) {
  auto input_document = ParseDocument(schema_str);
  if (!input_document)
    return false;

  rapidjson::SchemaDocument schema_document(*input_document);
  rapidjson::SchemaValidator validator(schema_document);
  if (!document.Accept(validator)) {
    rapidjson::StringBuffer buf;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(buf);
    FXL_LOG(ERROR) << "Document does not conform to schema. Rule: "
                   << validator.GetInvalidSchemaKeyword();

    return false;
  }
  return true;
}

std::optional<std::string> ReadStream(std::istream* input) {
  std::string contents;
  while (!input->eof()) {
    char buf[4096];
    input->read(buf, sizeof(buf) - 1);
    // This string constructor permits embedded zeroes.
    std::string read(buf, input->gcount());
    contents.append(std::move(read));
  }

  if (!input->eof() && !input->good()) {
    FXL_LOG(ERROR) << "Error reading the input stream.";
    return std::nullopt;
  }

  return contents;
}

}  // namespace

std::optional<std::vector<Target>> ProcessBugReport(const std::string& input) {
  auto opt_document = ParseDocument(input);
  if (!opt_document)
    return std::nullopt;

  auto& document = *opt_document;
  if (!Validate(document, fuchsia::bugreport::kBugReportJsonSchema))
    return std::nullopt;

  std::vector<Target> targets;

  // Annotations.
  if (document.HasMember("annotations")) {
    auto annotations = ParseAnnotations(document["annotations"]);
    if (annotations)
      targets.push_back(std::move(*annotations));
  }

  // Attachments.
  if (document.HasMember("attachments")) {
    auto attachments = ParseAttachments(document["attachments"]);
    if (attachments)
      targets.insert(targets.end(), attachments->begin(), attachments->end());
  }

  if (targets.empty())
    FXL_LOG(WARNING) << "No annotations or attachments are present.";
  return targets;
}

bool Export(const std::vector<Target>& targets,
            const std::filesystem::path& output_path) {
  std::error_code ec;
  if (!std::filesystem::exists(output_path, ec)) {
    if (!std::filesystem::create_directory(output_path, ec)) {
      FXL_LOG(ERROR) << "Could not create directory: " << ec;
      return false;
    }
  }

  // Create targets.
  bool no_errors = true;
  for (const Target& target : targets) {
    auto target_path = output_path / target.name;

    std::ofstream ofs(target_path, std::ofstream::trunc);
    if (!ofs.good()) {
      FXL_LOG(ERROR) << "Could not open path for " << target_path;
      no_errors = false;
      continue;
    }

    ofs << target.contents;
    ofs.close();
  }

  return no_errors;
}

std::optional<std::vector<Target>> HandleBugReport(
    const std::filesystem::path& output_path, std::istream* input) {
  auto content = ReadStream(input);
  if (!content)
    return std::nullopt;

  auto targets = ProcessBugReport(*content);
  if (!targets)
    return std::nullopt;

  if (!Export(*targets, output_path))
    return std::nullopt;

  return *targets;
}

}  // namespace bugreport
