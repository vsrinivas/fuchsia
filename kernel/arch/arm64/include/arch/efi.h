// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define EFI_SUCCESS     0
#define EFI_ERROR               ( 1 << 63)
#define EFI_LOAD_ERROR          ( 1 | EFI_ERROR)
#define EFI_INVALID_PARAMETER   ( 2 | EFI_ERROR)
#define EFI_UNSUPPORTED         ( 3 | EFI_ERROR)
#define EFI_BAD_BUFFER_SIZE     ( 4 | EFI_ERROR)
#define EFI_BUFFER_TOO_SMALL    ( 5 | EFI_ERROR)
#define EFI_NOT_READY           ( 6 | EFI_ERROR)
#define EFI_DEVICE_ERROR        ( 7 | EFI_ERROR)
#define EFI_WRITE_PROTECTED     ( 8 | EFI_ERROR)
#define EFI_OUT_OF_RESOURCES    ( 9 | EFI_ERROR)
#define EFI_NOT_FOUND           (14 | EFI_ERROR)
#define EFI_SECURITY_VIOLATION  (26 | EFI_ERROR)

#define EFI_MAGENTA_MAGIC  (0x4d4147454e544121)
#define EFI_BOOT_SIGNATURE (0x5453595320494249)

#ifndef ASSEMBLY

__BEGIN_CDECLS

typedef unsigned long efi_status_t;
typedef uint8_t efi_bool_t;
typedef uint16_t efi_char16_t;      /* UNICODE character */
typedef uint64_t efi_physical_addr_t;
typedef void *efi_handle_t;



typedef struct {
    uint64_t magic;
    uint64_t ramdisk_base_phys;
    uint64_t ramdisk_size;
    uint32_t cmd_line_len;
    char     cmd_line[];
} efi_magenta_hdr_t;

typedef struct {
    uint8_t b[16];
} efi_guid_t;

