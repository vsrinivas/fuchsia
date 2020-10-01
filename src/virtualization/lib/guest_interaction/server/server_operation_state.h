// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_OPERATION_STATE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_OPERATION_STATE_H_

#include <lib/syslog/cpp/macros.h>

#include <filesystem>

#include <grpc/support/log.h>

#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

#include <grpc++/grpc++.h>

// Manages the transfer of a file from the guest VM to the host.
//
// When the client requests a file, the server sends a stream of messages
// containing the file's contents until either gRPC breaks or the server
// hits the end of the requested file.
template <class T>
class GetCallData final : public CallData {
 public:
  GetCallData(GuestInteractionService::AsyncService* service, grpc::ServerCompletionQueue* cq);
  void Proceed(bool ok) override;

  T platform_interface_;

 private:
  void TryRead();
  void Finish();

  GuestInteractionService::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;
  grpc::ServerContext ctx_;
  grpc::ServerAsyncWriter<GetResponse> writer_;
  GetRequest request_;
  int32_t fd_;

  enum CallStatus { CREATE, INITIATE_TRANSFER, TRANSFER, END_TRANSFER, FINISH };
  CallStatus status_;
};

template <class T>
GetCallData<T>::GetCallData(GuestInteractionService::AsyncService* service,
                            grpc::ServerCompletionQueue* cq)
    : service_(service), cq_(cq), writer_(&ctx_), fd_(0), status_(CREATE) {
  Proceed(true);
}

// Tracks the state of a file transfer from the guest VM to the host.
//
// When instantiated, GetCallData immediately calls Proceed which calls
// RequestGet so that the server can handle incoming Get requests.
//
// The client specifies the source file in its initial request and then the
// server streams the file contents back to the guest until either the gRPC
// channel breaks (ok == false) or it hits EOF on the source file.
//
// According to the gRPC docs for a server Write operation:
// ok means that the data/metadata/status/etc is going to go to the wire. If it
// is false, it not going to the wire because the call is already dead (i.e.,
// canceled, deadline expired, other side dropped the channel, etc).
template <class T>
void GetCallData<T>::Proceed(bool ok) {
  if (!ok) {
    Finish();
    return;
  }

  switch (status_) {
    case CREATE:
      status_ = INITIATE_TRANSFER;
      service_->RequestGet(&ctx_, &request_, &writer_, cq_, cq_, this);
      return;
    case INITIATE_TRANSFER:
      // Allow new GetRequest streams to be handled.
      new GetCallData<T>(service_, cq_);

      if (!platform_interface_.FileExists(request_.source())) {
        GetResponse get_response;
        get_response.clear_data();
        get_response.set_status(OperationStatus::SERVER_MISSING_FILE_FAILURE);
        writer_.Write(get_response, this);
        status_ = END_TRANSFER;
        return;
      }

      fd_ = platform_interface_.OpenFile(request_.source(), READ);
      if (fd_ < 0) {
        GetResponse get_response;
        get_response.clear_data();
        get_response.set_status(OperationStatus::SERVER_FILE_READ_FAILURE);
        writer_.Write(get_response, this);
        status_ = END_TRANSFER;
        return;
      }

      status_ = TRANSFER;
      TryRead();
      return;
    case TRANSFER:
      TryRead();
      return;
    case END_TRANSFER:
      writer_.Finish(grpc::Status::OK, this);
      status_ = FINISH;
      return;
    case FINISH:
      Finish();
      return;
  }
}

template <class T>
void GetCallData<T>::TryRead() {
  GetResponse get_response;
  char data_chunk[CHUNK_SIZE];
  ssize_t data_read = platform_interface_.ReadFile(fd_, data_chunk, CHUNK_SIZE);

  if (data_read < 0) {
    if (-data_read == EAGAIN || -data_read == EWOULDBLOCK) {
      // Reading would have caused blocking, so send back an empty message.
      get_response.set_status(OperationStatus::OK);
      get_response.clear_data();
      writer_.Write(get_response, this);
    } else {
      // Reading failed in an unexpected way.  Notify client and finish.
      get_response.set_status(OperationStatus::SERVER_FILE_READ_FAILURE);
      get_response.clear_data();
      writer_.Write(get_response, this);
      status_ = END_TRANSFER;
    }
  } else if (data_read == 0) {
    // Read size of 0 indicates EOF.
    get_response.set_status(OperationStatus::OK);
    get_response.clear_data();
    writer_.Write(get_response, this);
    status_ = END_TRANSFER;
  } else {
    get_response.set_status(OperationStatus::OK);
    get_response.set_data(data_chunk, data_read);
    writer_.Write(get_response, this);
  }
}

