/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "socket.h"

#include <ctype.h>
#include <errno.h>
#include <fidl/fuchsia.hardware.adb/cpp/fidl.h>
#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

#include "adb-base.h"
#include "adb-protocol.h"
#include "transport.h"
#include "types.h"

using namespace std::chrono_literals;

static std::recursive_mutex& local_socket_list_lock = *new std::recursive_mutex();
static unsigned local_socket_next_id = 1;

static auto& local_socket_list = *new std::vector<asocket*>();

/* the the list of currently closing local sockets.
** these have no peer anymore, but still packets to
** write to their fd.
*/
static auto& local_socket_closing_list = *new std::vector<asocket*>();

// Parse the global list of sockets to find one with id |local_id|.
// If |peer_id| is not 0, also check that it is connected to a peer
// with id |peer_id|. Returns an asocket handle on success, NULL on failure.
asocket* find_local_socket(unsigned local_id, unsigned peer_id) {
  asocket* result = nullptr;

  std::lock_guard<std::recursive_mutex> lock(local_socket_list_lock);
  for (asocket* s : local_socket_list) {
    if (s->id != local_id) {
      continue;
    }
    if (peer_id == 0 || (s->peer && s->peer->id == peer_id)) {
      result = s;
    }
    break;
  }

  return result;
}

void install_local_socket(asocket* s) {
  std::lock_guard<std::recursive_mutex> lock(local_socket_list_lock);

  s->id = local_socket_next_id++;

  // Socket ids should never be 0.
  if (local_socket_next_id == 0) {
    FX_LOGS(ERROR) << "local socket id overflow";
  }

  local_socket_list.push_back(s);
}

void remove_socket(asocket* s) {
  std::lock_guard<std::recursive_mutex> lock(local_socket_list_lock);
  for (auto list : {&local_socket_list, &local_socket_closing_list}) {
    list->erase(std::remove_if(list->begin(), list->end(), [s](asocket* x) { return x == s; }),
                list->end());
  }
}

void close_all_sockets(atransport* t) {
  /* this is a little gross, but since s->close() *will* modify
  ** the list out from under you, your options are limited.
  */
  std::lock_guard<std::recursive_mutex> lock(local_socket_list_lock);
restart:
  for (asocket* s : local_socket_list) {
    if (s->transport == t || (s->peer && s->peer->transport == t)) {
      s->close(s);
      goto restart;
    }
  }
}

enum class SocketFlushResult {
  Destroyed,
  TryAgain,
  Completed,
};

zx_status_t local_socket_write(asocket* s, const void* buffer, size_t len, size_t* actual) {
  if (s->zx_socket.is_valid()) {
    return s->zx_socket.write(0, buffer, len, actual);
  }
  FX_LOGS(ERROR) << "LS(" << s->id << "): local write no matching socket";
  return ZX_ERR_INTERNAL;
}

static SocketFlushResult local_socket_flush_incoming(asocket* s) {
  FX_LOGS(DEBUG) << "LS(" << s->id << ") " << __func__ << ": " << s->packet_queue.size()
                 << " bytes in queue";
  uint32_t bytes_flushed = 0;
  if (!s->packet_queue.empty()) {
    size_t rc = 0;
    auto status = local_socket_write(s, s->packet_queue.data(), s->packet_queue.size(), &rc);
    FX_LOGS(DEBUG) << "LS(" << s->id << ") " << __func__ << ": rc = " << rc;
    // write_test_data(s);
    if (rc > 0) {
      bytes_flushed += rc;
      if (static_cast<size_t>(rc) == s->packet_queue.size()) {
        s->packet_queue.clear();
      } else {
        auto begin = s->packet_queue.begin();
        s->packet_queue.erase(begin, begin + rc);
      }
    } else if (status == ZX_ERR_SHOULD_WAIT) {
      // fd is full.
    } else {
      // rc == 0, probably.
      // The other side closed its read side of the fd, but it's possible that we can still
      // read from the socket. Give that a try before giving up.
      s->has_write_error = true;
    }
  }

  bool fd_full = !s->packet_queue.empty() && !s->has_write_error;
  if (s->transport && s->peer) {
    if (s->available_send_bytes.has_value()) {
      // Deferred acks are available.
      send_ready(s->id, s->peer->id, s->transport, bytes_flushed);
    } else {
      // Deferred acks aren't available, we should ask for more data as long as we've made any
      // progress.
      if (bytes_flushed != 0) {
        send_ready(s->id, s->peer->id, s->transport, 0);
      }
    }
  }

  // If we sent the last packet of a closing socket, we can now destroy it.
  if (s->closing) {
    s->close(s);
    return SocketFlushResult::Destroyed;
  }

  if (fd_full) {
    // fdevent_add(s->fde, FDE_WRITE);
    return SocketFlushResult::TryAgain;
  } else {
    // fdevent_del(s->fde, FDE_WRITE);
    return SocketFlushResult::Completed;
  }
}

