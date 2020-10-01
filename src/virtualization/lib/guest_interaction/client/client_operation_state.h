// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_OPERATION_STATE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_OPERATION_STATE_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/system/ulib/fit/include/lib/fit/function.h>

#include <grpc/support/log.h>

#include "fuchsia/netemul/guest/cpp/fidl.h"
#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

#include <grpc++/grpc++.h>

using TransferCallback = fit::function<void(zx_status_t status)>;

static inline zx_status_t translate_rpc_status(OperationStatus status) {
  switch (status) {
    case OperationStatus::OK:
      return ZX_OK;
    case OperationStatus::GRPC_FAILURE:
      return ZX_ERR_PEER_CLOSED;
    case OperationStatus::CLIENT_MISSING_FILE_FAILURE:
      return ZX_ERR_NOT_FOUND;
    case OperationStatus::CLIENT_CREATE_FILE_FAILURE:
      return ZX_ERR_ACCESS_DENIED;
    case OperationStatus::CLIENT_FILE_READ_FAILURE:
      return ZX_ERR_IO;
    case OperationStatus::CLIENT_FILE_WRITE_FAILURE:
      return ZX_ERR_IO;
    case OperationStatus::SERVER_MISSING_FILE_FAILURE:
      return ZX_ERR_NOT_FOUND;
    case OperationStatus::SERVER_CREATE_FILE_FAILURE:
      return ZX_ERR_ACCESS_DENIED;
    case OperationStatus::SERVER_FILE_READ_FAILURE:
      return ZX_ERR_IO;
    case OperationStatus::SERVER_FILE_WRITE_FAILURE:
      return ZX_ERR_IO;
    case OperationStatus::SERVER_EXEC_COMMAND_PARSE_FAILURE:
      return ZX_ERR_INVALID_ARGS;
    case OperationStatus::SERVER_EXEC_FORK_FAILURE:
      return ZX_ERR_INTERNAL;
    default:
      FX_LOGS(ERROR) << "Unknown gRPC transfer status: " << status;
      return ZX_ERR_BAD_STATE;
  }
}

// Manages the transfer of a file from the guest VM to the Fuchsia host.
//
// GetCallData will continually write new data from the guest into the
// specified destination location.  When the gRPC channel is terminated, the
// termination status is queried and final status is reported through the
// caller-supplied callback.
template <class T>
class GetCallData final : public CallData {
 public:
  GetCallData<T>(int32_t fd, TransferCallback callback)
      : status_(CREATE),
        callback_(std::move(callback)),
        fd_(fd),
        exit_status_(OperationStatus::OK) {}

  void Proceed(bool ok) override;

  grpc::ClientContext ctx_;
  std::unique_ptr<grpc::ClientAsyncReaderInterface<GetResponse>> reader_;
  GetResponse response_;
  T platform_interface_;

 private:
  enum CallStatus { CREATE, TRANSFER, FINISH };
  CallStatus status_;

  TransferCallback callback_;
  int32_t fd_;

  grpc::Status termination_status_;
  OperationStatus exit_status_;
};