template <class T>
void GetCallData<T>::Finish() {
  if (fd_ > 0) {
    platform_interface_.CloseFile(fd_);
  }
  delete this;
}

template <class T>
class PutCallData final : public CallData {
 public:
  PutCallData(GuestInteractionService::AsyncService* service, grpc::ServerCompletionQueue* cq)
      : service_(service), cq_(cq), reader_(&ctx_), status_(CREATE), fd_(0) {
    Proceed(true);
  }
  void Proceed(bool ok) override;

  T platform_interface_;

 private:
  // Attempt to read the latest message from the client and write it into
  // the output file.  If the output file stream has gone into a bad state
  // or the client has sent a final empty byte
  void TryWrite();
  void SendFinalStatus(OperationStatus status);

  // gRPC async boilerplate
  GuestInteractionService::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;
  grpc::ServerContext ctx_;
  grpc::ServerAsyncReader<PutResponse, PutRequest> reader_;

  enum CallStatus { CREATE, INITIATE_TRANSFER, TRANSFER, FINISH };
  CallStatus status_;
  int32_t fd_;

  // File transfer bits
  PutRequest new_request_;
};

template <class T>
void PutCallData<T>::Proceed(bool ok) {
  switch (status_) {
    case CREATE:
      status_ = INITIATE_TRANSFER;
      service_->RequestPut(&ctx_, &reader_, cq_, cq_, this);
      return;
    case INITIATE_TRANSFER:
      // Allow new PutRequest streams to be handled.
      new PutCallData(service_, cq_);
      reader_.Read(&new_request_, this);
      status_ = TRANSFER;
      return;
    case TRANSFER:
      if (!ok) {
        SendFinalStatus(OperationStatus::OK);
        return;
      }
      TryWrite();
      return;
    case FINISH:
      if (fd_ > 0) {
        platform_interface_.CloseFile(fd_);
      }
      delete this;
      return;
  }
}

template <class T>
void PutCallData<T>::SendFinalStatus(OperationStatus status) {
  PutResponse put_response;
  put_response.set_status(status);
  reader_.Finish(put_response, grpc::Status::OK, this);
  status_ = FINISH;
}

template <class T>
void PutCallData<T>::TryWrite() {
  if (fd_ == 0) {
    std::string destination = new_request_.destination();
    std::filesystem::path outpath = destination;
    if (platform_interface_.DirectoryExists(destination) ||
        (destination.length() > 0 && destination[destination.length() - 1] == '/')) {
      // If the client provides the path to a directory, return a failure.
      SendFinalStatus(OperationStatus::SERVER_CREATE_FILE_FAILURE);
      return;
    } else if (!platform_interface_.DirectoryExists(outpath.parent_path().string())) {
      // If the client wants to send the file to a nonexistent directory,
      // create it for them.
      if (!platform_interface_.CreateDirectory(outpath.parent_path().string())) {
        SendFinalStatus(OperationStatus::SERVER_CREATE_FILE_FAILURE);
        return;
      }
    }

    fd_ = platform_interface_.OpenFile(destination, WRITE);
  }

  if (fd_ < 0) {
    SendFinalStatus(OperationStatus::SERVER_FILE_WRITE_FAILURE);
    return;
  }

  if (platform_interface_.WriteFile(fd_, new_request_.data().c_str(),
                                    new_request_.data().length()) < 0) {
    SendFinalStatus(OperationStatus::SERVER_FILE_WRITE_FAILURE);
  }
  reader_.Read(&new_request_, this);
}

