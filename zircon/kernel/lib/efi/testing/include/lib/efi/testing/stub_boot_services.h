// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_STUB_BOOT_SERVICES_H_
#define ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_STUB_BOOT_SERVICES_H_

#include <memory>

#include <efi/boot-services.h>
#include <gmock/gmock.h>

namespace efi {

// Boot services EFI stubs.
//
// The boot services EFI table is complicated enough that it would be
// difficult to fake out all the APIs properly. Instead, we provide these stubs
// to allow tests to easily mock out the functionality they need, either with
// gmock or by subclassing and implementing the functions they need.
//
// Some of the more trivial functionality will be implemented, but can still
// be overridden by subclasses.
//
// Tests that are willing to use gmock should generally prefer to use
// MockBootServices instead, which hooks up the proper mock wrappers and adds
// some additional utility functions.
class StubBootServices {
 public:
  // IMPORTANT: only ONE StubBootServices can exist at a time. Since this is
  // intended to be a global singleton in EFI this shouldn't be a problem,
  // but if a test does attempt to create a second it will cause an exit().
  StubBootServices();
  virtual ~StubBootServices();

  // Not copyable or movable.
  StubBootServices(const StubBootServices&) = delete;
  StubBootServices& operator=(const StubBootServices&) = delete;

  // Returns the underlying efi_boot_services struct.
  efi_boot_services* services() { return &services_; }

  // EFI function implementations.
  // There are a lot of functions here, don't bother adding them until we need them.

  //   efi_tpl (*RaiseTPL)(efi_tpl new_tpl);
  //   void (*RestoreTPL)(efi_tpl old_tpl);

  // Default page allocation implementation is just to call malloc/free.
  //
  // |type| and |memory_type| are ignored, and freeing a different number of
  // pages than were initially allocated is unsupported.
  //
  // Also initializes memory to some non-zero value.
  virtual efi_status AllocatePages(efi_allocate_type type, efi_memory_type memory_type,
                                   size_t pages, efi_physical_addr* memory);
  virtual efi_status FreePages(efi_physical_addr memory, size_t pages);

  virtual efi_status GetMemoryMap(size_t* memory_map_size, efi_memory_descriptor* memory_map,
                                  size_t* map_key, size_t* desc_size, uint32_t* desc_version) {
    return EFI_UNSUPPORTED;
  }

  // Default pool allocation implementation is just to call malloc/free.
  virtual efi_status AllocatePool(efi_memory_type pool_type, size_t size, void** buf);
  virtual efi_status FreePool(void* buf);

  virtual efi_status CreateEvent(uint32_t type, efi_tpl notify_tpl, efi_event_notify notify_fn,
                                 void* notify_ctx, efi_event* event) {
    return EFI_UNSUPPORTED;
  }
  virtual efi_status SetTimer(efi_event event, efi_timer_delay type, uint64_t trigger_time) {
    return EFI_UNSUPPORTED;
  }

  //   efi_status (*WaitForEvent)(size_t num_events, efi_event* event, size_t* index);
  //   efi_status (*SignalEvent)(efi_event event);

  virtual efi_status CloseEvent(efi_event event) { return EFI_UNSUPPORTED; }
  virtual efi_status CheckEvent(efi_event event) { return EFI_UNSUPPORTED; }

  //   efi_status (*InstallProtocolInterface)(efi_handle* handle, const efi_guid* protocol,
  //                                          efi_interface_type intf_type, void* intf);
  //   efi_status (*ReinstallProtocolInterface)(efi_handle hadle, const efi_guid* protocol,
  //                                            void* old_intf, void* new_intf);
  //   efi_status (*UninstallProtocolInterface)(efi_handle handle, const efi_guid* protocol, void*
  //   intf); efi_status (*HandleProtocol)(efi_handle handle, const efi_guid* protocol, void**
  //   intf); efi_status (*RegisterProtocolNotify)(const efi_guid* protocol, efi_event event,
  //                                        void** registration);

