// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>

#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/socket.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "apps/netstack/dispatcher.h"
#include "apps/netstack/events.h"
#include "apps/netstack/handle_watcher.h"
#include "apps/netstack/iostate.h"
#include "apps/netstack/multiplexer.h"
#include "apps/netstack/net_socket.h"
#include "apps/netstack/request_queue.h"
#include "apps/netstack/socket_functions.h"
#include "apps/netstack/trace.h"

void handle_close(iostate_t* ios, mx_signals_t signals) {
  debug("handle_close\n");
  handle_request(request_pack(MXRIO_CLOSE, 0, NULL, ios), EVENT_NONE, signals);
}

static void schedule_sigconn(iostate_t* ios) {
  debug("schedule_sigconn\n");
  fd_event_set(ios->sockfd, EVENT_READ);
  wait_queue_put(WAIT_NET, ios->sockfd, request_pack(IO_SIGCONN, 0, NULL, ios));
}

static void schedule_rw(iostate_t* ios) {
  debug("schedule_rw\n");
  fd_event_set(ios->sockfd, EVENT_READ);
  wait_queue_put(WAIT_NET, ios->sockfd, request_pack(MXRIO_READ, 0, NULL, ios));

  socket_signals_set(ios, MX_SIGNAL_READABLE);
  wait_queue_put(WAIT_SOCKET, ios->sockfd,
                 request_pack(MXRIO_WRITE, 0, NULL, ios));
}

// error codes for a special cases
#define PENDING_NET (-99999)
#define PENDING_SOCKET (-99998)

#define RWBUF_SIZE (64 * 1024)

typedef struct rwbuf {
  union {
    struct rwbuf* next;
    char data[RWBUF_SIZE];
  };
} rwbuf_t;

static rwbuf_t* rwbuf_head = NULL;

rwbuf_t* get_rwbuf(void) {
  rwbuf_t* bufp = rwbuf_head;
  if (bufp == NULL) {
    bufp = (rwbuf_t*)malloc(sizeof(rwbuf_t));
  } else {
    rwbuf_head = bufp->next;
  }
  return bufp;
}

void put_rwbuf(rwbuf_t* bufp) {
  if (bufp == NULL) return;
  bufp->next = rwbuf_head;
  rwbuf_head = bufp;
}

static mx_status_t create_handles(iostate_t* ios, mx_handle_t* peer_h,
                                  mx_handle_t* peer_s) {
  mx_handle_t h[2];
  mx_status_t r;

  if ((r = mx_channel_create(0u, &h[0], &h[1])) < 0) goto fail_channel_create;

  mx_handle_t s[2];
  if ((r = mx_socket_create(0u, &s[0], &s[1])) < 0) goto fail_socket_create;

  ios->s = s[0];

  // The dispatcher will own and close the handle if the other end
  // is closed (it also disconnects the handler automatically)
  if ((r = dispatcher_add(h[0], ios)) < 0) goto fail_watcher_add;

  // increment the refcount for ios->s
  iostate_acquire(ios);

  *peer_h = h[1];
  *peer_s = s[1];
  return NO_ERROR;

fail_watcher_add:
  ios->s = 0;
  iostate_release(ios);
  mx_handle_close(s[0]);
  mx_handle_close(s[1]);

fail_socket_create:
  mx_handle_close(h[0]);
  mx_handle_close(h[1]);

fail_channel_create:
  return r;
}

enum {
  HANDLE_TYPE_NONE,
  HANDLE_TYPE_STREAM,
  HANDLE_TYPE_DGRAM,
};

static mx_status_t errno_to_status(int errno_) {
  switch (errno_) {
    case ENOMEM:
      return ERR_NO_MEMORY;
    case ENOBUFS:
      return ERR_NO_RESOURCES;
    case EWOULDBLOCK:
      return ERR_SHOULD_WAIT;
    case EBADF:
      return ERR_BAD_HANDLE;
    case EACCES:
      return ERR_ACCESS_DENIED;
    case EINVAL:
      return ERR_INVALID_ARGS;
    case EIO:
      return ERR_IO;
    default:
      return ERR_IO;  // TODO: map more errno
  }
}

