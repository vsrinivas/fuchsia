// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_INTERNAL_CONNECTION_H_
#define LIB_VFS_CPP_INTERNAL_CONNECTION_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>
#include <stddef.h>
#include <stdint.h>

namespace vfs {
namespace internal {

class Node;

// A connection to a file system object.
//
// A connection manages a single zx::channel, typically to another process.
class Connection {
 public:
  // Create a connection with the given |flags|.
  //
  // |flags| define permissions and rights that this connection have over
  // |Node|. They are defined in |fuchsia.io| fidl for example
  // |OPEN_FLAG_DESCRIBE|. These are stored in this object for future use and no
  // validations are performed.
  explicit Connection(uint32_t flags);
  virtual ~Connection();

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // The flags associated with this connection.
  //
  // These flags are typically received from |fuchsia.io.Node/Clone| or
  // |fuchsia.io.Directory/Open|.
  //
  // For example, |ZX_FS_RIGHT_READABLE|.
  uint32_t flags() const { return flags_; }

  // The current file offset.
  //
  // Typically used to position |read| and |write| operations. Can be adjusted
  // using |lseek|.
  uint64_t offset() const { return offset_; }
  void set_offset(uint64_t offset) { offset_ = offset; }

  // Associate |request| with this connection.
  //
  // Waits for messages asynchronously on the |request| channel using
  // |dispatcher|. If |dispatcher| is |nullptr|, the implementation will call
  // |async_get_default_dispatcher| to obtain the default dispatcher for the
  // current thread.
  //
  // After calling internal functions from implementing classes this function
  // will also send OnOpen event if |OPEN_FLAG_DESCRIBE| is present in |flags_|
  // and binding is successful.
  //
  // Returns |ZX_ERR_BAD_STATE| if channel is already bound.
  //
  // Typically called during connection setup.
  zx_status_t Bind(zx::channel request, async_dispatcher_t* dispatcher);

 protected:
  // Send OnOpen event for |fuchsia::io::Node|.
  //
  // This function will not check for |OPEN_FLAG_DESCRIBE|. Caller should do
  // that. Every subclass must implement this.
  //
  // This should only be called from |Bind()|.
  virtual void SendOnOpenEvent(zx_status_t status) = 0;

  // Associate |request| with this connection.
  //
  // This function is called by |Connection::Bind()|.
  //
  // Waits for messages asynchronously on the |request| channel using
  // |dispatcher|. If |dispatcher| is |nullptr|, the implementation will call
  // |async_get_default_dispatcher| to obtain the default dispatcher for the
  // current thread.
  //
  // Should returns |ZX_ERR_BAD_STATE| if channel is already bound.
  virtual zx_status_t BindInternal(zx::channel request,
                                   async_dispatcher_t* dispatcher) = 0;

  // Implementations for common |fuchsia.io.Node| operations. Used by
  // subclasses to avoid code duplication.

  void Clone(Node* vn, uint32_t flags, zx::channel request,
             async_dispatcher_t* dispatcher);
  void Close(Node* vn, fuchsia::io::Node::CloseCallback callback);
  void Describe(Node* vn, fuchsia::io::Node::DescribeCallback callback);
  void Sync(Node* vn, fuchsia::io::Node::SyncCallback callback);
  void GetAttr(Node* vn, fuchsia::io::Node::GetAttrCallback callback);
  void SetAttr(Node* vn, uint32_t flags, fuchsia::io::NodeAttributes attributes,
               fuchsia::io::Node::SetAttrCallback callback);
  void Ioctl(Node* vn, uint32_t opcode, uint64_t max_out,
             std::vector<zx::handle> handles, std::vector<uint8_t> in,
             fuchsia::io::Node::IoctlCallback callback);

  // returns |fuchsia.io.NodeInfo| if status is |ZX_OK|, else returns null
  // inside unique_ptr.
  std::unique_ptr<fuchsia::io::NodeInfo> NodeInfoIfStatusOk(Node* vn,
                                                            zx_status_t status);

 private:
  // The flags associated with this connection.
  //
  // See |flags()| for more information.
  uint32_t flags_ = 0u;

  // The current file offset.
  //
  // See |offset()| for more information.
  uint64_t offset_ = 0u;
};

}  // namespace internal
}  // namespace vfs

#endif  // LIB_VFS_CPP_INTERNAL_CONNECTION_H_
