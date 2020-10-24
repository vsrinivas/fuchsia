// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_UART_H_
#define LIB_UART_UART_H_

#include <lib/arch/intrin.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/assert.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include <cstdlib>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include <hwreg/mmio.h>
#include <hwreg/pio.h>

#include "chars-from.h"

namespace uart {

// This template is specialized by payload configuration type (see below).
// It parses bits out of strings from the "kernel.serial" boot option.
template <typename Config>
inline std::optional<Config> ParseConfig(std::string_view) {
  static_assert(std::is_void_v<Config>, "missing specialization");
  return {};
}

// This template is specialized by payload configuration type (see below).
// It recreates a string for Parse.
template <typename Config>
inline void UnparseConfig(const Config& config, FILE* out) {
  static_assert(std::is_void_v<Config>, "missing specialization");
}

// Specific hardware support is implemented in a class uart::xyz::Driver,
// referred to here as UartDriver.  The uart::DriverBase template class
// provides a helper base class for UartDriver implementations.
//
// The UartDriver object represents the hardware itself.  Many UartDriver
// classes hold no state other than the initial configuration data used in the
// constructor, but a UartDriver is not required to be stateless.  However, a
// UartDriver is required to be copy-constructible, trivially destructible,
// and contain no pointers.  This makes it safe to copy an object set up by
// physboot into a new object in the virtual-memory kernel to hand off the
// configuration and the state of the hardware.
//
// All access to the UartDriver object is serialized by its caller, so it does
// no synchronization of its own.  This serves to serialize the actual access
// to the hardware.
//
// The UartDriver API fills four roles:
//  1. Match a ZBI item that configures this driver.
//  2. Generate a ZBI item for another kernel to match this configuration.
//  3. Configure the IoProvider (see below).
//  4. Drive the actual hardware.
//
// The first three are handled by DriverBase.  The KdrvExtra and KdrvConfig
// template arguments give the KDRV_* value and the dcfg_*_t type for the ZBI
// item.  The Pio template argument tells the IoProvider whether this driver
// uses MMIO or PIO (including PIO via MMIO): the number of consecutive PIO
// ports used, or 0 for simple MMIO.
//
// Item matching is done by the MaybeCreate static method.  If the item
// matches KdrvExtra, then the UartDriver(KdrvConfig) constructor is called.
// DriverBase provides this constructor to fill the cfg_ field, which the
// UartDriver can then refer to.  The UartDriver copy constructor copies it.
//
// The calls to drive the hardware are all template functions passed an
// IoProvider object (see below).  The driver accesses device registers using
// hwreg ReadFrom and WriteTo calls on the pointers from the provider.  The
// IoProvider constructed is passed uart.config() and uart.pio_size().
//
template <typename Driver, uint32_t KdrvExtra, typename KdrvConfig, uint16_t Pio = 0>
class DriverBase {
 public:
  using config_type = KdrvConfig;

  DriverBase() = delete;

  DriverBase(const DriverBase&) = default;

  explicit DriverBase(const config_type& cfg) : cfg_(cfg) {}

  constexpr bool operator==(const Driver& other) const {
    return memcmp(&cfg_, &other.cfg_, sizeof(cfg_)) == 0;
  }
  constexpr bool operator!=(const Driver& other) const { return !(*this == other); }

  // API to fill a ZBI item describing this UART.
  constexpr uint32_t type() const { return ZBI_TYPE_KERNEL_DRIVER; }
  constexpr uint32_t extra() const { return KdrvExtra; }
  constexpr size_t size() const { return sizeof(cfg_); }
  void FillItem(void* payload) const { memcpy(payload, &cfg_, sizeof(cfg_)); }

  // API to match a ZBI item describing this UART.
  static std::optional<Driver> MaybeCreate(const zbi_header_t& header, const void* payload) {
    static_assert(alignof(config_type) <= ZBI_ALIGNMENT);
    if (header.type == ZBI_TYPE_KERNEL_DRIVER && header.extra == KdrvExtra &&
        header.length >= sizeof(config_type)) {
      return Driver{*reinterpret_cast<const config_type*>(payload)};
    }
    return {};
  }