static mx_status_t parse_socket_args(const char* path, int* domain, int* type,
                                     int* protocol) {
  char* ptr;
  errno = 0;  // strtol doesn't set errno to 0
  *domain = strtol(path, &ptr, 10);
  if (errno != 0) return ERR_INVALID_ARGS;
  debug("domain=%d\n", *domain);
  if (*ptr++ != '/') return ERR_INVALID_ARGS;
  *type = strtol(ptr, &ptr, 10);
  if (errno != 0) return ERR_INVALID_ARGS;
  debug("type=%d\n", *type);
  if (*ptr++ != '/') return ERR_INVALID_ARGS;
  *protocol = strtol(ptr, &ptr, 10);
  if (errno != 0) return ERR_INVALID_ARGS;
  debug("protocol=%d\n", *protocol);
  if (*ptr != '\0') return ERR_INVALID_ARGS;
  return NO_ERROR;
}

static const char* match_subdir(const char* path, const char* name,
                                int namelen) {
  if (strncmp(path, name, namelen) == 0) {
    if (path[namelen] == '\0') return path + namelen;  // return ptr to '\0'
    if (path[namelen] == '/')
      return path + namelen + 1;  // return ptr to the ch after /
  }
  return NULL;
}

// use this if the name is a string literal
#define MATCH_SUBDIR(path, name) match_subdir(path, name, sizeof(name) - 1)

mx_status_t do_none(mxrio_msg_t* msg, iostate_t* ios, int events,
                    mx_signals_t signals);
mx_status_t do_socket(mxrio_msg_t* msg, iostate_t* ios, int events,
                      mx_signals_t signals);
mx_status_t do_accept(mxrio_msg_t* msg, iostate_t* ios, int events,
                      mx_signals_t signals);

mx_status_t do_open(mxrio_msg_t* msg, iostate_t* ios, int events,
                    mx_signals_t signals) {
  debug("do_open: msg->datalen=%d\n", msg->datalen);

  char* path = (char*)msg->data;
  if ((msg->datalen < 1) || (msg->datalen > 1024)) {
    return ERR_INVALID_ARGS;
  }
  path[msg->datalen] = '\0';
  debug("do_open: path \"%s\"\n", path);

  if (MATCH_SUBDIR(path, MXRIO_SOCKET_DIR_NONE)) {
    return do_none(msg, ios, events, MX_SIGNAL_NONE);
  } else if (MATCH_SUBDIR(path, MXRIO_SOCKET_DIR_SOCKET)) {
    return do_socket(msg, ios, events, MX_SIGNAL_NONE);
  } else if (MATCH_SUBDIR(path, MXRIO_SOCKET_DIR_ACCEPT)) {
    return do_accept(msg, ios, events, MX_SIGNAL_NONE);
  } else {
    error("invalid path: %s\n", path);
    return ERR_INVALID_ARGS;
  }
}

mx_status_t do_none(mxrio_msg_t* msg, iostate_t* ios, int events,
                    mx_signals_t signals) {
  ios = iostate_alloc();  // override

  // TODO: we don't need socket
  mx_handle_t peer_h, peer_s;
  // ios->s is set inside create_handles()
  mx_status_t r = create_handles(ios, &peer_h, &peer_s);
  if (r >= 0) {
    msg->handle[0] = peer_h;
    msg->handle[1] = peer_s;
    msg->arg2.protocol = MXIO_PROTOCOL_SOCKET;
    msg->hcount = 2;
  } else {
    iostate_release(ios);
    return r;
  }

  msg->datalen = 0;
  return NO_ERROR;
}

