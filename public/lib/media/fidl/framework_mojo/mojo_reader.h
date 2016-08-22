// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_READER_H_
#define SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_READER_H_

#include <atomic>

#include "base/single_thread_task_runner.h"
#include "mojo/services/media/core/interfaces/seeking_reader.mojom.h"
#include "services/media/framework/parts/reader.h"
#include "services/util/cpp/incident.h"

namespace mojo {
namespace media {

// Reads raw data from a SeekingReader service.
class MojoReader : public Reader {
 public:
  // Creates an MojoReader. Must be called on a mojo thread.
  static std::shared_ptr<Reader> Create(
      InterfaceHandle<SeekingReader> seeking_reader) {
    return std::shared_ptr<Reader>(new MojoReader(seeking_reader.Pass()));
  }

  ~MojoReader() override;

  // Reader implementation.
  void Describe(const DescribeCallback& callback) override;

  void ReadAt(size_t position,
              uint8_t* buffer,
              size_t bytes_to_read,
              const ReadAtCallback& callback) override;

 private:
  static constexpr size_t kDataPipeCapacity = 32u * 1024u;

  // Calls ReadResponseBody.
  static void ReadResponseBodyStatic(void* self, MojoResult result);

  MojoReader(InterfaceHandle<SeekingReader> seeking_reader);

  // Continues a ReadAt operation on the thread on which this reader was
  // constructed (a mojo thread).
  void ContinueReadAt();

  // Reads from response_body_ into response_body_buffer_.
  void ReadResponseBody();

  // Completes a ReadAt operation by calling the read_at_callback_.
  void CompleteReadAt(Result result, size_t bytes_read = 0);

  // Shuts down the consumer handle and calls CompleteReadAt.
  void FailReadAt(MojoResult result);

  SeekingReaderPtr seeking_reader_;
  Result result_ = Result::kOk;
  size_t size_ = kUnknownSize;
  bool can_seek_ = false;
  Incident ready_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  std::atomic_bool read_in_progress_;
  size_t read_at_position_;
  uint8_t* read_at_buffer_;
  size_t read_at_bytes_to_read_;
  size_t read_at_bytes_remaining_;
  ReadAtCallback read_at_callback_;
  ScopedDataPipeConsumerHandle consumer_handle_;
  size_t consumer_handle_position_ = kUnknownSize;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_MOJO_PARTS_MOJO_READER_H_
