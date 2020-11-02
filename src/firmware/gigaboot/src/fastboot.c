// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <abr.h>
#include <bootbyte.h>
#include <diskio.h>
#include <fastboot.h>
#include <inet6.h>
#include <inttypes.h>
#include <lib/abr/data.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xefi.h>
#include <zircon/hw/gpt.h>

// Constants.
#define DEBUG 0
#define FB_CMD_MAX_LEN 64
#define FB_HDR_SIZE 4
#define FB_MAX_PAYLOAD_SIZE (UDP6_MAX_PAYLOAD - FB_HDR_SIZE)
#define FB_PROTOCOL_VERSION 4
#define INIT_PKT_SIZE (FB_HDR_SIZE + 4)
#define INITIAL_SEQ_NUM 0x55aa
#define NUM_COMMANDS 6
#define NUM_VARIABLES 14
#define PAGE_SIZE 4096
#define PARTITION_OFFSET 0
#define QUERY_PKT_SIZE (FB_HDR_SIZE + 2)

// Enumeration of the types of packets allowed in the fastboot protocol.
typedef enum {
  ERROR_TYPE = 0x00,
  QUERY_TYPE = 0x01,
  INIT_TYPE = 0x02,
  FASTBOOT_TYPE = 0x03
} pkt_type;

// Enumeration of the phase a fastboot command is in.
typedef enum { IDLE = 0, CMD = 1, DATA = 2, ALLVAR = 3 } fb_cmd_phase;

// Types.
// Fastboot packet.
typedef struct {
  uint8_t pkt_id;
  uint8_t pkt_flags;
  uint16_t seq_num;
  uint8_t data[FB_MAX_PAYLOAD_SIZE];
} fb_pkt_t;

// A UDP destination address.
typedef struct {
  void *daddr;
  uint16_t dport;
  uint16_t sport;
} udp_addr_t;

// fb_cmd_t represents a fastboot command, and contains a function to both
// execute the command and send a response to the host.
typedef struct {
  const char *name;
  void (*func)(char *cmd);
} fb_cmd_t;

// fb_var contains the name of a fastboot variable, along with either a
// constant value or a function that can get it. This function places the result
// in the second argument, and returns 0 on success, -1 on failure.
typedef struct {
  const char *name;
  const char *value;
  int (*func)(const char *arg, char *result);
  const char **default_args;
} fb_var_t;

// fb_img_t represents an in memory download image.
typedef struct {
  uint32_t size;
  uint32_t bytes_received;
  void *data;
} fb_img_t;

// Function prototypes.
// Helpers.
void pp_fb_pkt(const char *direction, fb_pkt_t *pkt, size_t len);
void fb_send_data(const char *msg);
void fb_send_okay(const char *msg);
void fb_send_info(const char *msg);
void fb_send_fail(const char *msg);
void fb_send_ack(void);
void fb_resend(void);

// Functions that respond to each packet type.
void respond_to_init_packet(fb_pkt_t *pkt);
void respond_to_query_packet(fb_pkt_t *pkt);
void respond_to_fastboot_pkt(fb_pkt_t *pkt, size_t len);

// Fastboot command functions. These functions execute a command and send
// results/responses to the host.
void fb_reboot(char *cmd);
void fb_flash(char *cmd);
void fb_erase(char *cmd);
void fb_download(char *cmd);
void fb_getvar(char *cmd);
void fb_set_active(char *cmd);

// Fastboot variable functions. These functions retreive a variable and return
// the value as a null terminated string. They are responsible for sending
// failures to the host when they encountered.
int get_max_download_size(const char *arg, char *result);
int get_current_slot(const char *arg, char *result);
int get_slot_unbootable(const char *slot, char *result);
int get_slot_successful(const char *slot, char *result);
int get_slot_retry_count(const char *slot, char *result);

// Global state.
static uint16_t max_pkt_size;
static udp_addr_t dest_addr;
static fb_pkt_t pkt_to_send;
static size_t pkt_to_send_len;
static char curr_cmd[FB_CMD_MAX_LEN + 1];  // Add space for null terminator.
static uint16_t expected_seq_num = INITIAL_SEQ_NUM;
static fb_img_t curr_img;
static fb_cmd_phase cmd_phase;
static uint8_t curr_var_idx;
static uint8_t curr_var_arg_idx;
static const char *slot_suffix_list[] = {"a", "b", NULL};

