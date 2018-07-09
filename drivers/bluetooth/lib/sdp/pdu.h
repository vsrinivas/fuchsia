// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_SDP_PDU_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_SDP_PDU_H_

#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"

namespace btlib {
namespace sdp {

constexpr uint64_t kInvalidContState = 0xFFFFFFFF;

// Maximum lenth of continuation information is 16 bytes, and the InfoLength
// is one byte. See v5.0, Vol 3, Part B, Sec 4.3
constexpr size_t kMaxContStateLength = 17;

class Request {
 public:
  Request();
  virtual ~Request() = default;

  // Returns true if the request is valid.
  virtual bool valid() const = 0;

  // Gets a buffer containing the PDU representation of this request.
  // Returns nullptr if the request is not valid.
  virtual common::ByteBufferPtr GetPDU(TransactionId tid) const = 0;

  // Returns a view with the current continuation state.
  // In a response packet with more than one packet, this contains the most
  // recent continuaton state (so it can be read to request a continuation).
  const common::BufferView ContinuationState() const {
    return cont_state_.view(1, cont_info_size());
  }

  // Sets the continuation state for this request.
  void SetContinuationState(const common::ByteBuffer& buf);

 protected:
  // Parses the continuation state portion of a packet, which is in |buf|.
  // Returns true if the parsing succeeded.
  bool ParseContinuationState(const common::ByteBuffer& buf);

  // Writes the continuation state to |buf|, which must have at least
  // cont_info_size() + 1 bytes avaiable.
  size_t WriteContinuationState(common::MutableByteBuffer* buf) const;

  uint8_t cont_info_size() const { return cont_state_.data()[0]; }

 private:
  // Continuation information, including the length.
  common::StaticByteBuffer<kMaxContStateLength> cont_state_;
};

// SDP Response objects are used in two places:
//  - to construct a response for returning from a request on the server
//  - to receive responses from a server as a client, possibly building from
//    multiple response PDUs
class Response {
 public:
  // Returns true if these parameters represent a complete response.
  virtual bool complete() const = 0;

  // Returns the continuation state from a partial response, used to
  // make an additional request.  Returns an empty view if this packet
  // is complete.
  virtual const common::BufferView ContinuationState() const = 0;

  // Parses parameters from a PDU response, storing a partial result if
  // necessary.
  // Returns a success status if the parameters could ba parsed, or a status
  // containing:
  //  - kNotReady if this response is already complete.
  //  - kPacketMalformed: if the parameters couldn't be parsed.
  //  - kOutOfMemory: if memory isn't available to store a partial response.
  virtual Status Parse(const common::ByteBuffer& buf) = 0;

  // Returns a buffer containing the PDU representation of this response,
  // including the header.
  // |max| will control the maximum size of the parameters based on the
  // transaction type:
  //  - for ServiceSearchResponse, this should be the maximum count of records
  //    to be included.
  //  - for ServiceAttributeResponse or ServiceSearchAttributeResponse, this
  //    is the MaximumAttributeByteCount from the request
  // The buffer parameters will contain continuation state if it is not the
  // last valid packet representing a response.
  // If that continuation state is passed to this function with the same
  // |max_size| argument it will produce the next parameters of response.
  virtual common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const = 0;
};

class ErrorResponse : public Response {
 public:
  ErrorResponse(ErrorCode code = ErrorCode::kReserved) : error_code_(code) {}
  // Response overrides.
  bool complete() const override { return error_code_ != ErrorCode::kReserved; }

  const common::BufferView ContinuationState() const override {
    // ErrorResponses never have continuation state.
    return common::BufferView();
  }

  Status Parse(const common::ByteBuffer& buf) override;

  // Note: |max_size| and |cont_state| are ignored.
  // Error Responses do not have a valid continuation.
  common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const override;

  ErrorCode error_code() const { return error_code_; }
  void set_error_code(ErrorCode code) { error_code_ = code; }

 private:
  ErrorCode error_code_;
};

class ServiceSearchRequest : public Request {
 public:
  // Create an empty search request.
  ServiceSearchRequest();
  // Parse the parameters given in |params| to initialize this request.
  explicit ServiceSearchRequest(const common::ByteBuffer& params);

  // Request overrides
  bool valid() const override;
  common::ByteBufferPtr GetPDU(TransactionId tid) const override;

  // A service search pattern matches if every UUID in the pattern is contained
  // within one of the services' attribute values.  They don't need to be in any
  // specific attribute or in any particular order, and extraneous UUIDs are
  // allowed to exist in the attribute value.
  // See v5.0, Volume 3, Part B, Sec 2.5.2.
  void set_search_pattern(std::unordered_set<common::UUID> pattern) {
    service_search_pattern_ = pattern;
  }
  const std::unordered_set<common::UUID>& service_search_pattern() const {
    return service_search_pattern_;
  }

  // The maximum count of records that should be included in any
  // response.
  void set_max_service_record_count(uint16_t count) {
    max_service_record_count_ = count;
  }
  uint16_t max_service_record_count() const {
    return max_service_record_count_;
  }

 private:
  std::unordered_set<common::UUID> service_search_pattern_;
  uint16_t max_service_record_count_;
};

class ServiceSearchResponse : public Response {
 public:
  ServiceSearchResponse();

  // Response overrides
  bool complete() const override;
  const common::BufferView ContinuationState() const override;
  Status Parse(const common::ByteBuffer& buf) override;
  common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const override;

  // The ServiceRecordHandleList contains as list of service record handles.
  // This should be set to the list of handles that match the request.
  // Limiting the response to the maximum requested is handled by
  // GetParameters();
  void set_service_record_handle_list(std::vector<ServiceHandle> handles) {
    service_record_handle_list_ = handles;
    total_service_record_count_ = handles.size();
  }
  std::vector<ServiceHandle> service_record_handle_list() const {
    return service_record_handle_list_;
  }

 private:
  // The list of service record handles.
  std::vector<ServiceHandle> service_record_handle_list_;
  // The total number of service records in the full response.
  uint16_t total_service_record_count_;

  common::ByteBufferPtr continuation_state_;
};

}  // namespace sdp
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_SDP_PDU_H_