// The Exec operation provides a bidirectional streaming interface for the
// client.  The client can stream input to the running exec-ed program's stdin
// and the server replies with a stdout/stderr stream and ultimately provides a
// return code.
//
// The requested program needs to initially be exec-ed and then the input and
// output streams must be continually serviced by read and write routines.
// There are some complications imposed by the limitations of the gRPC
// CompletionQueue.
// 1. Only one outstanding read operation can be queued for a given stream.
// 2. Only one outstanding write operation can be queued for a given stream.
// 3. The completion queue makes no distinction between read and write
//    operations when they complete.
//
// To ensure that multiple Reads or Writes are not enqueued, read and write
// state must be tracked in separate state machines.  An initial ExecCallData
// services incoming client requests.  ExecCallData forks a process and hands
// off a stdin pipe to an ExecReadCallData and hands stdout and stderr pipes to
// an ExecWriteCallData.  The ExecReadCallData and ExecWriteCallData each
// manage one direction of the data stream and enforce the single read/single
// write rule.
//
// The writer is ultimately responsible for reporting the final exit status to
// the client, so it is the responsibility of the writer to reap the pid of the
// child process.

// While a program is running under the supervision of the guest interaction
// daemon, the caller may continue to stream input to the programs stdin.  The
// ExecReadCallData is tasked with consuming the incoming stream of caller
// stdin and writing it to the running program's stdin.
template <class T>
class ExecReadCallData final : public CallData {
 public:
  ExecReadCallData(
      std::shared_ptr<grpc::ServerContext> ctx,
      const std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>& rw,
      int child_pid, int stdin_fd);

  void Proceed(bool ok) override;
  void Finish();

  T platform_interface_;

 private:
  std::shared_ptr<grpc::ServerContext> ctx_;
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> reader_;
  int child_pid_;
  int stdin_fd_;
  ExecRequest request_;
};

// While a child process is running, ExecWriteCallData streams its stdin/stderr
// back to the client.  When the child process exits, the ExecWriteCallData
// sends all remaining stdout/stderr back to the client along with the return
// code.
template <class T>
class ExecWriteCallData final : public CallData {
 public:
  ExecWriteCallData(
      std::shared_ptr<grpc::ServerContext> ctx,
      const std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>& rw,
      int child_pid, int stdout_fd, int stderr_fd);

  // Read out of stdin and stderr.  Send any new information back to the
  // client.  If the program has exited, also include the exit code.
  void Proceed(bool ok) override;
  void Finish();

  T platform_interface_;

 private:
  std::string ReadFd(int fd);
  std::string DrainFd(int fd);

  std::shared_ptr<grpc::ServerContext> ctx_;
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> writer_;
  int child_pid_;
  int stdout_fd_;
  int stderr_fd_;

  enum CallStatus { WRITE, FINISH };
  CallStatus status_;
};

template <class T>
class ExecCallData final : public CallData {
 public:
  ExecCallData(GuestInteractionService::AsyncService* service, grpc::ServerCompletionQueue* cq);
  void Proceed(bool ok) override;

  T platform_interface_;

 private:
  std::vector<std::string> CreateEnv(const ExecRequest& exec_request);

  GuestInteractionService::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;
  std::shared_ptr<grpc::ServerContext> ctx_;
  std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>> stream_;
  ExecRequest exec_request_;

  enum CallStatus { INITIATE_READ, FORK, FINISH_IN_ERROR };
  CallStatus status_;
};

template <class T>
ExecReadCallData<T>::ExecReadCallData(
    std::shared_ptr<grpc::ServerContext> ctx,
    const std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>& rw,
    int child_pid, int stdin_fd)
    : ctx_(ctx), reader_(rw), child_pid_(child_pid), stdin_fd_(stdin_fd) {
  reader_->Read(&request_, this);
}