mx_status_t do_socket(mxrio_msg_t* msg, iostate_t* ios, int events,
                      mx_signals_t signals) {
  const char* ptr = MATCH_SUBDIR((char*)msg->data, MXRIO_SOCKET_DIR_SOCKET);
  if (ptr == NULL) return ERR_INVALID_ARGS;

  int domain, type, protocol;
  if (parse_socket_args(ptr, &domain, &type, &protocol) < 0)
    return ERR_INVALID_ARGS;

  int handle_type;
  if (type == SOCK_STREAM) {
    handle_type = HANDLE_TYPE_STREAM;
  } else if (type == SOCK_DGRAM) {
    handle_type = HANDLE_TYPE_DGRAM;
    return ERR_NOT_SUPPORTED;  // TODO: support UDP
  } else {
    return ERR_NOT_SUPPORTED;
  }

  ios = iostate_alloc();  // override

  ios->sockfd = net_socket(domain, type, protocol);
  int errno_ = (ios->sockfd < 0) ? errno : 0;
  debug_net("net_socket => %d (errno=%d)\n", ios->sockfd, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }
  debug("do_open: new sockfd=%d\n", ios->sockfd);

  int non_blocking = 1;
  int ret = net_ioctl(ios->sockfd, FIONBIO, &non_blocking);
  debug_net("net_ioctl(FIONBIO) => %d (errno=%d)\n", ret, errno_);
  errno_ = (ret < 0) ? errno : 0;
  if (errno_ != 0) {
    iostate_release(ios);
    return errno_to_status(errno_);
  }

  // TODO: change the handle type according to HANDLE_TYPE

  mx_handle_t peer_h, peer_s;
  // ios->s is set inside create_handles()
  mx_status_t r = create_handles(ios, &peer_h, &peer_s);
  if (r >= 0) {
    msg->handle[0] = peer_h;
    msg->handle[1] = peer_s;
    msg->arg2.protocol = MXIO_PROTOCOL_SOCKET;
    msg->hcount = 2;
  } else {
    iostate_release(ios);
    return r;
  }
  msg->datalen = 0;

  fd_event_set(ios->sockfd, EVENT_EXCEPT);
  socket_signals_set(ios, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNALED);

  return NO_ERROR;
}

mx_status_t do_close(mxrio_msg_t* msg, iostate_t* ios, int events,
                     mx_signals_t signals) {
  if (ios->sockfd >= 0) {
    debug_net("net_close\n");
    net_close(ios->sockfd);
    // TODO: send the errno to the client
    fd_event_clear(ios->sockfd, EVENT_ALL);
    debug_net("wait_queue_discard(NET) (sockfd=%d)\n", ios->sockfd);
    wait_queue_discard(WAIT_NET, ios->sockfd);
    debug_socket("wait_queue_discard(SOCKET) (sockfd=%d)\n", ios->sockfd);
    wait_queue_discard(WAIT_SOCKET, ios->sockfd);
    info("sockfd %d closed (ios=%p)\n", ios->sockfd, ios);
    ios->sockfd = -1;
  }
  iostate_release(ios);
  return NO_ERROR;
}

mx_status_t do_connect(mxrio_msg_t* msg, iostate_t* ios, int events,
                       mx_signals_t signals) {
  int errno_ = 0;
  if (events == EVENT_NONE) {
    int ret = net_connect(ios->sockfd, (struct sockaddr*)msg->data,
                          (socklen_t)msg->datalen);
    errno_ = (ret < 0) ? errno : 0;
    debug_net("net_connect => %d (errno=%d)\n", ret, errno_);
    if (errno_ == EINPROGRESS) {
      debug("connect pending\n");
      // when the connection is done, it'll be writable
      fd_event_set(ios->sockfd, EVENT_WRITE);
      return PENDING_NET;
    }
  } else {
    debug("connect resumed\n");
    int val;
    socklen_t vallen = sizeof(val);
    int ret = net_getsockopt(ios->sockfd, SOL_SOCKET, SO_ERROR, &val, &vallen);
    errno_ = (ret < 0) ? errno : 0;
    debug_net("net_getsockopt => %d (errno=%d)\n", ret, errno_);
    if (errno_ != 0) {
      return errno_to_status(errno_);
    }
    debug_net("val(errno)=%d, vallen=%d\n", val, vallen);
    errno_ = val;
  }
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }
  schedule_rw(ios);

  msg->arg2.off = 0;
  msg->datalen = 0;
  return NO_ERROR;
}

mx_status_t do_bind(mxrio_msg_t* msg, iostate_t* ios, int events,
                    mx_signals_t signals) {
  int ret = net_bind(ios->sockfd, (struct sockaddr*)msg->data,
                     (socklen_t)msg->datalen);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_bind => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }
  msg->datalen = 0;
  msg->arg2.off = 0;
  return NO_ERROR;
}

