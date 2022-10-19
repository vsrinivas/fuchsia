// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_HDA_STREAM_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_HDA_STREAM_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>

#include <audio-proto/audio-proto.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

class IntelHDAStream : public fbl::RefCounted<IntelHDAStream>,
                       public fbl::WAVLTreeContainable<fbl::RefPtr<IntelHDAStream>>,
                       public fidl::WireServer<fuchsia_hardware_audio::RingBuffer> {
 public:
  using RefPtr = fbl::RefPtr<IntelHDAStream>;
  using Tree = fbl::WAVLTree<uint16_t, RefPtr>;
  enum class Type { INVALID, INPUT, OUTPUT, BIDIR };

  // Hardware allows buffer descriptor lists (BDLs) to be up to 256
  // entries long.
  static constexpr size_t MAX_BDL_LENGTH = 256;

  static fbl::RefPtr<IntelHDAStream> Create(Type type, uint16_t id,
                                            MMIO_PTR hda_stream_desc_regs_t* regs,
                                            const fbl::RefPtr<RefCountedBti>& pci_bti,
                                            fbl::RefPtr<fzl::VmarManager> vmar_manager);

  const char* log_prefix() const { return log_prefix_; }
  Type type() const { return type_; }
  Type configured_type() const { return configured_type_; }
  uint8_t tag() const { return tag_; }
  uint16_t id() const { return id_; }
  uint16_t dma_id() const {
    ZX_DEBUG_ASSERT(id() > 0);
    return static_cast<uint16_t>(id() - 1);
  }
  uint16_t GetKey() const { return id(); }

  zx_status_t SetStreamFormat(async_dispatcher_t* dispatcher, uint16_t encoded_fmt,
                              zx::channel server_endpoint) TA_EXCL(channel_lock_);
  void Deactivate() TA_EXCL(channel_lock_);

  void ProcessStreamIRQ() TA_EXCL(notif_lock_);

 protected:
  IntelHDAStream(Type type, uint16_t id, MMIO_PTR hda_stream_desc_regs_t* regs,
                 const fbl::RefPtr<RefCountedBti>& pci_bti,
                 fbl::RefPtr<fzl::VmarManager> vmar_manager);
  ~IntelHDAStream();
  zx_status_t Initialize();

 private:
  friend class IntelHDAController;  // Controllers have access to stuff like Reset and Configure
  friend class fbl::RefPtr<IntelHDAStream>;  // Only our ref ptrs may destruct us.

  void DeactivateLocked() TA_REQ(channel_lock_);
  void EnsureStoppedLocked() TA_REQ(channel_lock_) { EnsureStopped(regs_); }

  // Client request handlers
  zx_status_t ProcessClientRequestLocked(Channel* channel) TA_REQ(channel_lock_);
  void ProcessClientDeactivate();

  // fuchsia hardware audio RingBuffer Interface
  void GetProperties(GetPropertiesCompleter::Sync& completer) override;
  void GetVmo(GetVmoRequestView request,
              fidl::WireServer<fuchsia_hardware_audio::RingBuffer>::GetVmoCompleter::Sync&
                  completer) override;
  void Start(StartCompleter::Sync& completer) override;
  void Stop(StopCompleter::Sync& completer) override;
  void WatchClockRecoveryPositionInfo(
      WatchClockRecoveryPositionInfoCompleter::Sync& completer) override;
  void SetActiveChannels(SetActiveChannelsRequestView request,
                         SetActiveChannelsCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void WatchDelayInfo(WatchDelayInfoCompleter::Sync& completer) override;

  // Release the client ring buffer (if one has been assigned)
  void ReleaseRingBufferLocked() TA_REQ(channel_lock_);

  // Enter and exit the HW reset state.
  //
  // TODO(johngro) : leaving streams in reset at all times seems to have
  // trouble with locking up the hardware (it becomes completely unresponsive
  // to reset, both stream reset and top level reset).  One day we should
  // figure out why; in the meantime, do not leave streams held in reset for
  // any length of time.
  void Reset() { Reset(regs_); }

  // Called during stream allocation and release to configure the type of
  // stream (in the case of a bi-directional stream) and the tag that the
  // stream will put into the outbound SDO frames.
  void Configure(Type type, uint8_t tag);

  // Static helpers which can be used during early initialization
  static void EnsureStopped(MMIO_PTR hda_stream_desc_regs_t* regs);
  static void Reset(MMIO_PTR hda_stream_desc_regs_t* regs);

  // Accessor for the CPU accessible view of the Buffer Descriptor List
  IntelHDABDLEntry* bdl() const {
    return reinterpret_cast<IntelHDABDLEntry*>(bdl_cpu_mem_.start());
  }

  // Parameters determined construction time.
  const Type type_ = Type::INVALID;
  const uint16_t id_ = 0;
  MMIO_PTR hda_stream_desc_regs_t* const regs_ = nullptr;

  // Parameters determined at allocation time.
  Type configured_type_;
  uint8_t tag_;

  // Log prefix storage
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};

  // VMAR manager, shared with other streams and controller.
  fbl::RefPtr<fzl::VmarManager> vmar_manager_;

  // A reference to our controller's BTI.  We will need to this to grant the
  // controller access to the BDLs and the ring buffers that this stream needs
  // to operate.
  const fbl::RefPtr<RefCountedBti> pci_bti_;

  // Storage allocated for this stream context's buffer descriptor list.
  fzl::VmoMapper bdl_cpu_mem_;
  fzl::PinnedVmo bdl_hda_mem_;

  // The channel used by the application to talk to us once our format has
  // been set by the codec.
  fbl::Mutex channel_lock_;
  fbl::RefPtr<RingBufferChannel> channel_ TA_GUARDED(channel_lock_);
  fzl::PinnedVmo pinned_ring_buffer_ TA_GUARDED(channel_lock_);

  // Parameters determined after stream format configuration.
  uint16_t encoded_fmt_ = 0;
  uint16_t fifo_depth_ = 0;
  bool delay_info_updated_ = false;
  int64_t internal_delay_nsec_ = 0;
  uint32_t bytes_per_frame_ TA_GUARDED(channel_lock_) = 0;

  // Parameters determined after ring buffer allocation.
  uint32_t cyclic_buffer_length_ TA_GUARDED(channel_lock_) = 0;
  uint32_t bdl_last_valid_index_ TA_GUARDED(channel_lock_) = 0;

  // Start/stop flag.
  bool running_ TA_GUARDED(channel_lock_) = false;

  // State used by the IRQ thread to deliver position update notifications.
  fbl::Mutex notif_lock_ TA_ACQ_AFTER(channel_lock_);
  fbl::RefPtr<RingBufferChannel> irq_channel_ TA_GUARDED(notif_lock_);
  std::optional<WatchClockRecoveryPositionInfoCompleter::Async> position_completer_
      __TA_GUARDED(notif_lock_);
};

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_HDA_STREAM_H_