template <class T>
void ExecReadCallData<T>::Proceed(bool ok) {
  // If not ok, then the client has finished the write stream.  Clean up
  // the file descriptors and delete the reader.
  if (!ok) {
    Finish();
    return;
  }

  int kill_response = platform_interface_.KillPid(child_pid_, 0);
  if (kill_response != 0) {
    // The child process no longer exists and any new stdin from the
    // client is meaningless.
    Finish();
    return;
  }

  // The command, arguments, and environment variables can only be used
  // when initially forking the child process, so they are meaningless
  // when the child process is running.  Only handle the stdin here.
  ssize_t write_status =
      platform_interface_.WriteFile(stdin_fd_, request_.std_in().c_str(), request_.std_in().size());

  if (write_status < 0) {
    Finish();
    return;
  }
  reader_->Read(&request_, this);
}

template <class T>
void ExecReadCallData<T>::Finish() {
  platform_interface_.CloseFile(stdin_fd_);
  delete this;
}

template <class T>
ExecWriteCallData<T>::ExecWriteCallData(
    std::shared_ptr<grpc::ServerContext> ctx,
    const std::shared_ptr<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>& rw,
    int child_pid, int stdout_fd, int stderr_fd)
    : ctx_(ctx),
      writer_(rw),
      child_pid_(child_pid),
      stdout_fd_(stdout_fd),
      stderr_fd_(stderr_fd),
      status_(WRITE) {
  Proceed(true);
}

template <class T>
void ExecWriteCallData<T>::Proceed(bool ok) {
  if (!ok) {
    platform_interface_.KillPid(child_pid_, 9);
    int status;
    platform_interface_.WaitPid(child_pid_, &status, 0);
    Finish();
    return;
  }
  if (status_ != WRITE) {
    GPR_ASSERT(status_ == FINISH);
    Finish();
    return;
  }

  int status;
  int32_t poll_pid = platform_interface_.WaitPid(child_pid_, &status, WNOHANG);

  std::string std_out;
  std::string std_err;
  ExecResponse exec_response;
  if (poll_pid != 0) {
    std_out = DrainFd(stdout_fd_);
    std_err = DrainFd(stderr_fd_);

    exec_response.set_std_out(std_out);
    exec_response.set_std_err(std_err);
    exec_response.set_ret_code(WEXITSTATUS(status));
    exec_response.set_status(OperationStatus::OK);

    writer_->WriteAndFinish(exec_response, grpc::WriteOptions(), grpc::Status::OK, this);
    status_ = FINISH;
  } else {
    std_out = ReadFd(stdout_fd_);
    std_err = ReadFd(stderr_fd_);

    exec_response.set_std_out(std_out);
    exec_response.set_std_err(std_err);
    exec_response.clear_ret_code();
    exec_response.clear_status();

    writer_->Write(exec_response, this);
  }
}

template <class T>
void ExecWriteCallData<T>::Finish() {
  platform_interface_.CloseFile(stdout_fd_);
  platform_interface_.CloseFile(stderr_fd_);
  delete this;
}

template <class T>
std::string ExecWriteCallData<T>::ReadFd(int32_t fd) {
  std::string out_string;
  char read_buf[CHUNK_SIZE / 2];
  ssize_t bytes_read = platform_interface_.ReadFile(fd, read_buf, CHUNK_SIZE / 2 - 1);

  if (bytes_read < 0) {
    read_buf[0] = '\0';
  } else {
    read_buf[bytes_read] = '\0';
  }

  out_string = std::string(read_buf);
  return out_string;
}

template <class T>
std::string ExecWriteCallData<T>::DrainFd(int32_t fd) {
  std::string return_string = "";
  while (true) {
    std::string new_substring = ReadFd(fd);

    if (new_substring.size() > 0) {
      return_string += new_substring;
    } else {
      return return_string;
    }
  }
}

template <class T>
ExecCallData<T>::ExecCallData(GuestInteractionService::AsyncService* service,
                              grpc::ServerCompletionQueue* cq)
    : service_(service), cq_(cq), status_(INITIATE_READ) {
  // The ServerContext provides connection metadata to the streaming
  // reader/writer.  No context actually needs to be preserved, but
  // ExecCallData is just the entry point to the exec process.  Once the
  // child is forked, the ExecCallData is deleted.  To ensure that a valid
  // ServerContext can always be read by the reader/writer, allocate one
  // here and later pass a shared reference around to the read and write
  // routines.
  ctx_ = std::make_shared<grpc::ServerContext>();
  stream_ = std::make_shared<grpc::ServerAsyncReaderWriter<ExecResponse, ExecRequest>>(ctx_.get());
  service_->RequestExec(ctx_.get(), stream_.get(), cq_, cq_, this);
}