  // API to match a configuration string.
  static std::optional<Driver> MaybeCreate(std::string_view string) {
    const auto config_name = Driver::config_name();
    if (string.substr(0, config_name.size()) == config_name) {
      string.remove_prefix(config_name.size());
      auto config = ParseConfig<KdrvConfig>(string);
      if (config) {
        return Driver{*config};
      }
    }
    return {};
  }

  // API to reproduce a configuration string.
  void Unparse(FILE* out) const {
    fprintf(out, "%.*s", static_cast<int>(Driver::config_name().size()),
            Driver::config_name().data());
    UnparseConfig<KdrvConfig>(cfg_, out);
  }

  // API for use in IoProvider setup.
  const config_type& config() const { return cfg_; }
  constexpr uint16_t pio_size() const { return Pio; }

 protected:
  config_type cfg_;

 private:
  template <typename T>
  static constexpr bool Uninstantiated = false;

  // uart::KernelDriver API
  //
  // These are here just to cause errors if Driver is missing its methods.
  // They also serve to document the API required by uart::KernelDriver.
  // They should all be overridden by Driver methods.
  //
  // Each method is a template parameterized by an hwreg-compatible type for
  // accessing the hardware registers via hwreg ReadFrom and WriteTo methods.
  // This lets Driver types be used with hwreg::Mock in tests independent of
  // actual hardware access.

  template <typename IoProvider>
  void Init(IoProvider& io) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing Init");
  }

  // Return true if Write can make forward progress right now.
  template <typename IoProvider>
  bool TxReady(IoProvider& io) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing TxReady");
    return false;
  }

  // This is called only when TxReady() has just returned true.  Advance
  // the iterator at least one and as many as is convenient but not past
  // end, outputting each character before advancing.
  template <typename IoProvider, typename It1, typename It2>
  auto Write(IoProvider& io, It1 it, const It2& end) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing Write");
    return end;
  }

  // Poll for an incoming character and return one if there is one.
  template <typename IoProvider>
  std::optional<uint8_t> Read(IoProvider& io) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing Read");
    return {};
  }

  // Set the UART up to deliver interrupts.  This is called after Init.
  // After this, Interrupt (below) may be called from interrupt context.
  template <typename IoProvider>
  void InitInterrupt(IoProvider& io) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing InitInterrupt");
  }

  // Enable transmit interrupts so Interrupt will be called when TxReady().
  template <typename IoProvider>
  void EnableTxInterrupt(IoProvider& io) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing EnableTxInterrupt");
  }

  // Service an interrupt.  Call tx() if transmit has become ready.
  // If receive has become ready, call rx(read_char, full) one or more
  // times, where read_char() -> uint8_t if there is receive buffer
  // space and full() -> void if there is no space.
  template <typename IoProvider, typename Tx, typename Rx>
  void Interrupt(IoProvider& io, Tx&& tx, Rx&& rx) {
    static_assert(Uninstantiated<IoProvider>, "derived class is missing Interrupt");
  }
};

// The IoProvider is a template class parameterized by UartDriver::config_type,
// i.e. the dcfg_*_t type for the ZBI item's format.  This class is responsible
// for supplying pointers to be passed to hwreg types' ReadFrom and WriteTo.
//
// The IoProvider(UartDriver::config_type, uint16_t pio_size) constructor
// initializes the object.  Then IoProvider::io() is called to yield the
// pointer to pass to hwreg calls.
//
// uart::BasicIoProvider handles the simple case where physical MMIO and PIO
// base addresses are used directly.  It also provides base classes that can be
// subclassed with an overridden constructor that does address mapping.
//
template <typename Config>
class BasicIoProvider;

// This is the default "identity mapping" callback for BasicIoProvider::Init.
// A subclass can have an Init function that calls BasicIoProvider::Init with
// a different callback function.
inline auto DirectMapMmio(uint64_t phys) { return reinterpret_cast<volatile void*>(phys); }