zx_status_t local_socket_read(asocket* s, void* buffer, size_t len, size_t* actual) {
  *actual = 0;
  if (s->zx_socket.is_valid()) {
    zx_signals_t pending = 0;
    zx_status_t status = s->zx_socket.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                                               zx::deadline_after(zx::sec(1)), &pending);
    if (status != ZX_OK) {
      if (status == ZX_ERR_TIMED_OUT) {
        FX_LOGS(DEBUG) << "LS(" << s->id << "): read timed out";
      } else {
        FX_LOGS(ERROR) << "LS(" << s->id << "): read socket wait failed" << status;
      }
      return status;
    }

    if (pending & ZX_SOCKET_PEER_CLOSED) {
      return ZX_ERR_PEER_CLOSED;
    }

    return s->zx_socket.read(0u, buffer, len, actual);
  }

  FX_LOGS(ERROR) << "LS(" << s->id << "): local read no matching socket";
  return ZX_ERR_INTERNAL;
}

// Returns false if the socket has been closed and destroyed as a side-effect of this function.
static bool local_socket_flush_outgoing(asocket* s) {
  const size_t max_payload = 1024;  // s->get_max_payload();
  apacket::payload_type data;
  data.resize(max_payload);
  size_t r = 0;
  int is_eof = 0;

  auto status = local_socket_read(s, data.data(), max_payload, &r);

  FX_LOGS(DEBUG) << "LS(" << s->id << "): post adb_read() r=" << r << " (error=" << status << ")";
  if (status != ZX_OK) {
    if (status == ZX_ERR_SHOULD_WAIT || status == ZX_ERR_TIMED_OUT) {
      return true;
    }
    return false;
  }

  /* r = 0 or unhandled error */
  if (r == 0) {
    is_eof = 1;
    FX_LOGS(ERROR) << "LS(" << s->id << "): socket read eof. status - " << status << ", r - " << r;
  }
  FX_LOGS(DEBUG) << "LS(" << s->id << "): " /* fd=" << s->fd */ << "post avail loop. r=" << r
                 << " is_eof=" << is_eof;

  if (r < 0) {
    // Error return means they closed us as a side-effect and we must
    // return immediately.
    //
    // Note that if we still have buffered packets, the socket will be
    // placed on the closing socket list. This handler function will be
    // called again to process FDE_WRITE events.
    return false;
  }

  if (r > 0 && s->peer) {
    data.resize(r);

    // s->peer->enqueue() may call s->close() and free s,
    // so save variables for debug printing below.
    unsigned saved_id = s->id;
    // int saved_fd = s->fd;

    if (s->available_send_bytes) {
      *s->available_send_bytes -= data.size();
    }

    static uint32_t cnt = 0;
    static size_t total = 0;
    total += data.size();
    FX_LOGS(DEBUG) << "[" << cnt++ << "]LS(" << saved_id << "): Send " << data.size() << "total "
                   << total;
    r = s->peer->enqueue(s->peer, std::move(data));
    FX_LOGS(DEBUG) << "LS(" << saved_id
                   << "): " /* fd=" << saved_fd*/ << "post peer->enqueue(). r=" << r;

    if (s->available_send_bytes) {
      if (*s->available_send_bytes <= 0) {
        // zxlogf(INFO, "LS(%u): send buffer full (%" PRId64 ")", saved_id,
        // *s->available_send_bytes); fdevent_del(s->fde, FDE_READ);
      }
    } else {
      FX_LOGS(DEBUG) << "LS(" << saved_id << "): acks not deferred, blocking";
      // fdevent_del(s->fde, FDE_READ);
    }
  }

  // Don't allow a forced eof if data is still there.
  if (is_eof) {
    FX_LOGS(DEBUG) << " closing because is_eof=" << is_eof << " r=" << r;
    s->close(s);
    return false;
  }

  return true;
}