  virtual efi_status LocateHandle(efi_locate_search_type search_type, const efi_guid* protocol,
                                  void* search_key, size_t* buf_size, efi_handle* buf) {
    return EFI_UNSUPPORTED;
  }

  //   efi_status (*LocateDevicePath)(const efi_guid* protocol, efi_device_path_protocol** path,
  //                                  efi_handle* device);
  //   efi_status (*InstallConfigurationTable)(const efi_guid* guid, void* table);
  //   efi_status (*LoadImage)(bool boot_policy, efi_handle parent_image_handle,
  //                           efi_device_path_protocol* path, void* src, size_t src_size,
  //                           efi_handle* image_handle);
  //   efi_status (*StartImage)(efi_handle image_handle, size_t* exit_data_size,
  //                            char16_t** exit_data);
  //   efi_status (*Exit)(efi_handle image_handle, efi_status exit_status, size_t exit_data_size,
  //                      char16_t* exit_data);
  //   efi_status (*UnloadImage)(efi_handle image_handle);
  //   efi_status (*ExitBootServices)(efi_handle image_handle, size_t map_key);
  //   efi_status (*GetNextMonotonicCount)(uint64_t* count);
  //   efi_status (*Stall)(size_t microseconds);
  //   efi_status (*SetWatchdogTimer)(size_t timeout, uint64_t watchdog_code, size_t data_size,
  //                                  char16_t* watchdog_data);
  //   efi_status (*ConnectController)(efi_handle controller_handle,
  //                                   efi_handle* driver_image_handle,
  //                                   efi_device_path_protocol* remaining_path, bool recursive);
  //   efi_status (*DisconnectController)(efi_handle controller_handle,
  //                                      efi_handle driver_image_handle,
  //                                      efi_handle child_handle);

  virtual efi_status OpenProtocol(efi_handle handle, const efi_guid* protocol, void** intf,
                                  efi_handle agent_handle, efi_handle controller_handle,
                                  uint32_t attributes) {
    return EFI_UNSUPPORTED;
  }
  virtual efi_status CloseProtocol(efi_handle handle, const efi_guid* protocol,
                                   efi_handle agent_handle, efi_handle controller_handle) {
    return EFI_UNSUPPORTED;
  }

  //   efi_status (*OpenProtocolInformation)(efi_handle handle, const efi_guid* protocol,
  //                                         efi_open_protocol_information_entry** entry_buf,
  //                                         size_t* entry_count);
  //   efi_status (*ProtocolsPerHandle)(efi_handle handle, efi_guid*** protocol_buf,
  //                                    size_t* protocol_buf_count);

  virtual efi_status LocateHandleBuffer(efi_locate_search_type search_type,
                                        const efi_guid* protocol, void* search_key,
                                        size_t* num_handles, efi_handle** buf) {
    return EFI_UNSUPPORTED;
  }

  virtual efi_status LocateProtocol(const efi_guid* protocol, void* registration, void** intf) {
    return EFI_UNSUPPORTED;
  }

  //   efi_status (*InstallMultipleProtocolInterfaces)(efi_handle* handle, ...);
  //   efi_status (*UninstallMultipleProtocolInterfaces)(efi_handle handle, ...);
  //   efi_status (*CalculateCrc32)(void* data, size_t len, uint32_t* crc32);

  // Default implementation is memmove()/memset().
  //
  // UEFI documentation doesn't mention whether the pointers have to be valid
  // when length is 0, so to be cautious the default implementation will also
  // explicitly fail the test if the pointers are invalid.
  virtual void CopyMem(void* dest, const void* src, size_t len);
  virtual void SetMem(void* buf, size_t len, uint8_t val);

  //   efi_status (*CreateEventEx)(uint32_t type, efi_tpl notify_tpl, efi_event_notify notify_fn,
  //                               const void* notify_ctx, const efi_guid* event_group,
  //                               efi_event* event);