// Do one initial read to get the client's command.  Upon receiving the
// first command, fork and hand off control to a dedicated reader/writer.
template <class T>
void ExecCallData<T>::Proceed(bool ok) {
  if (status_ == INITIATE_READ) {
    new ExecCallData(service_, cq_);
    stream_->Read(&exec_request_, this);
    status_ = FORK;
  } else if (status_ != FORK) {
    GPR_ASSERT(status_ == FINISH_IN_ERROR);
    delete this;
    return;
  } else {
    // Generate string forms of the argv and environment variables
    std::vector<std::string> args = platform_interface_.ParseCommand(exec_request_.argv());
    std::vector<std::string> env = CreateEnv(exec_request_);

    // Repackage the string representations as C-strings
    std::vector<char*> exec_args;
    for (const std::string& arg : args) {
      exec_args.push_back(const_cast<char*>(arg.c_str()));
    }
    exec_args.push_back(nullptr);

    std::vector<char*> exec_env;
    for (const std::string& env_pair : env) {
      exec_env.push_back(const_cast<char*>(env_pair.c_str()));
    }
    exec_env.push_back(nullptr);

    // Create the arguments to exec from the C-string vectors
    char** args_ptr;
    if (args.empty()) {
      ExecResponse exec_response;
      exec_response.clear_std_out();
      exec_response.clear_std_err();
      exec_response.clear_ret_code();
      exec_response.set_status(OperationStatus::SERVER_EXEC_COMMAND_PARSE_FAILURE);

      stream_->WriteAndFinish(exec_response, grpc::WriteOptions(), grpc::Status::OK, this);
      status_ = FINISH_IN_ERROR;

      return;
    }

    args_ptr = exec_args.data();

    char** env_ptr;
    if (exec_env.empty()) {
      env_ptr = nullptr;
    } else {
      env_ptr = exec_env.data();
    }

    // File descriptors to be populated when exec-ing
    int32_t std_in;
    int32_t std_out;
    int32_t std_err;

    // fork and exec
    int32_t child_pid = platform_interface_.Exec(args_ptr, env_ptr, &std_in, &std_out, &std_err);

    if (child_pid < 0) {
      ExecResponse exec_response;
      exec_response.clear_std_out();
      exec_response.clear_std_err();
      exec_response.clear_ret_code();
      exec_response.set_status(OperationStatus::SERVER_EXEC_FORK_FAILURE);

      stream_->WriteAndFinish(exec_response, grpc::WriteOptions(), grpc::Status::OK, this);

      platform_interface_.CloseFile(std_in);
      platform_interface_.CloseFile(std_out);
      platform_interface_.CloseFile(std_err);
      status_ = FINISH_IN_ERROR;
      return;
    }
    // Set read FD's to nonblocking mode.
    platform_interface_.SetFileNonblocking(std_out);
    platform_interface_.SetFileNonblocking(std_err);

    // If the client specified any stdin, write that into the stdin FD.
    platform_interface_.WriteFile(std_in, exec_request_.std_in().c_str(),
                                  exec_request_.std_in().size());

    new ExecReadCallData<T>(ctx_, stream_, child_pid, std_in);
    new ExecWriteCallData<T>(ctx_, stream_, child_pid, std_out, std_err);
    delete this;
    return;
  }
}

template <class T>
std::vector<std::string> ExecCallData<T>::CreateEnv(const ExecRequest& exec_request) {
  std::vector<std::string> env;
  for (const ExecEnv& env_var : exec_request.env_vars()) {
    std::string env_pair = env_var.key() + "=" + env_var.value();
    env.push_back(env_pair);
  }
  return env;
}

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_OPERATION_STATE_H_
