// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_DATA_SOURCE_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_DATA_SOURCE_H_

#include <stdint.h>

#include <functional>
#include <memory>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "mx/socket.h"
#include "mx/vmo.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace storage {

// Represents a source of Data that can be read asynchronously.
class DataSource {
 public:
  // A chunk of Data returned by the DataSource. Ownership is given to the
  // recipient.
  class DataChunk {
   public:
    DataChunk() {}
    virtual ~DataChunk() {}

    virtual fxl::StringView Get() = 0;

    // Factory methods.
    static std::unique_ptr<DataChunk> Create(std::string value);
    static std::unique_ptr<DataChunk> Create(
        std::unique_ptr<flatbuffers::FlatBufferBuilder> builder);

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(DataChunk);
  };

  enum Status {
    DONE,
    TO_BE_CONTINUED,
    ERROR,
  };

  // Factory methods.
  static std::unique_ptr<DataSource> Create(std::string value);
  static std::unique_ptr<DataSource> Create(fidl::Array<uint8_t> value);
  static std::unique_ptr<DataSource> Create(mx::vmo vmo);
  static std::unique_ptr<DataSource> Create(mx::socket socket, uint64_t size);

  DataSource() {}
  virtual ~DataSource() {}

  // Returns the toal size of the data in the DataSource.
  virtual uint64_t GetSize() = 0;
  // Fetches the data. This must only be called once. |callback| will later be
  // called one or more times with subsequent chunks of data. If |Status| is
  // |TO_BE_CONTINUED|, |callback| will be called again with the next chunk of
  // data. If |Status| is |DONE|, all the data has been received. In case of
  // error, |callback| will be called with an |ERROR| status and a null |Data|.
  virtual void Get(
      std::function<void(std::unique_ptr<DataChunk>, Status)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DataSource);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_DATA_SOURCE_H_