 private:
  efi_boot_services services_;
};

// gmock matcher for an efi_guid.
//
// We have to alias to <efi_guid> type explicitly because the compiler can't
// deduce the efi_guid struct type from a generic aggregate initializer.
//
// Example usage:
//   EXPECT_CALL(..., MatchGuid(EFI_FOO_PROTOCOL_GUID), ...);
MATCHER_P(MatchGuidT, guid, "") { return memcmp(&guid, arg, sizeof(guid)) == 0; }
constexpr auto MatchGuid = MatchGuidT<efi_guid>;

// Subclasses StubBootServices to mock out methods using gmock.
//
// This will likely be the most common way to test boot services, but gmock
// is significantly more complicated than gtest and some projects may prefer
// to avoid it, so the base class is still available for direct use.
class MockBootServices : public StubBootServices {
 public:
  MOCK_METHOD(efi_status, GetMemoryMap,
              (size_t * memory_map_size, efi_memory_descriptor* memory_map, size_t* map_key,
               size_t* desc_size, uint32_t* desc_version),
              (override));
  MOCK_METHOD(efi_status, CreateEvent,
              (uint32_t type, efi_tpl notify_tpl, efi_event_notify notify_fn, void* notify_ctx,
               efi_event* event),
              (override));
  MOCK_METHOD(efi_status, SetTimer, (efi_event event, efi_timer_delay type, uint64_t trigger_time),
              (override));
  MOCK_METHOD(efi_status, CloseEvent, (efi_event event), (override));
  MOCK_METHOD(efi_status, CheckEvent, (efi_event event), (override));

  MOCK_METHOD(efi_status, LocateHandle,
              (efi_locate_search_type search_type, const efi_guid* protocol, void* search_key,
               size_t* buf_size, efi_handle* buf),
              (override));
  MOCK_METHOD(efi_status, OpenProtocol,
              (efi_handle handle, const efi_guid* protocol, void** intf, efi_handle agent_handle,
               efi_handle controller_handle, uint32_t attributes),
              (override));
  MOCK_METHOD(efi_status, CloseProtocol,
              (efi_handle handle, const efi_guid* protocol, efi_handle agent_handle,
               efi_handle controller_handle),
              (override));
  MOCK_METHOD(efi_status, LocateHandleBuffer,
              (efi_locate_search_type search_type, const efi_guid* protocol, void* search_key,
               size_t* num_handles, efi_handle** buf),
              (override));
  MOCK_METHOD(efi_status, LocateProtocol,
              (const efi_guid* protocol, void* registration, void** intf), (override));

  // Registers an expectation for protocol opening and closing.
  //
  // This sets up gmock expectations for the most common case, where a protocol
  // is successfully opened and closed. See below for variants than open or
  // close only.
  //
  // Currently |agent_handle|, |controller_handle|, and |attributes| parameters
  // to OpenProtocol()/CloseProtocol() are not checked and can be anything.
  //
  // handle: expected handle.
  // guid: expected protocol GUID.
  // protocol: the protocol table to copy out from OpenProtocol().
  void ExpectProtocol(efi_handle handle, efi_guid guid, void* protocol) {
    ExpectOpenProtocol(handle, guid, protocol);
    ExpectCloseProtocol(handle, guid);
  }

  // Registers expectations for protocol opening or closing only.
  //
  // Used less commonly, in cases like helper functions opening a protocol
  // but then returning it to the caller rather than closing it.
  void ExpectOpenProtocol(efi_handle handle, efi_guid guid, void* protocol);
  void ExpectCloseProtocol(efi_handle handle, efi_guid guid);

  // Similar to ExpectProtocol(), but instead of using EXPECT_CALL to ensure
  // that the protocol is opened and closed, just registers some default
  // behavior using ON_CALL/WillByDefault.
  //
  // This is more useful if you want to inject a test protocol, but don't care
  // how many times it's opened or closed.
  void SetDefaultProtocol(efi_handle handle, efi_guid guid, void* protocol);
};

}  // namespace efi

#endif  // ZIRCON_KERNEL_LIB_EFI_TESTING_INCLUDE_LIB_EFI_TESTING_STUB_BOOT_SERVICES_H_