static int local_socket_enqueue(asocket* s, apacket::payload_type data) {
  FX_LOGS(DEBUG) << "LS(" << s->id << "): enqueue " << data.size();

  // replace carriage return with new line
  if (s->newline_replace) {
    std::replace(data.data(), data.data() + data.size(), '\r', '\n');
  }
  // std::regex_replace(data.data(), std::regex("\r"), "\r\n");
  auto end = s->packet_queue.end();
  FX_LOGS(DEBUG) << "packet size before " << s->packet_queue.size();
  s->packet_queue.insert(end, data.data(), data.data() + data.size());
  FX_LOGS(DEBUG) << "packet size after " << s->packet_queue.size();

  switch (local_socket_flush_incoming(s)) {
    case SocketFlushResult::Destroyed:
      return -1;

    case SocketFlushResult::TryAgain:
      return 1;

    case SocketFlushResult::Completed:
      return 0;
  }

  return !s->packet_queue.empty();
}

static void local_socket_ready(asocket* s) {
  /* far side is ready for data, pay attention to
     readable events */
  FX_LOGS(DEBUG) << "This part is not implemented";
  // fdevent_add(s->fde, FDE_READ);
}

struct ClosingSocket {
  std::chrono::steady_clock::time_point begin;
};

#if 0
// The standard (RFC 1122 - 4.2.2.13) says that if we call close on a
// socket while we have pending data, a TCP RST should be sent to the
// other end to notify it that we didn't read all of its data. However,
// this can result in data that we've successfully written out to be dropped
// on the other end. To avoid this, instead of immediately closing a
// socket, call shutdown on it instead, and then read from the file
// descriptor until we hit EOF or an error before closing.
static void deferred_close(/*unique_fd fd*/) {
  // Shutdown the socket in the outgoing direction only, so that
  // we don't have the same problem on the opposite end.
  FX_LOGS(ERROR) << "This part is not implemented";
    adb_shutdown(fd.get(), SHUT_WR);
    auto callback = [](fdevent* fde, unsigned event, void* arg) {
        auto socket_info = static_cast<ClosingSocket*>(arg);
        if (event & FDE_READ) {
            ssize_t rc;
            char buf[BUFSIZ];
            while ((rc = adb_read(fde->fd.get(), buf, sizeof(buf))) > 0) {
                continue;
            }

            if (rc == -1 && errno == EAGAIN) {
                // There's potentially more data to read.
                auto duration = std::chrono::steady_clock::now() - socket_info->begin;
                if (duration > 1s) {
                    FX_LOGS(DEBUG) << "timeout expired while flushing socket, closing";
                } else {
                    return;
                }
            }
        } else if (event & FDE_TIMEOUT) {
            FX_LOGS(DEBUG) << "timeout expired while flushing socket, closing";
        }

        // Either there was an error, we hit the end of the socket, or our timeout expired.
        fdevent_destroy(fde);
        delete socket_info;
    };

    ClosingSocket* socket_info = new ClosingSocket{
            .begin = std::chrono::steady_clock::now(),
    };

    fdevent* fde = fdevent_create(fd.release(), callback, socket_info);
    fdevent_add(fde, FDE_READ);
    fdevent_set_timeout(fde, 1s);
}
#endif

// be sure to hold the socket list lock when calling this
static void local_socket_destroy(asocket* s) {
  int exit_on_close = s->exit_on_close;

  FX_LOGS(DEBUG) << "LS(" << s->id << "): destroying fde.";  // fd=" << s->fd;
  // deferred_close(fdevent_release(s->fde));

  remove_socket(s);
  delete s;

  if (exit_on_close) {
    FX_LOGS(DEBUG) << "local_socket_destroy: exiting";
    exit(1);
  }
}

