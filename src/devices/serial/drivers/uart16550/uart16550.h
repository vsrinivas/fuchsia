#ifndef SRC_DEVICES_SERIAL_DRIVERS_UART16550_UART16550_H_
#define SRC_DEVICES_SERIAL_DRIVERS_UART16550_UART16550_H_

#include <lib/zx/fifo.h>
#include <zircon/compiler.h>

#include <mutex>
#include <thread>
#include <vector>

#include <ddktl/device.h>
#include <ddktl/protocol/acpi.h>
#include <ddktl/protocol/serialimpl.h>
#include <fbl/function.h>
#include <hwreg/bitfields.h>

namespace uart16550 {

class Uart16550;
using DeviceType = ddk::Device<Uart16550, ddk::UnbindableNew>;

class Uart16550 : public DeviceType, public ddk::SerialImplProtocol<Uart16550, ddk::base_protocol> {
 public:
  Uart16550();

  explicit Uart16550(zx_device_t* parent);

  size_t FifoDepth() const;

  bool Enabled();

  bool NotifyCallbackSet();

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Init();

  // test-use only
  zx_status_t Init(zx::interrupt interrupt, fbl::Function<uint8_t(uint16_t)> port_read,
                   fbl::Function<void(uint8_t, uint16_t)> port_write);

  // test-use only
  zx::unowned_interrupt InterruptHandle();

  // ddk::SerialImplProtocol
  zx_status_t SerialImplGetInfo(serial_port_info_t* info);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplEnable(bool enable);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplRead(void* buf, size_t size, size_t* actual);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplWrite(const void* buf, size_t size, size_t* actual);

  // ddk::SerialImplProtocol
  zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb);

  // ddk::Releasable
  void DdkRelease();

  // ddk::UnbindableNew
  void DdkUnbindNew(ddk::UnbindTxn txn);

 private:
  class PortIo {
   public:
    using PortReadFn = fbl::Function<uint8_t(uint16_t)>;
    using PortWriteFn = fbl::Function<void(uint8_t, uint16_t)>;

    PortIo() = default;

    PortIo(PortReadFn read, PortWriteFn write, uint16_t base)
        : port_read_(std::move(read)), port_write_(std::move(write)), port_base_(base) {}

    template <typename IntType>
    void Write(IntType val, uint32_t offset) {
      static_assert(std::is_same_v<IntType, uint8_t>, "unsupported register access width");
      port_write_(val, static_cast<uint16_t>(port_base_ + offset));
    }

    template <typename IntType>
    IntType Read(uint32_t offset) {
      static_assert(std::is_same_v<IntType, uint8_t>, "unsupported register access width");
      return port_read_(static_cast<uint16_t>(port_base_ + offset));
    }

    uintptr_t base() const { return port_base_; }

   private:
    PortReadFn port_read_ = [](uint16_t /*unused*/) -> uint8_t { return 0x00; };
    PortWriteFn port_write_ = [](uint8_t /*unused*/, uint16_t /*unused*/) {};
    uint16_t port_base_ = 0;
  };

  bool SupportsAutomaticFlowControl() const;

  void ResetFifosLocked() __TA_REQUIRES(device_mutex_);

  void InitFifosLocked() __TA_REQUIRES(device_mutex_);

  void NotifyLocked() __TA_REQUIRES(device_mutex_);

  void HandleInterrupts();

  ddk::AcpiProtocolClient acpi_;
  std::mutex device_mutex_;

  std::thread interrupt_thread_;
  zx::interrupt interrupt_;

  serial_notify_t notify_cb_ __TA_GUARDED(device_mutex_) = {};
  PortIo port_io_ __TA_GUARDED(device_mutex_);

  size_t uart_fifo_len_ = 1;

  bool enabled_ __TA_GUARDED(device_mutex_) = false;
  serial_state_t state_ __TA_GUARDED(device_mutex_) = 0;
};  // namespace uart16550

}  // namespace uart16550

#endif  // SRC_DEVICES_SERIAL_DRIVERS_UART16550_UART16550_H_
