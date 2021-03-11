// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INTERNAL_H_
#define LIB_FDIO_INTERNAL_H_

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/debuglog.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/types.h>

using fdio_ns_t = struct fdio_namespace;

// FDIO provides POSIX I/O functionality over various transports
// via the fdio_t interface abstraction.
//
// The "pipe" transport is a thin wrapper over Zircon sockets supporting
// vector read/write.
//
// The "socket_stream"/"socket_dgram" transports implement BSD sockets.
//
// The "remote" transport uses Zircon channels to implement POSIX files
// and directories.
//
// The "local" transport resolves and forwards open calls by looking up
// paths in a namespace.
//
// The "null" transport absorbs writes and is never readable.
//
// TODO(fxbug.dev/43267): Eventually, with the exception of the "local" and "null"
// transport, the different transports should become an implementation detail
// in zxio.

struct Errno {
  constexpr explicit Errno(int e) : e(e) {}
  static constexpr int Ok = 0;
  bool is_error() const { return e != 0; }
  int e;
};

// fdio_t ioflag values
#define IOFLAG_CLOEXEC (1 << 0)
#define IOFLAG_EPOLL (1 << 2)
#define IOFLAG_WAITABLE (1 << 3)

// Socket is connecting to the peer.
#define IOFLAG_SOCKET_CONNECTING (1 << 4)
// Socket is connected to the peer.
#define IOFLAG_SOCKET_CONNECTED (1 << 5)
// Socket is operating in non-blocking mode.
#define IOFLAG_NONBLOCK (1 << 6)
// Socket has an error signal asserted.
#define IOFLAG_SOCKET_HAS_ERROR (1 << 7)
// Socket is listening for new connections.
#define IOFLAG_SOCKET_LISTENING (1 << 8)

// The subset of fdio_t per-fd flags queryable via fcntl.
// Static assertions in unistd.cc ensure we aren't colliding.
#define IOFLAG_FD_FLAGS IOFLAG_CLOEXEC

// Waits until one or more |events| are signalled, or the |deadline| passes.
// The |events| are of the form |FDIO_EVT_*|, defined in io.h.
// If not NULL, |out_pending| returns a bitmap of all observed events.
zx_status_t fdio_wait(fdio_t* io, uint32_t events, zx::time deadline, uint32_t* out_pending);

// Wraps a channel with an fdio_t using remote io.
fdio_t* fdio_remote_create(fidl::ClientEnd<fuchsia_io::Node> node, zx::eventpair event);

// Creates an |fdio_t| from a remote directory connection.
fdio_t* fdio_dir_create(fidl::ClientEnd<fuchsia_io::Directory> dir);

// Creates an |fdio_t| from a remote file connection.
fdio_t* fdio_file_create(fidl::ClientEnd<fuchsia_io::File> file, zx::event event,
                         zx::stream stream);

// Creates an |fdio_t| from a remote PTY connection.
fdio_t* fdio_pty_create(fidl::ClientEnd<fuchsia_hardware_pty::Device> device, zx::eventpair event);

// Creates a pipe backed by a socket.
fdio_t* fdio_pipe_create(zx::socket socket);

// Creates an |fdio_t| from a VMO and a stream.
//
// The stream should be backed by the given VMO.
fdio_t* fdio_vmo_create(zx::vmo vmo, zx::stream stream);

// Creates an |fdio_t| for a VMO file.
//
// * |vmo| is the VMO that contains the contents of the file.
// * |offset| is the index of the first byte of the file in the VMO.
// * |length| is the number of bytes in the file.
// * |seek| is the initial seek offset within the file (i.e., relative to
//   |offset| within the underlying VMO).
fdio_t* fdio_vmofile_create(fidl::ClientEnd<fuchsia_io::File> file, zx::vmo vmo, zx_off_t offset,
                            zx_off_t length, zx_off_t seek);

fdio_t* fdio_datagram_socket_create(zx::eventpair event,
                                    fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client);

fdio_t* fdio_stream_socket_create(zx::socket socket,
                                  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client,
                                  zx_info_socket_t info);

// Creates a message port and pair of simple io fdio_t's
zx_status_t fdio_pipe_pair(fdio_t** a, fdio_t** b, uint32_t options);

// Creates an |fdio_t| referencing the root of the |ns| namespace.
fdio_t* fdio_ns_open_root(fdio_ns_t* ns);

// Change the root of the given namespace |ns| to match |io|.
//
// Does not take ownership of |io|. The caller is responsible for retaining a reference to |io|
// for the duration of this call and for releasing that reference after this function returns.
zx_status_t fdio_ns_set_root(fdio_ns_t* ns, fdio_t* io);

// Validates a |path| argument.
//
// Returns ZX_OK if |path| is non-null and less than |PATH_MAX| in length
// (excluding the null terminator). Upon success, the length of the path is
// returned via |out_length|.
//
// Otherwise, returns |ZX_ERR_INVALID_ARGS|.
zx_status_t fdio_validate_path(const char* path, size_t* out_length);