// The specialization used most commonly handles simple MMIO devices.
template <>
class BasicIoProvider<dcfg_simple_t> {
 public:
  // Just install the MMIO base pointer.  The third argument can be passed by
  // a subclass constructor method to map the physical address to a virtual
  // address.
  template <typename T>
  BasicIoProvider(const dcfg_simple_t& cfg, uint16_t pio_size, T&& map_mmio) {
    auto ptr = map_mmio(cfg.mmio_phys);
    if (pio_size != 0) {
      // This is PIO via MMIO, i.e. scaled MMIO.
      io_.emplace<hwreg::RegisterPio>(ptr);
    } else {
      // This is normal MMIO.
      io_.emplace<hwreg::RegisterMmio>(ptr);
    }
  }
  BasicIoProvider(const dcfg_simple_t& cfg, uint16_t pio_size)
      : BasicIoProvider(cfg, pio_size, DirectMapMmio) {}

  auto* io() { return &io_; }

 private:
  std::variant<hwreg::RegisterMmio, hwreg::RegisterPio> io_{std::in_place_index<0>, nullptr};
};

// The specialization for devices using actual PIO only occurs on x86.
#if defined(__x86_64__) || defined(__i386__)
template <>
class BasicIoProvider<dcfg_simple_pio_t> {
 public:
  BasicIoProvider(const dcfg_simple_pio_t& cfg, uint16_t pio_size) : io_(cfg.base) {
    ZX_DEBUG_ASSERT(pio_size > 0);
  }

  auto* io() { return &io_; }

 private:
  hwreg::RegisterDirectPio io_;
};
#endif

// The specialization for devices requiring two separate MMIO areas
// provides an additional entry point for the second one.
template <>
class BasicIoProvider<dcfg_soc_uart_t> {
 public:
  // The third argument can be passed by a subclass's constructor.
  template <typename T>
  BasicIoProvider(const dcfg_soc_uart_t& cfg, uint16_t pio_size, T&& map_mmio)
      : soc_mmio_(map_mmio(cfg.soc_mmio_phys)), uart_mmio_(map_mmio(cfg.uart_mmio_phys)) {
    ZX_DEBUG_ASSERT(pio_size == 0);
  }
  BasicIoProvider(const dcfg_soc_uart_t& cfg, uint16_t pio_size)
      : BasicIoProvider(cfg, pio_size, DirectMapMmio) {}

  auto* io() { return &uart_mmio_; }

  auto* soc_io() { return &soc_mmio_; }

 private:
  hwreg::RegisterMmio soc_mmio_, uart_mmio_;
};

// The Sync class provides synchronization around the UartDriver.
//
// This is the degenerate case of the uart::KernelDriver Sync API.
// It busy-waits with no locking.
struct TA_CAP("uart") Unsynchronized {
  // This is returned by lock() and passed back to unlock().
  struct InterruptState {};

  // The Sync object is normally default-constructed.
  Unsynchronized() = default;

  // The constructor argument is the UartDriver object.  This is only used
  // by uart::mock::Sync so other implementations use a template just to
  // ignore the argument without specializing the whole class on its type.
  template <typename T>
  explicit Unsynchronized(const T&) {}

  // This is the normal pair, used in "process context", i.e. where
  // interrupts might happen.  unlock takes the state returned by lock.
  [[nodiscard]] const InterruptState lock() TA_ACQ() { return {}; }
  void unlock(InterruptState) TA_REL() {}

  // Wait for a good time to check again.  Implementations that actually
  // block pending an interrupt first call enable_tx_interrupt(), then
  // unlock to block, and finally relock when woken before return.
  template <typename T>
  InterruptState Wait(InterruptState, T&& enable_tx_interrupt) TA_REQ(this) {
    arch::Yield();
    return {};
  }

  // In blocking implementations, the interrupt handler calls this.  It should
  // statically never be reached in an instantiation using this implementation,
  // but static_assert for that only works in templates.
  void Wake() TA_REQ(this) { ZX_PANIC("uart::Unsynchronized::Wake() should never be called"); }
};

// Forward declaration.
namespace mock {
class Driver;
}