mx_status_t do_listen(mxrio_msg_t* msg, iostate_t* ios, int events,
                      mx_signals_t signals) {
  int backlog = *(int*)msg->data;
  debug("do_listen: backlog=%zd\n", backlog);

  int ret = net_listen(ios->sockfd, backlog);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_listen => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }
  schedule_sigconn(ios);
  msg->datalen = 0;
  msg->arg2.off = 0;
  return NO_ERROR;
}

mx_status_t do_sigconn(mxrio_msg_t* msg, iostate_t* ios, int events,
                       mx_signals_t signals) {
  debug_net("do_sigconn: events=0x%x\n", events);
  mx_status_t r = mx_object_signal(ios->s, 0u, MX_SIGNAL_SIGNAL0);
  debug_always("mx_object_signal(set) => %d\n", r);
  return NO_ERROR;
}

mx_status_t do_accept(mxrio_msg_t* msg, iostate_t* ios, int events,
                      mx_signals_t signals) {
  // we don't return the connected addr at this point.
  // the client will call getpeername later.
  int ret = net_accept(ios->sockfd, NULL, NULL);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_accept => %d (errno=%d)\n", ret, errno_);
  if (errno_ == EWOULDBLOCK) {
    vdebug("accept pending\n");
    fd_event_set(ios->sockfd, EVENT_READ);
    return PENDING_NET;
  } else if (errno_ != 0) {
    return errno_to_status(errno_);
  }

  mx_status_t r = mx_object_signal(ios->s, MX_SIGNAL_SIGNAL0, 0u);
  debug_always("mx_object_signal(clear) => %d\n", r);
  schedule_sigconn(ios);

  // TODO: share this code with socket()
  iostate_t* ios_new = iostate_alloc();
  ios_new->sockfd = ret;

  int non_blocking = 1;
  ret = net_ioctl(ios_new->sockfd, FIONBIO, &non_blocking);
  errno_ = (ret < 0) ? errno : 0;
  debug_net("net_ioctl(FIONBIO) => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    iostate_release(ios_new);
    return errno_to_status(errno_);
  }

  mx_handle_t peer_h, peer_s;
  r = create_handles(ios_new, &peer_h, &peer_s);
  if (r < 0) {
    iostate_release(ios_new);
    return r;
  } else {
    msg->handle[0] = peer_h;
    msg->handle[1] = peer_s;
    msg->arg2.protocol = MXIO_PROTOCOL_SOCKET;
    msg->hcount = 2;
  }
  msg->datalen = 0;

  fd_event_set(ios_new->sockfd, EVENT_EXCEPT);
  socket_signals_set(ios_new, MX_SIGNAL_PEER_CLOSED | MX_SIGNAL_SIGNALED);

  schedule_rw(ios_new);
  return NO_ERROR;
}

mx_status_t do_read(mxrio_msg_t* msg, iostate_t* ios, int events,
                    mx_signals_t signals) {
  debug_rw("do_read: rlen=%d net=%d socket=%d events=0x%x signals=0x%x\n",
           ios->rlen, ios->read_net_read, ios->read_socket_write, events,
           signals);

  if (ios->rlen <= 0) {
    if (ios->rbuf == NULL) {
      ios->rbuf = get_rwbuf();
      debug_alloc("do_read: get rbuf %p\n", ios->rbuf);
      assert(ios->rbuf);
    }
    int n = net_read(ios->sockfd, ios->rbuf->data, RWBUF_SIZE);
    int errno_ = (n < 0) ? errno : 0;
    debug_net("net_read => %d (errno=%d)\n", n, errno_);
    if (n == 0) {
    connection_closed:
      // connection is closed
      info("do_read: net_read: connection closed\n");
      mx_status_t r = mx_socket_write(ios->s, MX_SOCKET_HALF_CLOSE, NULL, 0u,
                                      NULL);
      if (r < 0) {
        error("do_read: MX_SOCKET_HALF_CLOSE failed\n");
        return r;
      }
      info("half_close(ios->s 0x%x) => %d (ios=%p)\n", ios->s, r, ios);
      return NO_ERROR;
    } else if (errno_ == EWOULDBLOCK) {
      debug("read would block\n");
      fd_event_set(ios->sockfd, EVENT_READ);
      return PENDING_NET;
    } else if (errno_ != 0) {
      // TODO: send the error to the client
      error("do_read: net_read failed (errno=%d)\n", errno_);
      goto connection_closed;
    }
    ios->rlen = n;
    ios->roff = 0;
    ios->read_net_read += ios->rlen;
  }

  while (ios->roff < ios->rlen) {
    mx_size_t nwritten;
    mx_status_t r = mx_socket_write(ios->s, 0u, ios->rbuf->data + ios->roff,
                                    ios->rlen - ios->roff, &nwritten);
    debug_socket("mx_socket_write(%p, %d) => %lu\n",
                 ios->rbuf->data + ios->roff, ios->rlen - ios->roff, nwritten);
    if (r < 0) {
      if (r == ERR_SHOULD_WAIT) {
        socket_signals_set(ios, MX_SIGNAL_WRITABLE);
        return PENDING_SOCKET;
      }
      error("do_read: mx_socket_write failed (%d)\n", r);
      // TODO: send the error to the client
      return r;
    }
    ios->roff += nwritten;
    ios->read_socket_write += nwritten;
  }
  ios->rlen = 0;
  ios->roff = 0;
  fd_event_set(ios->sockfd, EVENT_READ);
  return PENDING_NET;  // schedule next read
}

