// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_NODE_H_
#define LIB_VFS_NODE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>

namespace vfs {
class Connection;

// An object in a file system.
//
// Implements the |fuchsia.io.Node| interface. Incoming connections are owned by
// this object and will be destroyed when this object is destroyed.
//
// Subclass to implement a particular kind of file system object.
//
// See also:
//
//  * File, which is a subclass for file objects.
//  * Directory, which is a subclass for directory objects.
class Node {
 public:
  Node();
  virtual ~Node();

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  // Implementation of |fuchsia.io.Node/Close|.
  virtual zx_status_t Close();

  // Implementation of |fuchsia.io.Node/Describe|.
  //
  // Subclass must override this method to describe themselves accurately.
  virtual void Describe(fuchsia::io::NodeInfo* out_info) = 0;

  // Implementation of |fuchsia.io.Node/Sync|.
  virtual zx_status_t Sync();

  // Implementation of |fuchsia.io.Node/GetAttr|.
  virtual zx_status_t GetAttr(fuchsia::io::NodeAttributes* out_attributes);

  // Implementation of |fuchsia.io.Node/SetAttr|.
  virtual zx_status_t SetAttr(uint32_t flags,
                              const fuchsia::io::NodeAttributes& attributes);

  // Establishes a connection for |request| using the given |flags|.
  //
  // Waits for messages asynchronously on the |request| channel using
  // |dispatcher|. If |dispatcher| is |nullptr|, the implementation will call
  // |async_get_default_dispatcher| to obtain the default dispatcher for the
  // current thread.
  //
  // Uses |CreateConnection| to create a connection appropriate for the concrete
  // type of this object.
  zx_status_t Serve(uint32_t flags, zx::channel request,
                    async_dispatcher_t* dispatcher = nullptr);

  // Destroys the given connection.
  //
  // The underlying channel is closed.
  void RemoveConnection(Connection* connection);

 protected:
  // Creates a |Connection| appropriate for the concrete type of this object.
  //
  // Subclasses must override this method to create an appropriate connection.
  // The returned connection should be in an "unbound" state.
  //
  // Typically called by |Serve|.
  virtual std::unique_ptr<Connection> CreateConnection(uint32_t flags) = 0;

 private:
  // The active connections associated with this object.
  std::vector<std::unique_ptr<Connection>> connections_;
};

}  // namespace vfs

#endif  // LIB_VFS_NODE_H_