static void local_socket_close(asocket* s) {
  FX_LOGS(DEBUG) << "entered local_socket_close. LS(" << s->id << ")";  // fd=" << s->fd;
  std::lock_guard<std::recursive_mutex> lock(local_socket_list_lock);
  if (s->peer) {
    FX_LOGS(DEBUG) << "LS(" << s->id << "): closing peer. peer->id=" << s->peer->id;
    // << " peer->fd=" << s->peer->fd;
    /* Note: it's important to call shutdown before disconnecting from
     * the peer, this ensures that remote sockets can still get the id
     * of the local socket they're connected to, to send a CLOSE()
     * protocol event. */
    if (s->peer->shutdown) {
      s->peer->shutdown(s->peer);
    }
    s->peer->peer = nullptr;
    s->peer->close(s->peer);
    s->peer = nullptr;
  }

  /* If we are already closing, or if there are no
  ** pending packets, destroy immediately
  */
  if (s->closing || s->has_write_error || s->packet_queue.empty()) {
    int id = s->id;
    s->closing = 1;
    bool destroy_outgoing = true;
    // TODO: This is a temporary solution. There will be a race when the driver shutsdown
    // if the thread does not get to execute.
    if (s->destroy_outgoing.compare_exchange_strong(
            destroy_outgoing, false, std::memory_order_release, std::memory_order_relaxed)) {
      std::thread([s, id]() {
        if (s->outgoing_thrd.joinable()) {
          s->outgoing_thrd.join();
        }
        release_service_socket(s);
        local_socket_destroy(s);
        FX_LOGS(DEBUG) << "LS(" << id << "): closed";
      }).detach();
    }
    return;
  }

  /* otherwise, put on the closing list
   */
  FX_LOGS(DEBUG) << "LS(" << s->id << "): closing";
  s->closing = 1;
  if (std::this_thread::get_id() != s->outgoing_thrd.get_id()) {
    s->outgoing_thrd.join();
  }
  release_service_socket(s);
  // fdevent_del(s->fde, FDE_READ);
  remove_socket(s);
  FX_LOGS(DEBUG) << "LS(" << s->id << "): put on socket_closing_list";  // fd=" << s->fd;
  local_socket_closing_list.push_back(s);
  // ZX_ASSERT(FDE_WRITE == s->fde->state & FDE_WRITE);
}

#if 0
static void local_socket_event_func(int fd, unsigned ev, void* _s) {
    asocket* s = reinterpret_cast<asocket*>(_s);
    FX_LOGS(DEBUG) << "LS(" << s->id << "): event_func(fd=" << s->fd<< "(=="  << fd<< "), ev=" << ev<< ")";

    /* put the FDE_WRITE processing before the FDE_READ
    ** in order to simplify the code.
    */
    FX_LOGS(ERROR) <<"This part is not implemented";
#if 0
    if (ev & FDE_WRITE) {
        switch (local_socket_flush_incoming(s)) {
            case SocketFlushResult::Destroyed:
                return;

            case SocketFlushResult::TryAgain:
                break;

            case SocketFlushResult::Completed:
                break;
        }
    }

    if (ev & FDE_READ) {
        if (!local_socket_flush_outgoing(s)) {
            return;
        }
    }

    if (ev & FDE_ERROR) {
        /* this should be caught be the next read or write
        ** catching it here means we may skip the last few
        ** bytes of readable data.
        */
        FX_LOGS(DEBUG) << "LS(" << s->id<< "): FDE_ERROR (fd=" <<s->fd << ")";
        return;
    }
#endif
}
#endif

void local_socket_ack(asocket* s, std::optional<int32_t> acked_bytes) {
  // acked_bytes can be negative!
  //
  // In the future, we can use this to preemptively supply backpressure, instead
  // of waiting for the writer to hit its limit.
  if (s->available_send_bytes.has_value() != acked_bytes.has_value()) {
    FX_LOGS(ERROR) << "delayed ack mismatch: socket = " << s->available_send_bytes.has_value()
                   << ", payload = " << acked_bytes.has_value();
    return;
  }

  if (s->available_send_bytes.has_value()) {
    // zxlogf(INFO, "LS(%d) received delayed ack, available bytes: %" PRId64 " += %" PRIu32, s->id,
    //        *s->available_send_bytes, *acked_bytes);

    // This can't (reasonably) overflow: available_send_bytes is 64-bit.
    *s->available_send_bytes += *acked_bytes;
    if (*s->available_send_bytes > 0) {
      s->ready(s);
    }
  } else {
    FX_LOGS(DEBUG) << "LS(" << s->id << ") received ack";
    s->ready(s);
  }
}