mx_status_t do_write(mxrio_msg_t* msg, iostate_t* ios, int events,
                     mx_signals_t signals) {
  debug_rw("do_write: wlen=%d socket=%d net=%d events=0x%x signals=0x%x\n",
           ios->wlen, ios->write_socket_read, ios->write_net_write, events,
           signals);

  if (ios->wlen <= 0) {
    if (ios->wbuf == NULL) {
      ios->wbuf = get_rwbuf();
      debug_alloc("do_write: get wbuf %p\n", ios->wbuf);
      assert(ios->wbuf);
    }
    mx_size_t nread;
    mx_status_t r = mx_socket_read(ios->s, 0u, ios->wbuf->data, RWBUF_SIZE,
                                   &nread);
    debug_socket("mx_socket_read => %d (%lu)\n", r, nread);
    if (r == ERR_SHOULD_WAIT) {
      if (signals & MX_SIGNAL_PEER_CLOSED) {
        debug_socket("do_write: handle_close (socket is closed)\n");
        handle_close(ios, signals);
        return NO_ERROR;
      }
      socket_signals_set(ios, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED);
      return PENDING_SOCKET;
    } else if (r == ERR_REMOTE_CLOSED) {
      handle_close(ios, signals);
      return NO_ERROR;
    } else if (r < 0) {
      error("do_write: mx_socket_read failed (%d)\n", r);
      // half-close the socket to notify the error
      // TODO: use user signal
      mx_status_t r = mx_socket_write(ios->s, MX_SOCKET_HALF_CLOSE, NULL, 0u,
                                      NULL);
      info("mx_socket_write(half_close) => %d\n", r);
      return r;
    }
    ios->wlen = nread;
    ios->woff = 0;
    ios->write_socket_read += ios->wlen;
  }

  while (ios->woff < ios->wlen) {
    int n = net_write(ios->sockfd, ios->wbuf->data + ios->woff,
                      ios->wlen - ios->woff);
    int errno_ = (n < 0) ? errno : 0;
    debug_net("net_write => %d (errno=%d)\n", n, errno_);
    if (errno_ == EWOULDBLOCK) {
      fd_event_set(ios->sockfd, EVENT_WRITE);
      return PENDING_NET;
    } else if (errno_ != 0) {
      // TODO: send the error to the client
      error("do_write: net_write failed (errno=%d)\n", errno_);
      return NO_ERROR;
    }
    ios->woff += n;
    ios->write_net_write += n;
  }
  ios->wlen = 0;
  ios->woff = 0;

  socket_signals_set(ios, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED);
  return PENDING_SOCKET;
}

