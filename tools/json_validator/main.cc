// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <string>

#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;

// Reads the content of a file into a JSON document.
int ReadDocument(std::string file, Document* document) {
  std::ifstream file_stream(file);
  if (!file_stream.is_open()) {
    fprintf(stderr, "Error: unable to open file %s.\n", file.c_str());
    return 1;
  }
  std::string content((std::istreambuf_iterator<char>(file_stream)),
                       std::istreambuf_iterator<char>());
  file_stream.close();
  if (document->Parse(content).HasParseError()) {
    fprintf(stderr, "Error: unable to parse JSON in file %s.\n", file.c_str());
    return 1;
  }
  return 0;
}

int main(int argc, const char** argv) {
  if (argc < 3 || argc > 4) {
    printf("Usage: %s <schema> <file> [stamp]\n", argv[0]);
    return 1;
  }
  std::string schema_path = argv[1];
  std::string file_path = argv[2];

  Document schema_document;
  if (ReadDocument(schema_path, &schema_document) != 0) {
    return 1;
  }
  Document file_document;
  if (ReadDocument(file_path, &file_document) != 0) {
    return 1;
  }

  SchemaDocument schema(schema_document);
  SchemaValidator validator(schema);
  if (!file_document.Accept(validator)) {
    fprintf(stderr, "Error: the file %s is invalid according to schema %s.\n",
            file_path.c_str(), schema_path.c_str());
    StringBuffer buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(buffer);
    fprintf(stderr, " - location in schema     %s\n", buffer.GetString());
    fprintf(stderr, " - affected keyword       %s\n",
            validator.GetInvalidSchemaKeyword());
    buffer.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(buffer);
    fprintf(stderr, " - document reference     %s\n", buffer.GetString());
    return 1;
  }

  if (argc == 4) {
    // Write the stamp file if one was given.
    std::string stamp_path = argv[3];
    std::ofstream stamp(stamp_path);
    stamp.close();
  }

  return 0;
}