// Proceed is called when the completion queue signals that the most recent
// Read operation has completed and there is new data that can be processed.
//
// From the gRPC documentation for a client Read operation:
// `ok` indicates whether there is a valid message that got read. If not, you
// know that there are certainly no more messages that can ever be read from
// this stream. For the client-side operations, this only happens because the
// call is dead.
//
// The client attempts to write incoming data into the open file until gRPC
// indicates that the call is dead at which point it queries for final status
// and reports the transfer status back to the caller through the callback.
template <class T>
void GetCallData<T>::Proceed(bool ok) {
  switch (status_) {
    case CREATE:
      if (!ok) {
        reader_->Finish(&termination_status_, this);
        exit_status_ = OperationStatus::GRPC_FAILURE;
        status_ = FINISH;
        return;
      }
      reader_->Read(&response_, this);
      status_ = TRANSFER;
      return;
    case TRANSFER:
      if (!ok) {
        reader_->Finish(&termination_status_, this);
        status_ = FINISH;
        return;
      }

      if (response_.status() != OperationStatus::OK) {
        exit_status_ = response_.status();
        reader_->Finish(&termination_status_, this);
        status_ = FINISH;
        return;
      }

      if (platform_interface_.WriteFile(fd_, response_.data().c_str(), response_.data().size()) <
          0) {
        exit_status_ = OperationStatus::CLIENT_FILE_WRITE_FAILURE;
        reader_->Finish(&termination_status_, this);
        status_ = FINISH;
        return;
      }

      reader_->Read(&response_, this);
      status_ = TRANSFER;
      return;
    case FINISH:
      platform_interface_.CloseFile(fd_);
      if (ok || exit_status_ != OperationStatus::OK) {
        callback_(translate_rpc_status(exit_status_));
      } else {
        callback_(translate_rpc_status(OperationStatus::GRPC_FAILURE));
      }
      delete this;
      return;
  }
}

template <class T>
class PutCallData final : public CallData {
 public:
  PutCallData(int32_t fd, std::string destination, TransferCallback callback)
      : status_(TRANSFER),
        destination_(destination),
        callback_(std::move(callback)),
        exit_status_(OperationStatus::OK),
        fd_(fd) {}

  void Proceed(bool ok) override;

  grpc::ClientContext ctx_;
  std::unique_ptr<grpc::ClientAsyncWriterInterface<PutRequest>> writer_;
  PutResponse response_;
  T platform_interface_;

 private:
  void Finish();

  enum CallStatus { TRANSFER, END_TRANSFER, FINISH };
  CallStatus status_;

  std::string destination_;
  TransferCallback callback_;
  OperationStatus exit_status_;
  int32_t fd_;
  grpc::Status finish_status_;
};

template <class T>
void PutCallData<T>::Proceed(bool ok) {
  // If the client gets a bad status while performing a streaming write, then
  // the call is dead and no future messages will be sent.
  if (!ok) {
    exit_status_ = OperationStatus::GRPC_FAILURE;
    Finish();
    return;
  }

  switch (status_) {
    case TRANSFER: {
      PutRequest req;
      req.set_destination(destination_);
      char read_buf[CHUNK_SIZE];
      ssize_t data_read = platform_interface_.ReadFile(fd_, read_buf, CHUNK_SIZE);
      if (data_read < 0) {
        if (data_read != -EAGAIN && data_read != -EWOULDBLOCK) {
          // Read failed.
          exit_status_ = OperationStatus::CLIENT_FILE_READ_FAILURE;
          status_ = END_TRANSFER;
          writer_->WritesDone(this);
          return;
        }
        // Read would have caused to block, send empty data.
        req.clear_data();
        writer_->Write(req, this);
        return;
      }
      if (data_read == 0) {
        // Read hit EOF.
        status_ = END_TRANSFER;
        req.clear_data();
        writer_->WritesDone(this);
        return;
      }
      req.set_data(read_buf, data_read);
      writer_->Write(req, this);
      return;
    }
    case END_TRANSFER:
      writer_->Finish(&finish_status_, this);
      status_ = FINISH;
      return;
    case FINISH:
      if (response_.status() != OperationStatus::OK) {
        exit_status_ = response_.status();
      }
      Finish();
      return;
  }
}

template <class T>
void PutCallData<T>::Finish() {
  if (fd_ > 0) {
    platform_interface_.CloseFile(fd_);
  }
  callback_(translate_rpc_status(exit_status_));
  delete this;
}

class ListenerInterface : fuchsia::netemul::guest::CommandListener {
 public:
  explicit ListenerInterface(fidl::InterfaceRequest<fuchsia::netemul::guest::CommandListener> req)
      : binding_(this, std::move(req)) {}

  void OnStarted(zx_status_t status) { binding_.events().OnStarted(status); }

  void OnTerminated(zx_status_t status, int32_t ret_code) {
    binding_.events().OnTerminated(status, ret_code);
  }