// Create an |fdio_t| from a |node| and an |info|.
//
// Uses |info| to determine what kind of |fdio_t| to create.
//
// Upon success, |out_io| receives ownership of all handles.
//
// Upon failure, consumes all handles.
zx_status_t fdio_from_node_info(fidl::ClientEnd<fuchsia_io::Node> node,
                                fuchsia_io::wire::NodeInfo info, fdio_t** out_io);

// Creates an |fdio_t| from a |node|.
zx_status_t fdio_from_channel(fidl::ClientEnd<fuchsia_io::Node> node, fdio_t** out_io);

// Creates an |fdio_t| by waiting for a |fuchsia.io/Node.OnOpen| event on |channel|.
//
// Uses the contents of the event to determine what kind of |fdio_t| to create.
//
// Upon success, |out_io| receives ownership of all handles.
//
// Upon failure, consumes all handles.
zx_status_t fdio_from_on_open_event(fidl::ClientEnd<fuchsia_io::Node> client_end, fdio_t** out_io);

// io will be consumed by this and must not be shared
void fdio_chdir(fdio_t* io, const char* path);

// Wraps an arbitrary handle with a fdio_t that works with wait hooks.
// Takes ownership of handle unless shared_handle is true.
fdio_t* fdio_waitable_create(zx_handle_t h, zx_signals_t signals_in, zx_signals_t signals_out,
                             bool shared_handle);

// Returns the sum of the capacities of all the entries in |vector|.
size_t fdio_iovec_get_capacity(const zx_iovec_t* vector, size_t vector_count);

// Copies bytes from |buffer| into |vector|.
//
// Returns the number of bytes copied in |out_actual|.
void fdio_iovec_copy_to(const uint8_t* buffer, size_t buffer_size, const zx_iovec_t* vector,
                        size_t vector_count, size_t* out_actual);

// Copies bytes from |vector| into |buffer|.
//
// Returns the number of bytes copied in |out_actual|.
void fdio_iovec_copy_from(const zx_iovec_t* vector, size_t vector_count, uint8_t* buffer,
                          size_t buffer_size, size_t* out_actual);

using two_path_op = zx_status_t(const char* src, size_t srclen, zx_handle_t dst_token,
                                const char* dst, size_t dstlen);

namespace fdio_internal {

template <typename T>
class AllocHelper final {
 public:
  template <typename... Args>
  static fdio_t* alloc(Args&&... args) {
    return new T(std::forward<Args>(args)...);
  }
};

template <typename T, typename... Args>
fdio_t* alloc(Args&&... args) {
  return AllocHelper<T>::alloc(std::forward<Args>(args)...);
}

}  // namespace fdio_internal

// Lifecycle notes:
//
// Upon creation, objects have a refcount of 1. |acquire| and |release| are used to upref and
// downref, respectively. Upon downref to 0, the object will be freed.
//
// The close hook must be called before free and should only be called once.  In normal use, objects
// are accessed through the fdio_fdtab, and when close is called they are removed from the fdtab and
// the reference that the fdtab itself is holding is released, at which point they will be free()'d
// unless somebody is holding a ref due to an ongoing io transaction, which will certainly fail due
// to underlying handles being closed at which point a downref will happen and destruction will
// follow.
struct fdio {
  virtual zx_status_t close() = 0;
  virtual zx_status_t open(const char* path, uint32_t flags, uint32_t mode, fdio_t** out);
  virtual zx_status_t clone(zx_handle_t* out_handle);

  // |unwrap| releases the underlying handle if applicable.  The caller must ensure there are no
  // concurrent operations on |io|.
  //
  // For example, |fdio_fd_transfer| will call |fdio_unbind_from_fd| which will only succeed when
  // the caller has the last unique reference to the |fdio_t|, thus ensuring that the fd is only
  // transferred when there are no concurrent operations.
  virtual zx_status_t unwrap(zx_handle_t* out_handle);

  // |borrow_channel| borrows the underlying handle if applicable.
  virtual zx_status_t borrow_channel(zx_handle_t* out_handle);

  virtual void wait_begin(uint32_t events, zx_handle_t* out_handle, zx_signals_t* out_signals);
  virtual void wait_end(zx_signals_t signals, uint32_t* out_events);

  // |posix_ioctl| returns an |Errno|, which wraps an errno to be set on failure, or |Errno::Ok| (0)
  // on success.
  virtual Errno posix_ioctl(int req, va_list va);

