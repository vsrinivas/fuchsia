// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/mirror/server.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include "src/developer/shell/mirror/common.h"

namespace shell::mirror {

#define PRINT_FLUSH(...) \
  printf(__VA_ARGS__);   \
  fflush(stdout);

// SocketConnection --------------------------------------------------------------------------------

uint64_t SocketConnection::global_id_ = 0;

SocketConnection::~SocketConnection() {}

Err SocketConnection::Accept(debug_ipc::MessageLoop* main_thread_loop, int server_fd) {
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));

  socklen_t addrlen = sizeof(addr);
  fbl::unique_fd client(
      accept4(server_fd, reinterpret_cast<sockaddr*>(&addr), &addrlen, SOCK_NONBLOCK));
  if (!client.is_valid()) {
    return Err(kConnection, "Couldn't accept connection.");
  }

  main_thread_loop->PostTask(
      FROM_HERE, [this, client = std::move(client),
                  server_loop = debug_ipc::MessageLoop::Current()]() mutable {
        if (!buffer_.Init(std::move(client))) {
          FXL_LOG(ERROR) << "Error waiting for data.";
          debug_ipc::MessageLoop::Current()->QuitNow();
          return;
        }

        buffer_.set_data_available_callback(
            [buffer = &buffer_, path = this->server_->GetPath(), server_loop, connection = this]() {
              debug_ipc::StreamBuffer stream = buffer->stream();
              char buf[32];
              size_t len = stream.Read(buf, 32);
              if (len >= strlen(remote_commands::kQuitCommand) &&
                  strncmp(remote_commands::kQuitCommand, buf, len) == 0) {
                debug_ipc::MessageLoop::Current()->QuitNow();
                server_loop->QuitNow();
                return;
              }
              if (len >= strlen(remote_commands::kFilesCommand) &&
                  strncmp(remote_commands::kFilesCommand, buf, len) == 0) {
                Update update(&stream, &path);
                update.SendUpdates();
              } else {
                FXL_LOG(ERROR) << "Unrecognized command from socket";
              }

              connection->UnregisterAndDestroy();
            });
#if 0
        // TODO(jeremymanson): Should probably do something sensible here.
        buffer_.set_error_callback([]() {
          // Do this on Ctrl-C.
        });
#endif
      });

  PRINT_FLUSH("Accepted connection.\n");
  connected_ = true;
  return Err();
}

void SocketConnection::UnregisterAndDestroy() { server_->RemoveConnection(this); }

// SocketServer --------------------------------------------------------------------------------

void SocketServer::Run(ConnectionConfig config) {
  // Wait for one connection.
  PRINT_FLUSH("Waiting on port %d for shell connections...\n", config.port);
  config_ = config;
  connection_monitor_ = debug_ipc::MessageLoop::Current()->WatchFD(
      debug_ipc::MessageLoop::WatchMode::kRead, server_socket_.get(), this);
}

Err SocketServer::RunInLoop(ConnectionConfig config, debug_ipc::FileLineFunction from_here,
                            fit::closure inited_fn) {
  std::string init_error_message;

  // This loop manages incoming connections, and runs in this thread.
  debug_ipc::PlatformMessageLoop server_loop;
  if (!server_loop.Init(&init_error_message)) {
    return Err(kInit, init_error_message);
  }

  // Do appropriate init and start accepting connections.
  uint16_t port = static_cast<uint16_t>(config.port);
  Err err = Init(&port);
  if (!err.ok()) {
    server_loop.Cleanup();
    return err;
  }

  config.port = port;
  config.message_loop = &server_loop;
  Run(std::move(config));
  server_loop.PostTask(from_here, std::move(inited_fn));
  server_loop.Run();

  // Shut down the individual connections associated with the message loop.
  auto it = this->connections_.begin();
  while (it != this->connections_.end()) {
    it = this->connections_.begin();
    this->RemoveConnection(it->get());
  }

  // Shutdown.
  // Stop monitoring for new connections (otherwise the destructor complains).
  this->connection_monitor_.StopWatching();

  server_loop.Cleanup();
  return Err();
}

Err SocketServer::Init(uint16_t* port) {
  constexpr uint8_t kMaxAttempts = 6;
  for (uint8_t attempt = 0; attempt < kMaxAttempts; attempt++) {
    server_socket_.reset(socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP));
    if (!server_socket_.is_valid()) {
      return Err(kConnection, "Could not create socket: " + std::string(strerror(errno)));
    }

    // Bind to local address.
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(*port);
    if (bind(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      if (attempt != 0 || attempt == kMaxAttempts - 1) {
        // Either we tried a designated port in the first iteration, we tried 0 in the first
        // iteration and it couldn't give us anything, or we've tried as many times as we could.
        std::string msg = "Could not bind socket: ";
        msg += strerror(errno);
        return Err(kConnection, msg);
      } else {
        // We're looping - just try again with another port
        close(server_socket_.get());
        server_socket_.release();
        *port = 0;
        continue;
      }
    }

    if (*port != 0) {
      break;
    }

    // If port wasn't assigned, we want to get one assigned automatically.  We passed 0 in, which
    // means bind() will give you an unused ephemeral port, which is unbound.
    // Figure out which port was granted, close it, and then pretend it's the real port. Because
    // this is a bit racy (someone else might try to grab the port), do it up to |attempts| times.
    struct sockaddr_in6 addr_out;
    socklen_t out_length = sizeof(addr_out);
    if ((getsockname(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr), &out_length) < 0) ||
        out_length != sizeof(addr_out)) {
      std::string msg = "Could not get info for socket: ";
      msg += strerror(errno);
      return Err(kConnection, msg);
    }
    *port = addr_out.sin6_port;
    close(server_socket_.get());
    server_socket_.release();
  }

  if (listen(server_socket_.get(), 1) < 0) {
    std::string msg = "Could not listen on socket: ";
    msg += strerror(errno);
    return Err(kConnection, msg);
  }

  return Err();
}

void SocketServer::OnFDReady(int fd, bool readable, bool writeable, bool err) {
  if (!readable) {
    return;
  }

  // insert() returns a pair <iterator, bool>
  auto p = this->connections_.insert(std::make_unique<SocketConnection>(this));
  if (!p.second) {
    FXL_LOG(ERROR) << "Internal error";
    return;
  }
  Err error = p.first->get()->Accept(config_.message_loop, fd);
  if (!error.ok()) {
    FXL_LOG(INFO) << error.msg;
    return;
  }
  PRINT_FLUSH("Connection established.\n");
}

// SocketServer --------------------------------------------------------------------------------

Err Update::SendUpdates() {
  for (auto it_entry = std::filesystem::recursive_directory_iterator(path_);
       it_entry != std::filesystem::recursive_directory_iterator(); ++it_entry) {
    const std::string filename = std::filesystem::absolute(it_entry->path());
    files_.AddFile(filename.c_str());
  }
  std::vector<char> dumped_files;
  if (files_.DumpFiles(&dumped_files) != 0) {
    std::string msg = "Could not dump files: " + std::string(strerror(errno));
    return Err(kWrite, msg);
  }
  stream_->Write(dumped_files);
  return Err();
}

}  // namespace shell::mirror
