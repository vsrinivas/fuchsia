// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INTERNAL_H_
#define LIB_FDIO_INTERNAL_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/debuglog.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <threads.h>
#include <zircon/types.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

using fdio_ptr = fbl::RefPtr<fdio>;

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
#define IOFLAG_NONBLOCK (1 << 4)

// The subset of fdio_t per-fd flags queryable via fcntl.
// Static assertions in unistd.cc ensure we aren't colliding.
#define IOFLAG_FD_FLAGS IOFLAG_CLOEXEC

// Waits until one or more |events| are signalled, or the |deadline| passes.
// The |events| are of the form |FDIO_EVT_*|, defined in io.h.
// If not NULL, |out_pending| returns a bitmap of all observed events.
zx_status_t fdio_wait(const fdio_ptr& io, uint32_t events, zx::time deadline,
                      uint32_t* out_pending);

fdio_ptr fdio_iodir(const char** path, int dirfd);

zx::status<fdio_ptr> fdio_datagram_socket_create(
    zx::eventpair event, fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client);

zx::status<fdio_ptr> fdio_stream_socket_create(
    zx::socket socket, fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client);

zx::status<fdio_ptr> fdio_raw_socket_create(
    zx::eventpair event, fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client);

// Creates an |fdio_t| referencing the root of the |ns| namespace.
zx::status<fdio_ptr> fdio_ns_open_root(fdio_ns_t* ns);

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

void fdio_chdir(fdio_ptr io, const char* path);

// Wraps an arbitrary handle with an object that works with wait hooks.
zx::status<fdio_ptr> fdio_waitable_create(std::variant<zx::handle, zx::unowned_handle> h,
                                          zx_signals_t signals_in, zx_signals_t signals_out);

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
struct fdio : protected fbl::RefCounted<fdio>, protected fbl::Recyclable<fdio> {
  static zx::status<fdio_ptr> create(zx::handle handle);
  static zx::status<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::Node> node,
                                     fuchsia_io::wire::NodeInfo info);

  // Uses |fuchsia.io/Node.Describe| to obtain a |fuchsia.io/NodeInfo|.
  static zx::status<fdio_ptr> create_with_describe(fidl::ClientEnd<fuchsia_io::Node> node);

  // Waits for a |fuchsia.io/Node.OnOpen| event on channel.
  static zx::status<fdio_ptr> create_with_on_open(fidl::ClientEnd<fuchsia_io::Node> node);

  virtual zx::status<fdio_ptr> open(const char* path, uint32_t flags, uint32_t mode);
  virtual zx_status_t clone(zx_handle_t* out_handle) = 0;
  virtual zx_status_t add_inotify_filter(const char* path, uint32_t mask, uint32_t watch_descriptor,
                                         zx::socket socket);

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
  virtual zx_status_t dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory);
  virtual zx_status_t dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                           zxio_dirent_t** out_entry);
  virtual void dirent_iterator_destroy(zxio_dirent_iterator_t* iterator);
  virtual zx_status_t unlink(const char* name, size_t len, int flags);
  virtual zx_status_t truncate(uint64_t off);
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
  //
  // TODO(https://fxbug.dev/75778): The value of this field is not preserved when fdio_fd_transfer
  // is used.
  uint32_t& ioflag() { return ioflag_; }

  // The zxio object, if the zxio transport is selected in |ops|.
  zxio_storage_t& zxio_storage() { return storage_; }

  // Used to implement SO_RCVTIMEO. See `man 7 socket` for details.
  zx::duration& rcvtimeo() { return rcvtimeo_; }
  // Used to implement SO_SNDTIMEO. See `man 7 socket` for details.
  zx::duration& sndtimeo() { return sndtimeo_; }

  // Automatically calls |fdio_t::Close| on drop.
  struct last_reference {
   public:
    explicit last_reference(fdio_t* ptr) : ptr_(ptr, deleter) {}
    ~last_reference() {
      if (ptr_) {
        ptr_->close();
      }
    }

    last_reference(last_reference&& other) = default;
    last_reference& operator=(last_reference&& other) = default;

    fdio_t* ExportToRawPtr() { return ptr_.release(); }

    zx_status_t unwrap(zx_handle_t* out_handle) { return ptr_->unwrap(out_handle); }

    // Close and destroy the underlying object.
    zx_status_t Close() { return std::exchange(ptr_, nullptr)->close(); }

   private:
    // Custom deleter to keep the destructor buried.
    std::unique_ptr<fdio_t, void (*)(fdio_t*)> ptr_;
  };

  // Helpers from the reference documentation for std::visit<>, to allow
  // visit-by-overload of the std::variant<> returned by GetLastReference():
  template <class... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };
  // explicit deduction guide (not needed as of C++20)
  template <class... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<fdio>;
  // TODO(tamird/johngro): can we bury ExportToRawPtr? The only user outside of |fdio_slot| and
  // |last_reference| is |fdio_unsafe_fd_to_io|.
  //
  // TODO(tamird/johngro): can we bury ImportFromRawPtr? The only users outside of |fdio_slot| are
  // |fdio_bind_to_fd| and |fdio_unsafe_release|.
  friend class fbl::RefPtr<fdio>;
  friend class fbl::Recyclable<fdio>;

  // Returns the supplied |io| if it is not the last reference to the underlying
  // resource.
  friend std::variant<last_reference, fdio_ptr> GetLastReference(fdio_ptr io);

  static void deleter(fdio_t* ptr) { delete ptr; }

  fdio() = default;
  virtual ~fdio();

  void fbl_recycle() {
    close();
    delete this;
  }

 private:
  virtual zx_status_t close() = 0;

  uint32_t ioflag_ = 0;

  zxio_storage_t storage_ = {};

  zx::duration rcvtimeo_ = zx::duration::infinite();

  zx::duration sndtimeo_ = zx::duration::infinite();
};