// cmdlist maps a command name to the function that handles that command.
static fb_cmd_t cmdlist[NUM_COMMANDS] = {
    {
        // This command handles (-recovery|-bootloader) as well.
        .name = "reboot",
        .func = fb_reboot,
    },
    {
        .name = "flash",
        .func = fb_flash,
    },
    {
        .name = "erase",
        .func = fb_erase,
    },
    {
        .name = "download",
        .func = fb_download,
    },
    {
        .name = "getvar",
        .func = fb_getvar,
    },
    {
        .name = "set_active",
        .func = fb_set_active,
    }};

// varlist contains all variables this bootloader supports.
static fb_var_t varlist[NUM_VARIABLES] = {
    {
      .name = "has-slot",
      .value = "",
    },
    {
        .name = "partition-type",
        .value = "",
    },
    {
        .name = "max-download-size",
        .func = get_max_download_size,
    },
    {
        .name = "is-logical",
        .value = "no",
    },
    {
        .name = "slot-count",
        .value = "2",
    },
    {
        .name = "bootloader-min-versions",
        .value = "0",
    },
    {
        .name = "current-slot",
        .func = get_current_slot,
    },
    {
        .name = "hw-revision",
        .value = "unimplemented",
    },
    {
        .name = "product",
        .value = "unimplemented",
    },
    {
        .name = "serialno",
        .value = "unimplemented",
    },
    {
        .name = "slot-retry-count",
        .func = get_slot_retry_count,
        .default_args = slot_suffix_list,
    },
    {
        .name = "slot-successful",
        .func = get_slot_successful,
        .default_args = slot_suffix_list,
    },
    {
        .name = "slot-unbootable",
        .func = get_slot_unbootable,
        .default_args = slot_suffix_list,
    },
    {
        .name = "version",
        .value = "0.4",
    }};

// fb_recv runs every time a UDP packet destined for the fastboot port is
// received.
void fb_recv(void *data, size_t len, void *saddr, uint16_t sport, uint16_t dport) {
  if (len > sizeof(fb_pkt_t)) {
    fb_send_fail("received fastboot packet larger than max packet size");
    return;
  }
  fb_pkt_t *pkt = (fb_pkt_t *)data;
  if (DEBUG) {
    pp_fb_pkt("host", pkt, len);
  }
  uint16_t cur_seq_num = ntohs(pkt->seq_num);

  // Prepare the destination address.
  dest_addr.daddr = saddr;
  dest_addr.dport = sport;
  dest_addr.sport = dport;

  if (pkt->pkt_id == QUERY_TYPE) {
    // Clear the last response.
    memset(&pkt_to_send, 0, sizeof(pkt_to_send));
    pkt_to_send.pkt_id = pkt->pkt_id;
    pkt_to_send.seq_num = pkt->seq_num;
    pkt_to_send.pkt_flags = 0;

    respond_to_query_packet(pkt);
  } else if (cur_seq_num == expected_seq_num) {
    // Clear the last response.
    memset(&pkt_to_send, 0, sizeof(pkt_to_send));
    pkt_to_send.pkt_id = pkt->pkt_id;
    pkt_to_send.seq_num = pkt->seq_num;
    pkt_to_send.pkt_flags = 0;

    if (pkt->pkt_id == INIT_TYPE) {
      respond_to_init_packet(pkt);
      // Reset the command phase.
      cmd_phase = IDLE;
    } else if (pkt->pkt_id == FASTBOOT_TYPE) {
      respond_to_fastboot_pkt(pkt, len);
    } else if (pkt->pkt_id == ERROR_TYPE) {
      printf("got error from host: %s", (char *)(pkt->data));
    } else {
      // Send an error to the host.
      pkt_to_send.pkt_id = ERROR_TYPE;
      snprintf((char *) pkt_to_send.data, FB_MAX_PAYLOAD_SIZE,
               "fastboot packet had malformed type %#02x", pkt->pkt_id);
      udp6_send((void *)&pkt_to_send, FB_HDR_SIZE +
                strnlen((char *) pkt_to_send.data, FB_MAX_PAYLOAD_SIZE),
                dest_addr.daddr, dest_addr.dport, dest_addr.sport);
      printf("error: malformed type: %#02x", pkt->pkt_id);
      return;
    }
    expected_seq_num += 1;
  } else if (cur_seq_num == expected_seq_num - 1) {
    fb_resend();
  }
}

