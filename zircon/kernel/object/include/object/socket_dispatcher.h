// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SOCKET_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SOCKET_DISPATCHER_H_

#include <lib/user_copy/user_ptr.h>
#include <lib/zx/status.h>
#include <stdint.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/intrusive_single_list.h>
#include <fbl/ref_counted.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <object/mbuf.h>

class SocketDispatcher final : public PeeredDispatcher<SocketDispatcher, ZX_DEFAULT_SOCKET_RIGHTS> {
 public:
  class Disposition {
   public:
    enum Value { kNone, kWriteDisabled, kWriteEnabled };

    static zx::result<Disposition> TryFrom(uint32_t disposition);
    explicit Disposition(Value disposition);
    operator Value() const;

   private:
    Value value_;
  };

  enum class ReadType { kConsume, kPeek };

  static zx_status_t Create(uint32_t flags, KernelHandle<SocketDispatcher>* handle0,
                            KernelHandle<SocketDispatcher>* handle1, zx_rights_t* rights);

  ~SocketDispatcher() final;

  // Dispatcher implementation.
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_SOCKET; }

  // Socket methods.
  zx_status_t Write(user_in_ptr<const char> src, size_t len, size_t* written);

  // Set the socket endpoints' dispositions.
  zx_status_t SetDisposition(Disposition disposition, Disposition disposition_peer);

  zx_status_t Read(ReadType type, user_out_ptr<char> dst, size_t len, size_t* nread);

  // Property methods.
  size_t GetReadThreshold() const;
  zx_status_t SetReadThreshold(size_t value);
  size_t GetWriteThreshold() const;
  zx_status_t SetWriteThreshold(size_t value);

  void GetInfo(zx_info_socket_t* info) const;

  // PeeredDispatcher implementation.
  void on_zero_handles_locked() TA_REQ(get_lock());
  void OnPeerZeroHandlesLocked() TA_REQ(get_lock());

 private:
  using PeerHolderType = PeerHolder<SocketDispatcher>;

  SocketDispatcher(fbl::RefPtr<PeerHolderType> holder, zx_signals_t starting_signals,
                   uint32_t flags);
  zx_status_t WriteSelfLocked(user_in_ptr<const char> src, size_t len, size_t* nwritten,
                              Guard<CriticalMutex>& guard) TA_REQ(get_lock());
  void UpdateReadStatus(Disposition disposition_peer) TA_REQ(get_lock());
  [[nodiscard]] bool IsDispositionStateValid(Disposition disposition_peer) const TA_REQ(get_lock());

  bool is_full() const TA_REQ(get_lock()) { return data_.is_full(); }
  bool is_empty() const TA_REQ(get_lock()) { return data_.is_empty(); }

  const uint32_t flags_;

  // The shared |get_lock()| protects all members below.
  MBufChain data_ TA_GUARDED(get_lock());
  size_t read_threshold_;
  size_t write_threshold_;
  bool read_disabled_ TA_GUARDED(get_lock());
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SOCKET_DISPATCHER_H_
