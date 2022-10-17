// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INTERNAL_H_
#define LIB_FDIO_INTERNAL_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/status.h>
#include <lib/zxio/zxio.h>
#include <sys/socket.h>
#include <zircon/types.h>

#include <variant>

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

// Possibly return an owned fdio_t corresponding to either the root,
// the cwd, or, for the ...at variants, dirfd. In the absolute path
// case, in_out_path is also adjusted.
fdio_ptr fdio_iodir(int dirfd, std::string_view& in_out_path);

// Validates a |path| argument.
//
// Returns ZX_OK if |path| is non-null and less than |PATH_MAX| in length
// (excluding the null terminator). Upon success, the length of the path is
// returned via |out_length|.
//
// Otherwise, returns |ZX_ERR_INVALID_ARGS|.
zx_status_t fdio_validate_path(const char* path, size_t* out_length);

// Wraps an arbitrary handle with an object that works with wait hooks.
zx::result<fdio_ptr> fdio_waitable_create(std::variant<zx::handle, zx::unowned_handle> h,
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

using two_path_op = zx_status_t(std::string_view src, zx_handle_t dst_token, std::string_view dst);

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
  template <typename F>
  static zx::result<fdio_ptr> create(F fn) {
    void* context = nullptr;
    return create(context, fn(zxio_allocator, &context));
  }
  static zx::result<fdio_ptr> create(zx::handle handle);
  static zx::result<fdio_ptr> create(fidl::ClientEnd<fuchsia_io::Node> node,
                                     fuchsia_io::wire::NodeInfoDeprecated info);

  // Waits for a |fuchsia.io/Node.OnOpen| event on channel.
  static zx::result<fdio_ptr> create_with_on_open(fidl::ClientEnd<fuchsia_io::Node> node);

  virtual zx::result<fdio_ptr> open(std::string_view path, fuchsia_io::wire::OpenFlags flags,
                                    uint32_t mode);
  virtual zx_status_t clone(zx_handle_t* out_handle) = 0;
  virtual zx_status_t add_inotify_filter(std::string_view path, uint32_t mask,
                                         uint32_t watch_descriptor, zx::socket socket);

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
                                           zxio_dirent_t* inout_entry);
  virtual void dirent_iterator_destroy(zxio_dirent_iterator_t* iterator);
  virtual zx_status_t watch_directory(zxio_watch_directory_cb cb, zx_time_t deadline,
                                      void* context);
  virtual zx_status_t unlink(std::string_view name, int flags);
  virtual zx_status_t truncate(uint64_t off);
  virtual two_path_op rename;
  virtual two_path_op link;
  virtual zx_status_t get_flags(fuchsia_io::wire::OpenFlags* out_flags);
  virtual zx_status_t set_flags(fuchsia_io::wire::OpenFlags flags);
  virtual zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code);
  virtual zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                              int16_t* out_code);

  virtual bool is_local_dir() { return false; }

  // |ioflag| contains mutable properties of this object, shared by
  // different transports. Possible values are |IOFLAG_*| in private.h.
  //
  // TODO(https://fxbug.dev/75778): The value of this field is not preserved when fdio_fd_transfer
  // is used.
  uint32_t& ioflag() { return ioflag_; }

  // The zxio object, if the zxio transport is selected in |ops|.
  zxio_storage_t& zxio_storage() { return storage_; }
  const zxio_storage_t& zxio_storage() const { return storage_; }

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
  static zx_status_t zxio_allocator(zxio_object_type_t type, zxio_storage_t** out_storage,
                                    void** out_context);

  static zx::result<fdio_ptr> create(void*& context, zx_status_t status);

  virtual zx_status_t close() = 0;

  uint32_t ioflag_ = 0;

  zxio_storage_t storage_ = {};

  zx::duration rcvtimeo_ = zx::duration::infinite();

  zx::duration sndtimeo_ = zx::duration::infinite();
};

#endif  // LIB_FDIO_INTERNAL_H_