mx_status_t do_getaddrinfo(mxrio_msg_t* msg, iostate_t* ios, int events,
                           mx_signals_t signals) {
  ssize_t len = msg->datalen;
  vdebug("do_getaddrifo: len=%zd\n", len);

  mxrio_gai_req_t* gai_reqp = (mxrio_gai_req_t*)msg->data;

  const char* node = gai_reqp->node_is_null ? NULL : gai_reqp->node;
  const char* service = gai_reqp->service_is_null ? NULL : gai_reqp->service;
  struct addrinfo* hints = gai_reqp->hints_is_null ? NULL : &gai_reqp->hints;

  vdebug("do_gai: node=%s\n", node);
  vdebug("do_gai: service=%s\n", service);
  vdebug("do_gai: flags=0x%x, family=%d, socktype=%d, protocol=%d\n",
         hints->ai_flags, hints->ai_family, hints->ai_socktype,
         hints->ai_protocol);

  struct addrinfo* res;
  int ret = net_getaddrinfo(node, service, (struct addrinfo*)hints, &res);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_getaddrinfo() => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }

  static_assert(sizeof(mxrio_gai_reply_t) < MXIO_CHUNK_SIZE,
                "mxrio_gai_reply_t should fit into msg->data");
  mxrio_gai_reply_t* gai_replyp = (struct mxrio_gai_reply*)msg->data;

  // TODO: we are returning the first one only
  gai_replyp->nres = 1;
  memcpy(&gai_replyp->res[0].ai, res, sizeof(struct addrinfo));
  vdebug("do_gai: res[0]: family=%d, socktype=%d, protocol=%d\n",
         gai_replyp->res[0].ai.ai_family, gai_replyp->res[0].ai.ai_socktype,
         gai_replyp->res[0].ai.ai_protocol);

  gai_replyp->res[0].ai.ai_addr = NULL;  // don't pass the pointer
  memcpy(&gai_replyp->res[0].addr, res->ai_addr, res->ai_addrlen);
  gai_replyp->res[0].ai.ai_canonname = NULL;  // TODO
  gai_replyp->res[0].ai.ai_next = NULL;       // TODO

  net_freeaddrinfo(res);
  debug_net("net_freeaddrinfo\n");

  msg->datalen = sizeof(struct mxrio_gai_reply);
  msg->arg2.off = 0;
  return NO_ERROR;
}

mx_status_t do_getsockname(mxrio_msg_t* msg, iostate_t* ios, int events,
                           mx_signals_t signals) {
  struct mxrio_sockaddr_reply* reply = (struct mxrio_sockaddr_reply*)msg->data;
  reply->len = sizeof(reply->addr);
  int ret =
      net_getsockname(ios->sockfd, (struct sockaddr*)&reply->addr, &reply->len);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_getsockname => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }

  msg->arg2.off = 0;
  msg->datalen = sizeof(*reply);
  return NO_ERROR;
}

mx_status_t do_getpeername(mxrio_msg_t* msg, iostate_t* ios, int events,
                           mx_signals_t signals) {
  struct mxrio_sockaddr_reply* reply = (struct mxrio_sockaddr_reply*)msg->data;
  reply->len = sizeof(reply->addr);
  int ret =
      net_getpeername(ios->sockfd, (struct sockaddr*)&reply->addr, &reply->len);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_getpeername => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }

  msg->arg2.off = 0;
  msg->datalen = sizeof(*reply);
  return NO_ERROR;
}

mx_status_t do_getsockopt(mxrio_msg_t* msg, iostate_t* ios, int events,
                          mx_signals_t signals) {
  struct mxrio_sockopt_req_reply* req_reply =
      (struct mxrio_sockopt_req_reply*)msg->data;
  req_reply->optlen = sizeof(req_reply->optval);
  int ret = net_getsockopt(ios->sockfd, req_reply->level, req_reply->optname,
                           &req_reply->optval, &req_reply->optlen);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_getsockopt => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }

  msg->arg2.off = 0;
  msg->datalen = sizeof(*req_reply);
  return NO_ERROR;
}

mx_status_t do_setsockopt(mxrio_msg_t* msg, iostate_t* ios, int events,
                          mx_signals_t signals) {
  struct mxrio_sockopt_req_reply* req =
      (struct mxrio_sockopt_req_reply*)msg->data;
  int ret = net_setsockopt(ios->sockfd, req->level, req->optname, &req->optval,
                           req->optlen);
  int errno_ = (ret < 0) ? errno : 0;
  debug_net("net_setsockopt => %d (errno=%d)\n", ret, errno_);
  if (errno_ != 0) {
    return errno_to_status(errno_);
  }

  msg->arg2.off = 0;
  msg->datalen = 0;
  return NO_ERROR;
}