 private:
  fidl::Binding<fuchsia::netemul::guest::CommandListener> binding_;
};

// Pump incoming stdin into the child process managed by the guest service.
template <class T>
class ExecWriteCallData final : public CallData {
 public:
  ExecWriteCallData(
      const std::string& command, const std::vector<ExecEnv>& env, int32_t std_in,
      const std::shared_ptr<grpc::ClientContext>& ctx,
      const std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>>& rw);

  void Proceed(bool ok) override;

  T platform_interface_;

 private:
  void Finish();

  int32_t stdin_;
  std::shared_ptr<grpc::ClientContext> ctx_;
  std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>> writer_;

  enum CallStatus { WRITING, FINISH };
  CallStatus status_;
};

template <class T>
class ExecReadCallData final : public CallData {
 public:
  ExecReadCallData(
      int32_t std_out, int32_t std_err, const std::shared_ptr<grpc::ClientContext>& ctx,
      const std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>>& rw,
      std::unique_ptr<ListenerInterface> listener);

  void Proceed(bool ok) override;

  T platform_interface_;

 private:
  void Finish();

  int32_t stdout_;
  int32_t stderr_;
  std::shared_ptr<grpc::ClientContext> ctx_;
  std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>> reader_;
  std::unique_ptr<ListenerInterface> listener_;
  int32_t ret_val_;

  ExecResponse response_;
  OperationStatus operation_status_;

  enum CallStatus { READ, FINISH };
  CallStatus status_;

  grpc::Status grpc_stream_status_;
};

template <class T>
class ExecCallData final : public CallData {
 public:
  ExecCallData(const std::string& command, const std::map<std::string, std::string>& env_vars,
               int32_t std_in, int32_t std_out, int32_t std_err,
               std::unique_ptr<ListenerInterface> listener);

  void Proceed(bool ok) override;

  std::shared_ptr<grpc::ClientContext> ctx_;
  std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>> rw_;
  T platform_interface_;

 private:
  std::vector<ExecEnv> EnvMapToVector(const std::map<std::string, std::string>& env_vars);

  int32_t stdin_;
  int32_t stdout_;
  int32_t stderr_;
  std::unique_ptr<ListenerInterface> listener_;
  std::string command_;
  std::vector<ExecEnv> env_;
};

template <class T>
ExecWriteCallData<T>::ExecWriteCallData(
    const std::string& command, const std::vector<ExecEnv>& env, int32_t std_in,
    const std::shared_ptr<grpc::ClientContext>& ctx,
    const std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>>& rw)
    : stdin_(std_in), ctx_(ctx), writer_(rw), status_(WRITING) {
  // Send over the initial command request to get the child process
  // running.  stdin will be pumped once the first write request finishes.
  ExecRequest exec_request;

  exec_request.set_argv(command);

  for (const ExecEnv& key_val : env) {
    ExecEnv* new_env = exec_request.add_env_vars();
    *new_env = key_val;
  }

  exec_request.clear_std_in();

  writer_->Write(exec_request, this);
}

template <class T>
void ExecWriteCallData<T>::Proceed(bool ok) {
  if (!ok) {
    // gRPC has shut down the connection.
    Finish();
    return;
  }
  if (status_ != WRITING) {
    GPR_ASSERT(status_ == FINISH);
    Finish();
    return;
  }

  char read_buf[CHUNK_SIZE];
  ssize_t read_status = platform_interface_.ReadFile(stdin_, read_buf, CHUNK_SIZE);
  if (read_status == -EAGAIN || read_status == -EWOULDBLOCK) {
    // Reading would have caused blocking, so send back an empty message.
    ExecRequest exec_request;
    exec_request.clear_argv();
    exec_request.clear_env_vars();
    exec_request.clear_std_in();
    writer_->Write(exec_request, this);
  } else if (read_status <= 0) {
    // Reading failed in an unexpected way.  Notify client and finish.
    writer_->WritesDone(this);
    status_ = FINISH;
  } else {
    std::string new_stdin(read_buf, read_status);

    ExecRequest exec_request;
    exec_request.clear_argv();
    exec_request.clear_env_vars();
    exec_request.set_std_in(new_stdin);

    writer_->Write(exec_request, this);
  }
}