void respond_to_fastboot_pkt(fb_pkt_t *pkt, size_t len) {
  switch (cmd_phase) {
    case IDLE: {
      memcpy((void *)curr_cmd, (void *)(pkt->data), (len - FB_HDR_SIZE));
      // Ensure that the current command is null terminated, as we will depend
      // on this to tokenize later.
      curr_cmd[len - FB_HDR_SIZE] = '\0';
      cmd_phase = CMD;
      // Handle the "getvar:all" special case, as it requires multi packet
      // interaction.
      if (!strncmp(curr_cmd, "getvar:all", strlen("getvar:all"))) {
        cmd_phase = ALLVAR;
        curr_var_idx = 0;
        curr_var_arg_idx = 0;
      }
      fb_send_ack();
      break;
    }
    case CMD: {
      // Generally, we transition to the IDLE phase after handling a CMD.
      cmd_phase = IDLE;
      bool found = false;
      for (int i = 0; i < NUM_COMMANDS; i++) {
        fb_cmd_t cmd = cmdlist[i];
        // strlen is safe here because the cmd name is specified as a constant
        // above.
        if (!strncmp(curr_cmd, cmd.name, strlen(cmd.name))) {
          found = true;
          cmd.func(curr_cmd);
          break;
        }
      }
      if (!found) {
        fb_send_fail("command not found");
      }
      // Clear the current command.
      memset(curr_cmd, '\0', FB_CMD_MAX_LEN);
      break;
    }
    case DATA: {
      if (curr_img.bytes_received == curr_img.size) {
        fb_send_okay("");
        cmd_phase = IDLE;
      } else {
        // Keep copying data from the host until we've received all of it.
        uint32_t payload_size = len - FB_HDR_SIZE;
        memcpy(curr_img.data + curr_img.bytes_received, pkt->data, payload_size);
        curr_img.bytes_received += payload_size;

        // Send an ACK to tell the host we received the data.
        fb_send_ack();
      }
      break;
    }
    case ALLVAR: {
      if (curr_var_idx == NUM_VARIABLES) {
        // If we've gone through all of our variables, send an OKAY and return
        // to IDLE.
        cmd_phase = IDLE;
        fb_send_okay("");
        return;
      }
      fb_var_t var = varlist[curr_var_idx];
      char allvar_result[FB_CMD_MAX_LEN];
      if (var.value) {
        snprintf(allvar_result, FB_CMD_MAX_LEN, "%s:%s", var.name, var.value);
        fb_send_info(allvar_result);
        curr_var_idx += 1;
      } else {
        const char *arg = NULL;
        if (var.default_args) {
          arg = var.default_args[curr_var_arg_idx];
        }
        char result[FB_CMD_MAX_LEN];
        memset(result, 0, FB_CMD_MAX_LEN);  // Zero out to null terminate.
        if (var.func(arg, result) == 0) {
          // Since the variable was successfully retrieved, generate the
          // formatted key:value pair response and send.
          if (arg) {
            snprintf(allvar_result, sizeof(allvar_result), "%s:%s:%s", var.name, arg, result);
          } else {
            snprintf(allvar_result, sizeof(allvar_result), "%s:%s", var.name, result);
          }
          fb_send_info(allvar_result);
        } else {
          fb_send_fail(result);
        }

        // If we've exhausted all default args, or there are no default args,
        // move to the next var.
        curr_var_arg_idx += 1;
        if (!var.default_args || !var.default_args[curr_var_arg_idx]) {
          curr_var_idx += 1;
          curr_var_arg_idx = 0;
        }
      }

      break;
    }
  }
}

void respond_to_query_packet(fb_pkt_t *pkt) {
  uint16_t be_seq_num = htons(expected_seq_num);
  memcpy((void *)pkt_to_send.data, (void *)&be_seq_num, sizeof(uint16_t));

  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, FB_HDR_SIZE + sizeof(uint16_t));
  }
  udp6_send((void *)&pkt_to_send, FB_HDR_SIZE + sizeof(uint16_t),
            dest_addr.daddr, dest_addr.dport, dest_addr.sport);
}