typedef mx_status_t (*do_func_t)(mxrio_msg_t*, iostate_t*, int, mx_signals_t);

static do_func_t do_funcs[] = {
        [MXRIO_OPEN] = do_open,
        [MXRIO_CONNECT] = do_connect,
        [MXRIO_BIND] = do_bind,
        [MXRIO_LISTEN] = do_listen,
        [MXRIO_GETADDRINFO] = do_getaddrinfo,
        [MXRIO_GETSOCKNAME] = do_getsockname,
        [MXRIO_GETPEERNAME] = do_getpeername,
        [MXRIO_GETSOCKOPT] = do_getsockopt,
        [MXRIO_SETSOCKOPT] = do_setsockopt,
        [MXRIO_WRITE] = do_write,
        [MXRIO_READ] = do_read,
        [MXRIO_CLOSE] = do_close,
        [IO_SIGCONN] = do_sigconn,
};

static bool is_message_valid(mxrio_msg_t* msg) {
  if ((msg->magic != MXRIO_MAGIC) || (msg->datalen > MXIO_CHUNK_SIZE) ||
      (msg->hcount > MXIO_MAX_HANDLES)) {
    return false;
  }
  return true;
}

static void discard_handles(mx_handle_t* handles, unsigned count) {
  while (count-- > 0) {
    mx_handle_close(*handles++);
  }
}

static void send_status(mxrio_msg_t* msg, mx_handle_t rh) {
  debug("send_status: msg->arg = %d\n", msg->arg);
  if ((msg->arg < 0) || !is_message_valid(msg)) {
    error("send_status: msg invalid\n");
    discard_handles(msg->handle, msg->hcount);
    msg->datalen = 0;
    msg->hcount = 0;
    msg->arg = (msg->arg < 0) ? msg->arg : ERR_INTERNAL;
  }

  // rh is always a reply pipe
  msg->handle[msg->hcount++] = rh;

  msg->op = MXRIO_STATUS;
  if (mx_channel_write(rh, 0u, msg, MXRIO_HDR_SZ + msg->datalen, msg->handle,
                       msg->hcount) < 0) {
    error("send_status: write failed\n");
    discard_handles(msg->handle, msg->hcount);
  }

  debug_alloc("send_status: free msg %p\n", msg);
  free(msg);
}

void handle_request(request_t* rq, int events, mx_signals_t signals) {
  int op;
  mx_handle_t rh;
  mxrio_msg_t* msg;
  iostate_t* ios;
  request_unpack(rq, &op, &rh, &msg, &ios);

  debug_alloc("handle_request: rq %p\n", rq);
  if (op >= NUM_OPS) {
    error("handle_request: unknown op (%d)\n", op);
    goto err;
  }
  debug_always("handle_request: op=%d(%s), ios=%p, sockfd=%d, events=0x%x\n",
               op, getopname(op), ios, ios ? ios->sockfd : -999, events);

  do_func_t func = do_funcs[op];
  if (func == NULL) {
    error("handle_request: no func is registered for op(%s)\n", getopname(op));
    goto err;
  }
  mx_status_t r = func(msg, ios, events, signals);
  if (r == PENDING_NET) {
    debug_net("pending on net: op=%d(%s)\n", op, getopname(op));
    wait_queue_put(WAIT_NET, ios->sockfd, rq);
    return;
  } else if (r == PENDING_SOCKET) {
    debug_socket("pending on socket: op=%d(%s)\n", op, getopname(op));
    wait_queue_put(WAIT_SOCKET, ios->sockfd, rq);
    return;
  } else {
    switch (op) {
      case MXRIO_READ:
      case MXRIO_WRITE:
      case MXRIO_CLOSE:
      case IO_SIGCONN:
        // Don't call send_status()
        break;
      default:
        msg->arg = r;
        send_status(msg, rh);  // this frees msg
        break;
    }
    debug_alloc("handle_request: request_free rq %p\n", rq);
    request_free(rq);
    return;
  }
err:
  if (op < MXRIO_NUM_OPS) {
    msg->arg = ERR_INVALID_ARGS;
    send_status(msg, rh);
  }
  debug_alloc("handle_request: request_free rq %p\n", rq);
  request_free(rq);
}
