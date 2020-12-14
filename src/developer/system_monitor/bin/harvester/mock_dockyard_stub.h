// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_MOCK_DOCKYARD_STUB_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_MOCK_DOCKYARD_STUB_H_

#include <lib/zx/clock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/system_monitor/lib/proto/dockyard.grpc.pb.h"

#include <grpc++/test/mock_stream.h>
using namespace dockyard_proto;
using namespace testing;

class MockDockyardStub : public dockyard_proto::Dockyard::StubInterface {
 public:
  MOCK_METHOD(::grpc::Status, Init,
              (::grpc::ClientContext*, const InitRequest&, InitReply*),
              (override));

  MOCK_METHOD(::grpc::Status, GetDockyardIdsForPaths,
              (::grpc::ClientContext*, const DockyardPaths&, DockyardIds*),
              (override));

  MOCK_METHOD(
      (::grpc::ClientReaderWriterInterface<::dockyard_proto::InspectJson,
                                           ::dockyard_proto::EmptyMessage>*),
      SendInspectJsonRaw, (::grpc::ClientContext*), (override));

  MOCK_METHOD(
      (::grpc::ClientAsyncReaderWriterInterface<
          ::dockyard_proto::InspectJson, ::dockyard_proto::EmptyMessage>*),
      AsyncSendInspectJsonRaw,
      (::grpc::ClientContext*, ::grpc::CompletionQueue*, void*), (override));

  MOCK_METHOD(
      (::grpc::ClientAsyncReaderWriterInterface<
          ::dockyard_proto::InspectJson, ::dockyard_proto::EmptyMessage>*),
      PrepareAsyncSendInspectJsonRaw,
      (::grpc::ClientContext*, ::grpc::CompletionQueue*), (override));

  MOCK_METHOD(
      (::grpc::ClientReaderWriterInterface<::dockyard_proto::RawSample,
                                           ::dockyard_proto::EmptyMessage>*),
      SendSampleRaw, (::grpc::ClientContext*), (override));

  MOCK_METHOD(
      (::grpc::ClientAsyncReaderWriterInterface<
          ::dockyard_proto::RawSample, ::dockyard_proto::EmptyMessage>*),
      AsyncSendSampleRaw,
      (::grpc::ClientContext*, ::grpc::CompletionQueue*, void*), (override));

  MOCK_METHOD(
      (::grpc::ClientAsyncReaderWriterInterface<
          ::dockyard_proto::RawSample, ::dockyard_proto::EmptyMessage>*),
      PrepareAsyncSendSampleRaw,
      (::grpc::ClientContext*, ::grpc::CompletionQueue*), (override));

  MOCK_METHOD(
      (::grpc::ClientReaderWriterInterface<::dockyard_proto::RawSamples,
                                           ::dockyard_proto::EmptyMessage>*),
      SendSamplesRaw, (::grpc::ClientContext*), (override));

  MOCK_METHOD(
      (::grpc::ClientAsyncReaderWriterInterface<
          ::dockyard_proto::RawSamples, ::dockyard_proto::EmptyMessage>*),
      AsyncSendSamplesRaw,
      (::grpc::ClientContext*, ::grpc::CompletionQueue*, void*), (override));

  MOCK_METHOD(
      (::grpc::ClientAsyncReaderWriterInterface<
          ::dockyard_proto::RawSamples, ::dockyard_proto::EmptyMessage>*),
      PrepareAsyncSendSamplesRaw,
      (::grpc::ClientContext*, ::grpc::CompletionQueue*), (override));

 private:
  MOCK_METHOD(
      ::grpc::ClientAsyncResponseReaderInterface<::dockyard_proto::InitReply>*,
      AsyncInitRaw,
      (::grpc::ClientContext * context,
       const ::dockyard_proto::InitRequest& request,
       ::grpc::CompletionQueue* cq),
      (override));
  MOCK_METHOD(
      ::grpc::ClientAsyncResponseReaderInterface<::dockyard_proto::InitReply>*,
      PrepareAsyncInitRaw,
      (::grpc::ClientContext*, const ::dockyard_proto::InitRequest&,
       ::grpc::CompletionQueue*),
      (override));

  MOCK_METHOD(::grpc::ClientAsyncResponseReaderInterface<
                  ::dockyard_proto::DockyardIds>*,
              AsyncGetDockyardIdsForPathsRaw,
              (::grpc::ClientContext*, const ::dockyard_proto::DockyardPaths&,
               ::grpc::CompletionQueue*),
              (override));
  MOCK_METHOD(::grpc::ClientAsyncResponseReaderInterface<
                  ::dockyard_proto::DockyardIds>*,
              PrepareAsyncGetDockyardIdsForPathsRaw,
              (::grpc::ClientContext*, const ::dockyard_proto::DockyardPaths&,
               ::grpc::CompletionQueue*),
              (override));
};
#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_MOCK_DOCKYARD_STUB_H_