void respond_to_init_packet(fb_pkt_t *pkt) {
  // In this case, the response data is 2 big endian 2-byte values containing
  // the protocol version and max UDP packet size.
  max_pkt_size = sizeof(fb_pkt_t);
  uint16_t data[2] = {htons(1), htons(max_pkt_size)};
  memcpy((void *)pkt_to_send.data, (void *)&data, 2 * sizeof(uint16_t));

  // Set the max packet size.
  uint16_t host_max_pkt_size = 0;
  memcpy((void *)&host_max_pkt_size, (void *)(pkt->data + 2), sizeof(uint16_t));
  if (ntohs(host_max_pkt_size) < max_pkt_size) {
    max_pkt_size = ntohs(host_max_pkt_size);
  }

  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, FB_HDR_SIZE + 4);
  }

  udp6_send((void *)&pkt_to_send, FB_HDR_SIZE + 4, dest_addr.daddr, dest_addr.dport,
            dest_addr.sport);
}

void fb_reboot(char *cmd) {
  // Throw away the reboot command.
  strtok(cmd, "-");

  char *partition = strtok(NULL, "-");
  if (!partition) {
    bootbyte_set_normal();
  } else if (!strncmp(partition, "bootloader", 10)) {
    bootbyte_set_bootloader();
  } else if (!strncmp(partition, "recovery", 8)) {
    bootbyte_set_recovery();
  }
  fb_send_okay("");
  gSys->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

void fb_flash(char *cmd) {
  // Throw away the flash command string.
  strtok(cmd, ":");

  // Get the partition to flash by getting the next token.
  char *partition = strtok(NULL, ":");
  if (!partition) {
    fb_send_fail("no partition provided to flash");
    return;
  }

  uint8_t guid_value[GPT_GUID_LEN];
  if (guid_value_from_name(partition, guid_value)) {
    fb_send_fail("could not find guid value of partition");
    return;
  }

  efi_status status = write_partition(gImg, gSys, guid_value, partition, PARTITION_OFFSET,
                                      (unsigned char *)curr_img.data, curr_img.size);
  if (status != EFI_SUCCESS) {
    char err_msg[FB_CMD_MAX_LEN];
    snprintf(err_msg, FB_CMD_MAX_LEN, "failed to write partition; efi_status: %016llx", status);
    fb_send_fail(err_msg);
    return;
  }

  fb_send_okay("");
}

void fb_erase(char *cmd) {
  // Throw away the erase command string.
  strtok(cmd, ":");

  // Get the partition to flash by getting the next token.
  char *partition = strtok(NULL, ":");
  if (!partition) {
    fb_send_fail("no partition provided to erase");
    return;
  }

  uint8_t guid_value[GPT_GUID_LEN];
  if (guid_value_from_name(partition, guid_value)) {
    fb_send_fail("could not find guid value of partition");
    return;
  }

  disk_t disk;
  if (disk_find_boot(gImg, gSys, DEBUG, &disk) < 0) {
    fb_send_fail("could not find boot disk");
    return;
  }
  if (disk_find_partition(&disk, DEBUG, guid_value, partition) < 0) {
    fb_send_fail("could not find partition");
    return;
  }
  uint64_t size = (disk.last - disk.first) * disk.blksz;

  // Allocate some memory to clear.
  size_t num_pages = PAGE_SIZE * 16;
  efi_physical_addr pg_addr;
  efi_status status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, num_pages, &pg_addr);
  if (status != EFI_SUCCESS) {
    char err_msg[FB_CMD_MAX_LEN];
    snprintf(err_msg, FB_CMD_MAX_LEN, "failed to allocate memory; efi_status: %016llx", status);
    fb_send_fail(err_msg);
    return;
  }
  size_t increment = num_pages * PAGE_SIZE;
  memset((void *)pg_addr, 0xff, increment);

  // Clear the partition in 256MiB increments. This value is just large enough
  // to erase an entire zircon partition in less than 500ms. Admittedly, this
  // is a bit fragile to future partition size increases, so we should
  // probably intermittently poll the network interface so the host doesn't
  // think the port is closed.
  size_t offset = 0;
  while (size > 0) {
    size_t len = (size < increment) ? size : increment;
    efi_status status = disk_write(&disk, offset, (void *)pg_addr, len);
    if (status != EFI_SUCCESS) {
      char err_msg[FB_CMD_MAX_LEN];
      snprintf(err_msg, FB_CMD_MAX_LEN, "failed to write to disk; efi_status: %016llx", status);
      fb_send_fail(err_msg);
      return;
    }
    size -= len;
    offset += len;
  }

  // Send the OKAY.
  fb_send_okay("");

  // Free the memory.
  gBS->FreePages(pg_addr, num_pages);
}