void local_outgoing_thread(asocket* s) {
  FX_LOGS(DEBUG) << "LS(" << s->id << ") Starting outgoing thread";
  while (true) {
    if (s->closing) {
      FX_LOGS(DEBUG) << "LS(" << s->id << ") Closing thread";
      break;
    }
    if (!local_socket_flush_outgoing(s)) {
      break;
    }
  }
  FX_LOGS(DEBUG) << "LS(" << s->id << ") Exiting outgoing thread";
}

asocket* create_local_socket(/*unique_fd ufd*/) {
  // int fd = 0;  // ufd.release();
  asocket* s = new asocket();
  // s->fd = fd;
  s->enqueue = local_socket_enqueue;
  s->ready = local_socket_ready;
  s->shutdown = nullptr;
  s->close = local_socket_close;
  install_local_socket(s);
  // s->fde = fdevent_create(fd, local_socket_event_func, s);
  FX_LOGS(DEBUG) << "LS(" << s->id << "): created";  // (fd=" << s->fd << ")";
  return s;
}

void release_service_socket(asocket* s) {
  if (s->zx_socket.is_valid()) {
    FX_LOGS(DEBUG) << "Ffx socket returned";
    __UNUSED auto socket = s->zx_socket.release();
  }
}

bool newline_replace(std::string_view name) {
  if (name == "shell:") {
    return true;
  }
  return false;
}

zx_status_t daemon_service_connect(std::string_view name, void* adb_ctxt, asocket* s) {
  std::string_view service_name = "UNKNOWN";
  std::string args = "";
  if (name.rfind("shell:", 0) == 0) {
    service_name = kShellService;
    args = name.substr(strlen("shell:"));
    if (!args.empty()) {
      FX_LOGS(DEBUG) << "Requesting shell cmd " << std::string(args).c_str() << "[" << args.size()
                     << "]";
    }
  } else if (name == "local:ffx") {
    service_name = kFfxService;
  } else {
    FX_LOGS(ERROR) << "Service " << name << " not supported";
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto adb = static_cast<adb::AdbBase*>(adb_ctxt);
  auto socket = adb->GetServiceSocket(service_name, args);
  if (socket.is_error()) {
    FX_LOGS(ERROR) << "Could not get socket " << socket.error_value();
    return socket.error_value();
  }
  if (!socket->is_valid()) {
    FX_LOGS(ERROR) << "Socket is invalid";
    return ZX_ERR_NOT_FOUND;
  }
  FX_LOGS(DEBUG) << "Socket is valid and moved";
  s->zx_socket = std::move(socket.value());
  return ZX_OK;
}

asocket* create_local_service_socket(std::string_view name, atransport* transport) {
  // int fd_value = 0; // fd.get();
  asocket* s = create_local_socket();  // std::move(fd));
  s->transport = transport;
  s->adb = static_cast<adb::AdbBase*>(transport->connection()->adb());
  s->name = name;

  zx_status_t status = daemon_service_connect(name, transport->connection()->adb(), s);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "No connection for the requested service " << std::string(name).c_str()
                   << ", status: " << status << ". Returning error";
    s->close(s);
    return nullptr;
  }

  s->newline_replace = newline_replace(name);
  FX_LOGS(DEBUG) << "LS(" << s->id << "): bound to "
                 << std::string(name).c_str();  // << " via " << fd_value;

  if (((name.rfind("root:", 0) == 0) && getuid() != 0) ||
      ((name.rfind("unroot:", 0) == 0) && getuid() == 0) || (name.rfind("usb:", 0) == 0) ||
      (name.rfind("tcpip:", 0) == 0)) {
    FX_LOGS(DEBUG) << "LS(" << s->id << "): enabling exit_on_close";
    s->exit_on_close = 1;
  }

  s->outgoing_thrd = std::thread([s]() { local_outgoing_thread(s); });
  return s;
}

static int remote_socket_enqueue(asocket* s, apacket::payload_type data) {
  FX_LOGS(DEBUG) << "entered remote_socket_enqueue RS(" << s->id
                 << ") WRITE";  // fd=" << s->fd
                                //  << " peer.fd=" << s->peer->fd;
  apacket* p = get_apacket();

  p->msg.command = A_WRTE;
  p->msg.arg0 = s->peer->id;
  p->msg.arg1 = s->id;

  if (data.size() > MAX_PAYLOAD) {
    put_apacket(p);
    return -1;
  }

  if (false /*s->peer->newline_replace*/) {
    auto y = std::string(data.data(), data.size());
    auto x = std::regex_replace(y, std::regex("\n"), "\r\n");
    if (x.size() != data.size()) {
      apacket::payload_type new_data;
      new_data.resize(x.size());
      memcpy(new_data.data(), x.data(), x.size());
      data = std::move(new_data);
    }
  }

  p->payload = std::move(data);
  p->msg.data_length = static_cast<uint32_t>(p->payload.size());
  static uint32_t cnt = 0;
  FX_LOGS(DEBUG) << "[" << cnt++ << "]RS(" << s->id << ") Send " << p->msg.data_length;
  send_packet(p, s->transport);
  return 1;
}