  virtual zx_status_t get_token(zx_handle_t* out);
  virtual zx_status_t get_attr(zxio_node_attributes_t* out);
  virtual zx_status_t set_attr(const zxio_node_attributes_t* attr);
  virtual uint32_t convert_to_posix_mode(zxio_node_protocols_t protocols,
                                         zxio_abilities_t abilities);
  virtual zx_status_t dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory);
  virtual zx_status_t dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                           zxio_dirent_t** out_entry);
  virtual void dirent_iterator_destroy(zxio_dirent_iterator_t* iterator);
  virtual zx_status_t unlink(const char* path, size_t len);
  virtual zx_status_t truncate(off_t off);
  virtual two_path_op rename;
  virtual two_path_op link;
  virtual zx_status_t get_flags(uint32_t* out_flags);
  virtual zx_status_t set_flags(uint32_t flags);
  virtual zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code);
  virtual zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code);
  virtual zx_status_t listen(int backlog, int16_t* out_code);
  virtual zx_status_t accept(int flags, struct sockaddr* addr, socklen_t* addrlen,
                             zx_handle_t* out_handle, int16_t* out_code);
  virtual zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code);
  virtual zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code);
  virtual zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                                 int16_t* out_code);
  virtual zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                                 int16_t* out_code);
  virtual zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code);
  virtual zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                              int16_t* out_code);
  virtual zx_status_t shutdown(int how, int16_t* out_code);

  virtual bool is_local_dir() { return false; }

  // |ioflag| contains mutable properties of this object, shared by
  // different transports. Possible values are |IOFLAG_*| in private.h.
  uint32_t& ioflag() { return ioflag_; }

  // The zxio object, if the zxio transport is selected in |ops|.
  zxio_storage_t& zxio_storage() { return storage_; }

  // Used to implement SO_RCVTIMEO. See `man 7 socket` for details.
  zx::duration& rcvtimeo() { return rcvtimeo_; }
  // Used to implement SO_SNDTIMEO. See `man 7 socket` for details.
  zx::duration& sndtimeo() { return sndtimeo_; }

  void acquire() { refcount_.fetch_add(1); }

  zx_status_t release() {
    if (refcount_.fetch_sub(1) == 1) {
      zx_status_t status = close();
      delete this;
      return status;
    }
    return ZX_OK;
  }

  bool is_last_reference() { return refcount_.load() == 1; }

 protected:
  friend class fdio_internal::AllocHelper<fdio>;

  fdio() = default;
  virtual ~fdio();

 private:
  // The number of references on this object. Note that each appearance
  // in the fd table counts as one reference on the corresponding object.
  // Ongoing operations will also contribute to the refcount.
  std::atomic_int_fast32_t refcount_ = 1;

  uint32_t ioflag_ = 0;

  zxio_storage_t storage_ = {};

  zx::duration rcvtimeo_ = zx::duration::infinite();

  zx::duration sndtimeo_ = zx::duration::infinite();
};

namespace fdio_internal {
using base = fdio_t;
}  // namespace fdio_internal

using fdio_available = struct {};
using fdio_reserved = struct {};

using fdio_state_t = struct {
  mtx_t lock;
  mtx_t cwd_lock __TA_ACQUIRED_BEFORE(lock);
  mode_t umask __TA_GUARDED(lock);
  fdio_t* root __TA_GUARDED(lock);
  fdio_t* cwd __TA_GUARDED(lock);
  std::array<std::variant<fdio_available, fdio_reserved, fdio_t*>, FDIO_MAX_FD> fdtab
      __TA_GUARDED(lock);
  fdio_ns_t* ns __TA_GUARDED(lock);
  char cwd_path[PATH_MAX] __TA_GUARDED(cwd_lock);
};

extern fdio_state_t __fdio_global_state;

#define fdio_lock (__fdio_global_state.lock)
#define fdio_root_handle (__fdio_global_state.root)
#define fdio_cwd_handle (__fdio_global_state.cwd)
#define fdio_cwd_lock (__fdio_global_state.cwd_lock)
#define fdio_cwd_path (__fdio_global_state.cwd_path)
#define fdio_fdtab (__fdio_global_state.fdtab)
#define fdio_root_ns (__fdio_global_state.ns)

// Returns an fd number greater than or equal to |starting_fd|, following the
// same rules as fdio_bind_fd. If there are no free file descriptors, -1 is
// returned and |errno| is set to EMFILE. The returned |fd| is not valid for
// use outside of |fdio_assign_reserved| and |fdio_release_reserved|.
int fdio_reserve_fd(int starting_fd);

// Assign the given |io| to the reserved |fd|. If |fd| is not reserved, then -1
// is returned and errno is set to EINVAL.
int fdio_assign_reserved(int fd, fdio_t* io);

// Unassign the reservation at |fd|. If |fd| does not resolve to a reservation
// then -1 is returned and errno is set to EINVAL, otherwise |fd| is returned.
int fdio_release_reserved(int fd);

template <class T>
zx::status<typename T::SyncClient>& get_client() {
  static zx::status<typename T::SyncClient> client;
  static std::once_flag once;

  std::call_once(once, [&]() {
    client = [&]() -> zx::status<typename T::SyncClient> {
      auto endpoints = fidl::CreateEndpoints<T>();
      if (endpoints.is_error()) {
        return endpoints.take_error();
      }
      zx_status_t status =
          fdio_service_connect_by_name(T::Name, endpoints->server.channel().release());
      if (status != ZX_OK) {
        return zx::error(status);
      }
      return zx::ok(fidl::BindSyncClient(std::move(endpoints->client)));
    }();
  });
  return client;
}

zx::status<fuchsia_posix_socket::Provider::SyncClient>& fdio_get_socket_provider();

#endif  // LIB_FDIO_INTERNAL_H_