// Turns a hex string of exactly length 8 into a uint32_t.
uint32_t hex_to_int(const char *hexstring) {
  uint32_t value = 0;
  uint8_t hexstring_length = 8;
  uint8_t bits_per_char = 4;
  for (uint8_t i = 0; i < hexstring_length; i++) {
    char hex_digit = *(hexstring + i);
    uint32_t ascii = (uint32_t)hex_digit;
    if (ascii >= '0' && ascii <= '9') {
      // character is 0-9
      value += (ascii - '0') << ((7 - i) * bits_per_char);
    } else if (ascii >= 'a' && ascii <= 'f') {
      // character is a-f
      uint32_t intermediate = (ascii - 'a') + 10;
      value += intermediate << ((7 - i) * bits_per_char);
    } else {
      // This will lead to unexpected failures if the provided hexstring is
      // 0xffffffff, but this seems like a rare edge case.
      return -1;
    }
  }
  return value;
}

void fb_download(char *cmd) {
  // Throw away download command string.
  strtok(cmd, ":");

  // Free any pages used during a previous download.
  if (curr_img.data != NULL) {
    uint32_t pages_used = (curr_img.size + PAGE_SIZE - 1) / PAGE_SIZE;
    efi_status status = gBS->FreePages((efi_physical_addr)(curr_img.data), pages_used);
    if (status != EFI_SUCCESS) {
      char err_msg[FB_CMD_MAX_LEN];
      snprintf(err_msg, FB_CMD_MAX_LEN, "failed to free memory; efi_status: %016llx", status);
      fb_send_fail(err_msg);
      return;
    }
    curr_img.data = NULL;
    curr_img.bytes_received = 0;
  }

  // Get the size of the current download.
  char *hexstring = strtok(NULL, ":");
  if (!hexstring) {
    fb_send_fail("download size not provided");
    return;
  }
  curr_img.size = hex_to_int(hexstring);
  if (curr_img.size == (uint32_t) (-1)) {
    fb_send_fail("failed to convert download size to integer");
    return;
  }

  // Allocate space for the download.
  uint32_t pages_needed = (curr_img.size + PAGE_SIZE - 1) / PAGE_SIZE;
  efi_physical_addr mem_addr;
  efi_status status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages_needed, &mem_addr);
  if (status != EFI_SUCCESS) {
    char err_msg[FB_CMD_MAX_LEN];
    snprintf(err_msg, FB_CMD_MAX_LEN, "failed to allocate memory; efi_status: %016llx", status);
    fb_send_fail(err_msg);
    return;
  }
  curr_img.data = (void *)mem_addr;

  // Respond with the appropriate DATA packet.
  fb_send_data(hexstring);
  cmd_phase = DATA;
}

void fb_set_active(char *cmd) {
  // Throw away set-active command string.
  strtok(cmd, ":");

  char *slot = strtok(NULL, ":");
  if (!slot) {
    fb_send_fail("no slot provided to set-active");
    return;
  }

  AbrResult res;
  if (*slot == 'a') {
    res = zircon_abr_set_slot_active(kAbrSlotIndexA);
  } else if (*slot == 'b') {
    res = zircon_abr_set_slot_active(kAbrSlotIndexB);
  } else {
    fb_send_fail("invalid slot in set-active");
    return;
  }
  if (res != kAbrResultOk) {
    fb_send_fail("failed to set slot active");
    return;
  }

  fb_send_okay("");
}