namespace fdio_internal {

using base = fdio_t;

}  // namespace fdio_internal

// TODO(tamird): every operation on this type should require the global lock.
struct fdio_slot {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(fdio_slot);

  fdio_slot() = default;

  fdio_ptr get() {
    fdio_t** ptr = std::get_if<fdio_t*>(&inner_);
    if (ptr != nullptr) {
      return fbl::RefPtr(*ptr);
    }
    return nullptr;
  }

  fdio_ptr release() {
    fdio_t** ptr = std::get_if<fdio_t*>(&inner_);
    if (ptr != nullptr) {
      fdio_ptr io = fbl::ImportFromRawPtr(*ptr);
      inner_ = available{};
      return io;
    }
    return nullptr;
  }

  bool try_set(fdio_ptr io) {
    if (std::holds_alternative<available>(inner_)) {
      inner_ = fbl::ExportToRawPtr(&io);
      return true;
    }
    return false;
  }

  fdio_ptr replace(fdio_ptr io) {
    auto previous = std::exchange(inner_, fbl::ExportToRawPtr(&io));
    fdio_t** ptr = std::get_if<fdio_t*>(&previous);
    if (ptr != nullptr) {
      return fbl::ImportFromRawPtr(*ptr);
    }
    return nullptr;
  }

  std::optional<void (fdio_slot::*)()> try_reserve() {
    if (std::holds_alternative<available>(inner_)) {
      inner_ = reserved{};
      return &fdio_slot::release_reservation;
    }
    return std::nullopt;
  }

  bool try_fill(fdio_ptr io) {
    if (std::holds_alternative<reserved>(inner_)) {
      inner_ = fbl::ExportToRawPtr(&io);
      return true;
    }
    return false;
  }

 private:
  struct available {};
  struct reserved {};

  void release_reservation() {
    if (std::holds_alternative<reserved>(inner_)) {
      inner_ = available{};
    }
  }

  // TODO(https::/fxbug.dev/72214): clang incorrectly rejects std::variant<.., fdio_ptr> as a
  // non-literal type. When that is fixed, change this |fdio_t*| to |fdio_ptr|.
  std::variant<available, reserved, fdio_t*> inner_;
};

using fdio_state_t = struct {
  mtx_t lock;
  mtx_t cwd_lock __TA_ACQUIRED_BEFORE(lock);
  mode_t umask __TA_GUARDED(lock);
  fdio_slot root __TA_GUARDED(lock);
  fdio_slot cwd __TA_GUARDED(lock);
  std::array<fdio_slot, FDIO_MAX_FD> fdtab __TA_GUARDED(lock);
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

template <class T>
zx::status<typename fidl::WireSyncClient<T>>& get_client() {
  static zx::status<typename fidl::WireSyncClient<T>> client;
  static std::once_flag once;

  std::call_once(once, [&]() {
    client = [&]() -> zx::status<typename fidl::WireSyncClient<T>> {
      auto endpoints = fidl::CreateEndpoints<T>();
      if (endpoints.is_error()) {
        return endpoints.take_error();
      }
      zx_status_t status = fdio_service_connect_by_name(fidl::DiscoverableProtocolName<T>,
                                                        endpoints->server.channel().release());
      if (status != ZX_OK) {
        return zx::error(status);
      }
      return zx::ok(fidl::BindSyncClient(std::move(endpoints->client)));
    }();
  });
  return client;
}

#endif  // LIB_FDIO_INTERNAL_H_