static void remote_socket_ready(asocket* s) {
  FX_LOGS(DEBUG) << "entered remote_socket_ready RS(" << s->id
                 << ") OKAY";  // fd=" << s->fd << " peer.fd=" << s->peer->fd;
  apacket* p = get_apacket();
  p->msg.command = A_OKAY;
  p->msg.arg0 = s->peer->id;
  p->msg.arg1 = s->id;
  send_packet(p, s->transport);
}

static void remote_socket_shutdown(asocket* s) {
  FX_LOGS(DEBUG) << "entered remote_socket_shutdown RS(" << s->id
                 << ") CLOSE";  // fd=" << s->fd << " peer->fd=" << (s->peer ? s->peer->fd : -1);
  apacket* p = get_apacket();
  p->msg.command = A_CLSE;
  if (s->peer) {
    p->msg.arg0 = s->peer->id;
  }
  p->msg.arg1 = s->id;
  send_packet(p, s->transport);
}

static void remote_socket_close(asocket* s) {
  if (s->peer) {
    s->peer->peer = nullptr;
    FX_LOGS(DEBUG) << "RS(" << s->id << ") peer->close()ing peer->id="
                   << s->peer->id;  // << " peer->fd=" << s->peer->fd;
    s->peer->close(s->peer);
  }
  FX_LOGS(DEBUG) << "entered remote_socket_close RS(" << s->id
                 << ") CLOSE";  // fd=" << s->fd << " peer->fd=" << (s->peer ? s->peer->fd : -1);
  FX_LOGS(DEBUG) << "RS(" << s->id << "): closed";
  delete s;
}

// Create a remote socket to exchange packets with a remote service through transport
// |t|. Where |id| is the socket id of the corresponding service on the other
//  side of the transport (it is allocated by the remote side and _cannot_ be 0).
// Returns a new non-NULL asocket handle.
asocket* create_remote_socket(unsigned id, atransport* t) {
  if (id == 0) {
    FX_LOGS(ERROR) << "invalid remote socket id (0)";
  }
  asocket* s = new asocket();
  s->id = id;
  s->enqueue = remote_socket_enqueue;
  s->ready = remote_socket_ready;
  s->shutdown = remote_socket_shutdown;
  s->close = remote_socket_close;
  s->transport = t;

  FX_LOGS(DEBUG) << "RS(" << s->id << "): created";
  return s;
}

void connect_to_remote(asocket* s, std::string_view destination) {
  FX_LOGS(DEBUG) << "Connect_to_remote call RS(" << s->id << ")";  // fd=" << s->fd;
  apacket* p = get_apacket();

  FX_LOGS(DEBUG) << "LS(" << s->id << ": connect(" << std::string(destination).c_str() << ")";
  p->msg.command = A_OPEN;
  p->msg.arg0 = s->id;

  if (s->transport->SupportsDelayedAck()) {
    p->msg.arg1 = INITIAL_DELAYED_ACK_BYTES;
    s->available_send_bytes = 0;
  }

  // adbd used to expect a null-terminated string.
  // Keep doing so to maintain backward compatibility.
  p->payload.resize(destination.size() + 1);
  memcpy(p->payload.data(), destination.data(), destination.size());
  p->payload[destination.size()] = '\0';
  p->msg.data_length = static_cast<uint32_t>(p->payload.size());

  ZX_ASSERT(p->msg.data_length <= s->get_max_payload());

  send_packet(p, s->transport);
}

size_t asocket::get_max_payload() const {
  size_t max_payload = MAX_PAYLOAD;
  if (transport) {
    max_payload = std::min(max_payload, transport->get_max_payload());
  }
  if (peer && peer->transport) {
    max_payload = std::min(max_payload, peer->transport->get_max_payload());
  }
  return max_payload;
}