// getvar retrieves the value of the requested fastboot variable (if it exists).
void fb_getvar(char *cmd) {
  // Throw away the "getvar" portion of the string.
  strtok(cmd, ":");

  char *varname = strtok(NULL, ":");
  if (!varname) {
    fb_send_fail("no variable provided");
    return;
  }

  char *arg = strtok(NULL, ":");

  bool found = false;
  for (int i = 0; i < NUM_VARIABLES; i++) {
    fb_var_t var = varlist[i];
    // strlen is safe here because all of the variable names and values
    // are constant strings specified above.
    if (!strncmp(varname, var.name, strlen(var.name))) {
      found = true;
      if (var.value != NULL) {
        fb_send_okay(var.value);
      } else {
        char result[FB_CMD_MAX_LEN];
        memset(result, 0, FB_CMD_MAX_LEN);  // Zero out to null terminate.
        if (var.func(arg, result) == 0) {
          fb_send_okay(result);
        } else {
          fb_send_fail(result);
        }
      }
      break;
    }
  }
  if (!found) {
    fb_send_fail("no such variable");
  }
}

// get_max_download_size puts the size of the largest contiguous section of
// memory in the result buffer. Returns 0 on success, -1 on failure.
int get_max_download_size(const char *arg, char *result) {
  efi_memory_type mem_type = EfiLoaderData | EfiConventionalMemory;
  uint64_t max_download_size = 0;
  // Get memory map.
  static char buf[32786];
  size_t buf_size = sizeof(buf);
  size_t mkey = 0;
  size_t dsize = 0;
  uint32_t dversion = 0;
  efi_status status =
      gBS->GetMemoryMap(&buf_size, (efi_memory_descriptor *)buf, &mkey, &dsize, &dversion);
  if (status != EFI_SUCCESS) {
    snprintf(result, FB_MAX_PAYLOAD_SIZE, "failed to get memory map; efi_status: %016llx", status);
    return -1;
  }
  // Look through the memory map for the largest contiguous region of memory.
  for (void *p = (void *)buf; p < (void *)(buf) + buf_size; p += dsize) {
    efi_memory_descriptor *des = (efi_memory_descriptor *)p;
    if ((des->Type & mem_type) && (des->NumberOfPages * PAGE_SIZE) > max_download_size) {
      max_download_size = (des->NumberOfPages * PAGE_SIZE);
    }
  }
  snprintf(result, FB_MAX_PAYLOAD_SIZE, "0x%016llx", max_download_size);
  return 0;
}

// get_current_slot returns the current boot slot.
int get_current_slot(const char *arg, char *result) {
  AbrSlotIndex idx = zircon_abr_get_boot_slot();
  switch (idx) {
    case kAbrSlotIndexA:
      strncpy(result, "a", FB_MAX_PAYLOAD_SIZE);
      break;
    case kAbrSlotIndexB:
      strncpy(result, "b", FB_MAX_PAYLOAD_SIZE);
      break;
    case kAbrSlotIndexR:
      strncpy(result, "r", FB_MAX_PAYLOAD_SIZE);
      break;
    default:
      strncpy(result, "failed to get boot slot", FB_MAX_PAYLOAD_SIZE);
      return -1;
  }
  return 0;
}

// get_slot_info is a helper function that populates an AbrSlotInfo object given
// a slot.
// Returns 0 on success, -1 on failure.
int get_slot_info(char slot, AbrSlotInfo *info) {
  AbrSlotIndex slotIdx;
  if (slot == 'a') {
    slotIdx = kAbrSlotIndexA;
  } else if (slot == 'b') {
    slotIdx = kAbrSlotIndexB;
  } else {
    // Fastboot does not support getting boot bit for any other partition.
    return -1;
  }
  if (zircon_abr_get_slot_info(slotIdx, info) != kAbrResultOk) {
    return -1;
  }
  return 0;
}

int get_slot_unbootable(const char *slot, char *result) {
  if (!slot) {
    strncpy(result, "no slot provided", FB_MAX_PAYLOAD_SIZE);
    return -1;
  }
  AbrSlotInfo info;
  if (get_slot_info(*slot, &info) != 0) {
    strncpy(result, "could not get slot info", FB_MAX_PAYLOAD_SIZE);
    return -1;
  }

  if (!info.is_bootable) {
    strncpy(result, "yes", FB_MAX_PAYLOAD_SIZE);
  } else {
    strncpy(result, "no", FB_MAX_PAYLOAD_SIZE);
  }
  return 0;
}