// The KernelDriver template class is parameterized by those three to implement
// actual driver logic for some environment.
//
// The KernelDriver constructor just passes its arguments through to the
// UartDriver constructor.  So it can be created directly from a configuration
// struct or copied from another UartDriver object.  In this way, the device is
// handed off from one KernelDriver instantiation to a different one using a
// different IoProvider (physboot vs kernel) and/or Sync (polling vs blocking).
//
template <class UartDriver, template <typename> class IoProvider, class Sync>
class KernelDriver {
 public:
  using uart_type = UartDriver;
  static_assert(std::is_copy_constructible_v<uart_type>);
  static_assert(std::is_trivially_destructible_v<uart_type> ||
                std::is_same_v<uart_type, mock::Driver>);

  // This sets up the object but not the device itself.  The device might
  // already have been set up by a previous instantiation's Init function,
  // or might never actually be set up because this instantiation gets
  // replaced with a different one before ever calling Init.
  template <typename... Args>
  explicit KernelDriver(Args&&... args)
      : uart_(std::forward<Args>(args)...), io_(uart_.config(), uart_.pio_size()) {}

  // Access underlying hardware driver object.
  const auto& uart() const { return uart_; }
  auto& uart() { return uart_; }

  // Access IoProvider object.
  auto& io() { return io_; }

  // Set up the device for nonblocking output and polling input.
  // If the device is handed off from a different instantiation,
  // this won't be called in the new instantiation.
  void Init() {
    Guard lock(sync_);
    uart_.Init(io_);
  }

  // TODO(mcgrathr): Add InitInterrupt for enabling interrupt-based i/o.

  // This is the FILE-compatible API: `FILE::stdout_ = FILE{&driver};`.
  int Write(std::string_view str) {
    uart::CharsFrom chars(str);  // Massage into uint8_t with \n -> CRLF.
    auto it = chars.begin();
    Guard lock(sync_);
    while (it != chars.end()) {
      // Wait until the UART is ready for Write.
      while (!uart_.TxReady(io_)) {
        // Block or just unlock and spin or whatever "wait" means to Sync.  If
        // that means blocking for interrupt wakeup, enable the tx interrupt.
        lock.Wait([this]() TA_REQ(sync_) { uart_.EnableTxInterrupt(io_); });
      }
      // Advance the iterator by writing some.
      it = uart_.Write(io_, it, chars.end());
    }
    return static_cast<int>(str.size());
  }

  // This is a direct polling read, not used in interrupt-based operation.
  auto Read() {
    Guard lock(sync_);
    return uart_.Read(io_);
  }

 private:
  friend Sync;
  uart_type uart_ TA_GUARDED(sync_);
  IoProvider<typename uart_type::config_type> io_;
  Sync sync_{uart_};

  class TA_SCOPED_CAP Guard {
   public:
    template <typename... T>
    __WARN_UNUSED_CONSTRUCTOR explicit Guard(Sync& sync, T... args) TA_ACQ(sync) TA_ACQ(sync_)
        : sync_(sync), state_(sync_.lock(std::forward<T>(args)...)) {}

    ~Guard() TA_REL() { sync_.unlock(std::move(state_)); }

    template <typename T>
    void Wait(T&& enable) TA_REQ(sync_) {
      state_ = sync_.Wait(std::move(state_), std::forward<T>(enable));
    }

    void Wake() TA_REQ(sync_) { sync_.Wake(); }

   private:
    Sync& sync_;
    typename Sync::InterruptState state_;
  };
};

// These specializations are defined in the library to cover all the ZBI item
// payload types used by the various drivers.

template <>
std::optional<dcfg_simple_t> ParseConfig<dcfg_simple_t>(std::string_view string);

template <>
void UnparseConfig(const dcfg_simple_t& config, FILE* out);

template <>
std::optional<dcfg_simple_pio_t> ParseConfig<dcfg_simple_pio_t>(std::string_view string);

template <>
void UnparseConfig(const dcfg_simple_pio_t& config, FILE* out);

template <>
std::optional<dcfg_soc_uart_t> ParseConfig<dcfg_soc_uart_t>(std::string_view string);

template <>
void UnparseConfig(const dcfg_soc_uart_t& config, FILE* out);

}  // namespace uart

#endif  // LIB_UART_UART_H_