#define EFI_GUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
((efi_guid_t) \
{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
  (b) & 0xff, ((b) >> 8) & 0xff, \
  (c) & 0xff, ((c) >> 8) & 0xff, \
  (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

/* Generic EFI table header */
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t headersize;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_hdr_t;


/* Memory types: */
#define EFI_RESERVED_TYPE               0
#define EFI_LOADER_CODE                 1
#define EFI_LOADER_DATA                 2
#define EFI_BOOT_SERVICES_CODE          3
#define EFI_BOOT_SERVICES_DATA          4
#define EFI_RUNTIME_SERVICES_CODE       5
#define EFI_RUNTIME_SERVICES_DATA       6
#define EFI_CONVENTIONAL_MEMORY         7
#define EFI_UNUSABLE_MEMORY             8
#define EFI_ACPI_RECLAIM_MEMORY         9
#define EFI_ACPI_MEMORY_NVS             10
#define EFI_MEMORY_MAPPED_IO            11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                    13
#define EFI_PERSISTENT_MEMORY           14
#define EFI_MAX_MEMORY_TYPE             15

#define EFI_PAGE_SHIFT      12
#define EFI_PAGE_SIZE       (1UL << EFI_PAGE_SHIFT)
#define EFI_ALLOC_ALIGN     (1 << 16)

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t phys_addr;
    uint64_t virt_addr;
    uint64_t num_pages;
    uint64_t attribute;
} efi_memory_desc_t;

typedef struct {
    efi_guid_t guid;
    uint32_t headersize;
    uint32_t flags;
    uint32_t imagesize;
} efi_capsule_header_t;

#define EFI_ALLOCATE_ANY_PAGES      0
#define EFI_ALLOCATE_MAX_ADDRESS    1
#define EFI_ALLOCATE_ADDRESS        2
#define EFI_MAX_ALLOCATE_TYPE       3

typedef int (*efi_freemem_callback_t) (uint64_t start, uint64_t end, void *arg);

#define EFI_TIME_ADJUST_DAYLIGHT 0x1
#define EFI_TIME_IN_DAYLIGHT     0x2
#define EFI_UNSPECIFIED_TIMEZONE 0x07ff

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t pad1;
    uint32_t nanosecond;
    int16_t timezone;
    uint8_t daylight;
    uint8_t pad2;
} efi_time_t;

typedef struct {
    uint32_t resolution;
    uint32_t accuracy;
    uint8_t sets_to_zero;
} efi_time_cap_t;


/* EFI Boot Services table */
typedef struct {
    efi_table_hdr_t hdr;
    void *raise_tpl;
    void *restore_tpl;
    efi_status_t (*allocate_pages)(int, int, unsigned long,
                       efi_physical_addr_t *);
    efi_status_t (*free_pages)(efi_physical_addr_t, unsigned long);
    efi_status_t (*get_memory_map)(unsigned long *, void *, unsigned long *,
                       unsigned long *, uint32_t *);
    efi_status_t (*allocate_pool)(int, unsigned long, void **);
    efi_status_t (*free_pool)(void *);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_status_t (*handle_protocol)(efi_handle_t, efi_guid_t *, void **);
    void *__reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_status_t (*exit_boot_services)(efi_handle_t, unsigned long);
    void *get_next_monotonic_count;
    void *stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    void *locate_protocol;
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces;
    void *calculate_crc32;
    void *copy_mem;
    void *set_mem;
    void *create_event_ex;
} efi_boot_services_t;

#define EFI_RESET_COLD 0
#define EFI_RESET_WARM 1
#define EFI_RESET_SHUTDOWN 2

typedef struct {
    efi_table_hdr_t hdr;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable;
    void *set_variable;
    void *get_next_high_mono_count;
    void *reset_system;
    void *update_capsule;
    void *query_capsule_caps;
    void *query_variable_info;
} efi_runtime_services_t;

#define LOADED_IMAGE_PROTOCOL_GUID \
    EFI_GUID(  0x5b1b31a1, 0x9562, 0x11d2, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b )

#define EFI_SYSTEM_TABLE_SIGNATURE ((uint64_t)0x5453595320494249ULL)

typedef struct {
    efi_table_hdr_t hdr;
    unsigned long fw_vendor;
    uint32_t fw_revision;
    unsigned long con_in_handle;
    unsigned long con_in;
    unsigned long con_out_handle;
    unsigned long con_out;
    unsigned long stderr_handle;
    unsigned long stderr;
    efi_runtime_services_t *runtime;
    efi_boot_services_t *boottime;
    unsigned long nr_tables;
    unsigned long tables;
} efi_system_table_t;


typedef struct {
    uint32_t revision;
    void *parent_handle;
    efi_system_table_t *system_table;
    void *device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    __attribute__((aligned(8))) uint64_t image_size;
    unsigned int image_code_type;
    unsigned int image_data_type;
    unsigned long unload;
} efi_loaded_image_t;

extern struct efi {
    efi_system_table_t *systab; /* EFI system table */
    unsigned int runtime_version;   /* Runtime services version */
    unsigned long mps;      /* MPS table */
    unsigned long acpi;     /* ACPI table  (IA64 ext 0.71) */
    unsigned long acpi20;       /* ACPI table  (ACPI 2.0) */
    unsigned long smbios;       /* SMBIOS table (32 bit entry point) */
    unsigned long smbios3;      /* SMBIOS table (64 bit entry point) */
    unsigned long sal_systab;   /* SAL system table */
    unsigned long boot_info;    /* boot info table */
    unsigned long hcdp;     /* HCDP table */
    unsigned long uga;      /* UGA table */
    unsigned long uv_systab;    /* UV system table */
    unsigned long fw_vendor;    /* fw_vendor */
    unsigned long runtime;      /* runtime table */
    unsigned long config_table; /* config tables */
    unsigned long esrt;     /* ESRT table */
    unsigned long properties_table; /* properties table */
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *get_variable;
    void *get_next_variable;
    void *set_variable;
    void *set_variable_nonblocking;
    void *query_variable_info;
    void *update_capsule;
    void *query_capsule_caps;
    void *get_next_high_mono_count;
    void *reset_system;
    void *set_virtual_address_map;
    struct efi_memory_map *memmap;
    unsigned long flags;
} efi;

struct efi_simple_text_output_protocol {
    void *reset;
    efi_status_t (*output_string)(void *, void *);
    void *test_string;
};

uint64_t efi_boot(void* handle, efi_system_table_t *systable, paddr_t image_addr) __EXTERNALLY_VISIBLE;

__END_CDECLS

#endif