int get_slot_successful(const char *slot, char *result) {
  if (!slot) {
    strncpy(result, "no slot provided", FB_MAX_PAYLOAD_SIZE);
    return -1;
  }
  AbrSlotInfo info;
  if (get_slot_info(*slot, &info) != 0) {
    strncpy(result, "could not get slot info", FB_MAX_PAYLOAD_SIZE);
    return -1;
  }

  if (info.is_marked_successful) {
    strncpy(result, "yes", FB_MAX_PAYLOAD_SIZE);
  } else {
    strncpy(result, "no", FB_MAX_PAYLOAD_SIZE);
  }
  return 0;
}

int get_slot_retry_count(const char *slot, char *result) {
  if (!slot) {
    strncpy(result, "no slot provided", FB_MAX_PAYLOAD_SIZE);
    return -1;
  }
  AbrSlotInfo info;
  if (get_slot_info(*slot, &info) != 0) {
    strncpy(result, "could not get slot info", FB_MAX_PAYLOAD_SIZE);
    return -1;
  }

  snprintf(result, FB_MAX_PAYLOAD_SIZE, "%d", (kAbrMaxTriesRemaining - info.num_tries_remaining));
  return 0;
}

void pp_fb_pkt(const char *direction, fb_pkt_t *pkt, size_t len) {
  // Pretty printing is too slow when transferring data, so skip in the data
  // phase. TCP dump is generally sufficient when debugging data transfer
  // issues.
  if (cmd_phase == DATA) {
    return;
  }
  printf("Size: %zu, %s: ", len, direction);
  switch (pkt->pkt_id) {
    case ERROR_TYPE:
      printf("ERROR");
      break;
    case QUERY_TYPE:
      printf("QUERY");
      break;
    case INIT_TYPE:
      printf("INIT");
      printf("    Protocol version: 0x%04x ", *((uint16_t *)(pkt->data)));
      printf("    Max packet size: 0x%04x", *((uint16_t *)(pkt->data) + 1));
      break;
    case FASTBOOT_TYPE:
      printf("FASTBOOT");
      break;
    default:
      printf("error: malformed type: %#02x", pkt->pkt_id);
      return;
  }
  printf("    Flags: %02x", pkt->pkt_flags);
  printf("    Seq_Num: %04x", pkt->seq_num);
  pkt->data[len - FB_HDR_SIZE] = '\0';
  printf("    Data: \"%s\" \n", pkt->data);
}

void fb_send_ack(void) {
  pkt_to_send_len = FB_HDR_SIZE;
  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, pkt_to_send_len);
  }
  udp6_send((void *)&pkt_to_send, pkt_to_send_len, dest_addr.daddr, dest_addr.dport,
            dest_addr.sport);
}

void fb_resend(void) {
  udp6_send((void *)&pkt_to_send, pkt_to_send_len, dest_addr.daddr, dest_addr.dport,
            dest_addr.sport);
}

void fb_send(const char *msg) {
  // Copy the payload into the packet.
  size_t msg_len = strnlen(msg, FB_MAX_PAYLOAD_SIZE - 4);
  strncpy((char *)(pkt_to_send.data + 4), msg, msg_len);
  pkt_to_send_len = FB_HDR_SIZE + msg_len + 4;
  // Send the packet.
  if (DEBUG) {
    pp_fb_pkt("device", &pkt_to_send, pkt_to_send_len);
  }

  udp6_send((void *)&pkt_to_send, pkt_to_send_len, dest_addr.daddr, dest_addr.dport,
            dest_addr.sport);
}

void fb_send_okay(const char *msg) {
  memcpy(pkt_to_send.data, "OKAY", 4);
  fb_send(msg);
}

void fb_send_fail(const char *msg) {
  memcpy(pkt_to_send.data, "FAIL", 4);
  fb_send(msg);
}

void fb_send_data(const char *msg) {
  memcpy(pkt_to_send.data, "DATA", 4);
  fb_send(msg);
}

void fb_send_info(const char *msg) {
  memcpy(pkt_to_send.data, "INFO", 4);
  fb_send(msg);
}