template <class T>
void ExecWriteCallData<T>::Finish() {
  platform_interface_.CloseFile(stdin_);
  delete this;
}

template <class T>
ExecReadCallData<T>::ExecReadCallData(
    int32_t std_out, int32_t std_err, const std::shared_ptr<grpc::ClientContext>& ctx,
    const std::shared_ptr<grpc::ClientAsyncReaderWriterInterface<ExecRequest, ExecResponse>>& rw,
    std::unique_ptr<ListenerInterface> listener)
    : stdout_(std_out),
      stderr_(std_err),
      ctx_(ctx),
      reader_(rw),
      listener_(std::move(listener)),
      ret_val_(0),
      status_(READ) {
  reader_->Read(&response_, this);
}

template <class T>
void ExecReadCallData<T>::Proceed(bool ok) {
  if (!ok) {
    reader_->Finish(&grpc_stream_status_, this);
    status_ = FINISH;
    return;
  }
  if (status_ != READ) {
    GPR_ASSERT(status_ == FINISH);
    if (!grpc_stream_status_.ok() && operation_status_ == OperationStatus::OK) {
      operation_status_ = OperationStatus::GRPC_FAILURE;
    }
    Finish();
    listener_->OnTerminated(translate_rpc_status(operation_status_), ret_val_);
    delete this;
    return;
  }

  // Record the statuses at every report.  The last responses will be
  // passed as arguments to the supplied callback.
  ret_val_ = response_.ret_code();
  operation_status_ = response_.status();

  std::string new_stdout = response_.std_out();
  std::string new_stderr = response_.std_err();

  platform_interface_.WriteFile(stdout_, new_stdout.c_str(), new_stdout.size());
  platform_interface_.WriteFile(stderr_, new_stderr.c_str(), new_stderr.size());

  reader_->Read(&response_, this);
}

template <class T>
void ExecReadCallData<T>::Finish() {
  platform_interface_.CloseFile(stdout_);
  platform_interface_.CloseFile(stderr_);
}

template <class T>
ExecCallData<T>::ExecCallData(const std::string& command,
                              const std::map<std::string, std::string>& env_vars, int32_t std_in,
                              int32_t std_out, int32_t std_err,
                              std::unique_ptr<ListenerInterface> listener)
    : ctx_(std::make_shared<grpc::ClientContext>()),
      stdin_(std_in),
      stdout_(std_out),
      stderr_(std_err),
      listener_(std::move(listener)),
      command_(std::move(command)),
      env_(std::move(EnvMapToVector(env_vars))) {}

template <class T>
void ExecCallData<T>::Proceed(bool ok) {
  if (!ok) {
    platform_interface_.CloseFile(stdin_);
    platform_interface_.CloseFile(stdout_);
    platform_interface_.CloseFile(stderr_);

    listener_->OnStarted(ZX_ERR_INTERNAL);
    listener_->OnTerminated(translate_rpc_status(OperationStatus::GRPC_FAILURE), 0);
  } else {
    listener_->OnStarted(ZX_OK);

    new ExecWriteCallData<T>(std::move(command_), std::move(env_), stdin_, ctx_, rw_);
    new ExecReadCallData<T>(stdout_, stderr_, ctx_, rw_, std::move(listener_));
  }
  delete this;
}

template <class T>
std::vector<ExecEnv> ExecCallData<T>::EnvMapToVector(
    const std::map<std::string, std::string>& env_vars) {
  std::vector<ExecEnv> env;

  for (auto const& [key, value] : env_vars) {
    ExecEnv& env_var = env.emplace_back();
    env_var.set_key(key);
    env_var.set_value(value);
  }

  return env;
}

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_OPERATION_STATE_H_
