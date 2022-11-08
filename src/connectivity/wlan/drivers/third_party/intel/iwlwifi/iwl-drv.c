/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/img.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-agn-hw.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-config.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-dbg-tlv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-modparams.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/align.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/module.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/stats.h"
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-dbg-cfg.h"
#endif
#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-tm-gnl.h"
#endif

#define FIRMWARE_DIR "iwlwifi"
#define DRV_DESCRIPTION "Intel(R) Wireless WiFi driver for Fuchsia"

#ifdef CPTCFG_IWLWIFI_DEBUGFS
static struct dentry* iwl_dbgfs_root;
#endif

/**
 * struct iwl_drv - drv common data
 * @list: list of drv structures using this opmode
 * @fw: the iwl_fw structure
 * @op_mode: the running op_mode
 * @trans: transport layer
 * @dev: for debug prints only
 * @fw_index: firmware revision to try loading
 * @firmware_name: composite filename of ucode file to load
 * @request_firmware_complete: the firmware has been obtained from user space
 */
struct iwl_drv {
  list_node_t list;
  struct iwl_fw fw;

  struct iwl_op_mode* op_mode;
  struct iwl_trans* trans;
  struct device* dev;
#if IS_ENABLED(CPTCFG_IWLXVT)
  bool xvt_mode_on;
#endif

  int fw_index;           /* firmware we're trying to load */
  char firmware_name[64]; /* name of firmware file to load */

  sync_completion_t request_firmware_complete;

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  struct dentry* dbgfs_drv;
  struct dentry* dbgfs_trans;
  struct dentry* dbgfs_op_mode;
#endif
};

enum {
  DVM_OP_MODE,
  MVM_OP_MODE,
#if IS_ENABLED(CPTCFG_IWLXVT)
  XVT_OP_MODE,
#endif
#if IS_ENABLED(CPTCFG_IWLTEST)
  TRANS_TEST_OP_MODE,
#endif
#if IS_ENABLED(CPTCFG_IWLFMAC)
  FMAC_OP_MODE,
#endif
};

/* Protects the table contents, i.e. the ops pointer & drv list */
static mtx_t iwlwifi_opmode_table_mtx;
static struct iwlwifi_opmode_table {
  const char* name;                  /* name: iwldvm, iwlmvm, etc */
  const struct iwl_op_mode_ops* ops; /* pointer to op_mode ops */
  list_node_t drv;                   /* list of devices using this op_mode */
} iwlwifi_opmode_table[] = {
    /* ops set when driver is initialized */
    [DVM_OP_MODE] = {.name = "iwldvm", .ops = NULL},
    [MVM_OP_MODE] = {.name = "iwlmvm", .ops = NULL},
#if IS_ENABLED(CPTCFG_IWLFMAC)
    [FMAC_OP_MODE] = {.name = "iwlfmac", .ops = NULL},
#endif
#if IS_ENABLED(CPTCFG_IWLXVT)
    [XVT_OP_MODE] = {.name = "iwlxvt", .ops = NULL},
#endif
#if IS_ENABLED(CPTCFG_IWLTEST)
    [TRANS_TEST_OP_MODE] = {.name = "iwltest", .ops = NULL},
#endif
};

#if IS_ENABLED(CPTCFG_IWLXVT)
/* kernel object for a device dedicated
 * folder in the sysfs */
static struct kobject* iwl_kobj;

static struct iwl_op_mode* _iwl_op_mode_start(struct iwl_drv* drv, struct iwlwifi_opmode_table* op);
static void _iwl_op_mode_stop(struct iwl_drv* drv);

/*
 * iwl_drv_get_dev_container - Given a device, returns the pointer
 * to it's corresponding driver's struct
 */
struct iwl_drv* iwl_drv_get_dev_container(struct device* dev) {
  struct iwl_drv* drv_itr;
  int i;

  /* Going over all drivers, looking for the one that holds dev */
  for (i = 0; (i < ARRAY_SIZE(iwlwifi_opmode_table)); i++) {
    list_for_each_entry(drv_itr, &iwlwifi_opmode_table[i].drv, list) if (drv_itr->dev == dev) {
      return drv_itr;
    }
  }

  return NULL;
}
IWL_EXPORT_SYMBOL(iwl_drv_get_dev_container);

/*
 * iwl_drv_get_op_mode - Returns the index of the device's
 * active operation mode
 */
static zx_status_t iwl_drv_get_op_mode_idx(struct iwl_drv* drv) {
  struct iwl_drv* drv_itr;
  int i;

  if (!drv || !drv->dev) {
    return -ENODEV;
  }

  /* Going over all drivers, looking for the list that holds it */
  for (i = 0; (i < ARRAY_SIZE(iwlwifi_opmode_table)); i++) {
    list_for_each_entry(drv_itr, &iwlwifi_opmode_table[i].drv, list) {
      if (drv_itr->dev == drv->dev) {
        return i;
      }
    }
  }

  return ZX_ERR_INVALID_ARGS;
}

static bool iwl_drv_xvt_mode_supported(enum iwl_fw_type fw_type, int mode_idx) {
  /* xVT mode is available only with 16 FW */
  switch (fw_type) {
    case IWL_FW_MVM:
#if IS_ENABLED(CPTCFG_IWLFMAC)
    case IWL_FW_FMAC:
#endif
      break;
    default:
      return false;
  }

  /* check whether the requested operation mode is supported */
  switch (mode_idx) {
    case XVT_OP_MODE:
    case MVM_OP_MODE:
#if IS_ENABLED(CPTCFG_IWLFMAC)
    case FMAC_OP_MODE:
#endif
      return true;
    default:
      return false;
  }
}

/*
 * iwl_drv_switch_op_mode - Switch between operation modes
 * Checks if the desired operation mode is valid, if it
 * is supported by the device. Stops the current op mode
 * and starts the desired mode.
 */
zx_status_t iwl_drv_switch_op_mode(struct iwl_drv* drv, const char* new_op_name) {
  struct iwlwifi_opmode_table* new_op = NULL;
  int idx;

  /* Searching for wanted op_mode*/
  for (idx = 0; idx < ARRAY_SIZE(iwlwifi_opmode_table); idx++) {
    if (!strcmp(iwlwifi_opmode_table[idx].name, new_op_name)) {
      new_op = &iwlwifi_opmode_table[idx];
      break;
    }
  }

  /* Checking if the desired op mode is valid */
  if (!new_op) {
    IWL_ERR(drv, "No such op mode \"%s\"\n", new_op_name);
    return ZX_ERR_INVALID_ARGS;
  }

  /*
   * If the desired op mode is already the
   * device's current op mode, do nothing
   */
  if (idx == iwl_drv_get_op_mode_idx(drv)) {
    return 0;
  }

  /* Check if the desired operation mode is supported by the device/fw */
  if (!iwl_drv_xvt_mode_supported(drv->fw.type, idx)) {
    IWL_ERR(drv, "Op mode %s is not supported by the loaded fw\n", new_op_name);
    return -ENOTSUPP;
  }

  /* Recording new op mode state */
  drv->xvt_mode_on = (idx == XVT_OP_MODE);

  /* Stopping the current op mode */
  _iwl_op_mode_stop(drv);

  /* Changing operation mode */
  mtx_lock(&iwlwifi_opmode_table_mtx);
  list_move_tail(&drv->list, &new_op->drv);
  mtx_unlock(&iwlwifi_opmode_table_mtx);

  /* Starting the new op mode */
  if (new_op->ops) {
    drv->op_mode = _iwl_op_mode_start(drv, new_op);
    if (!drv->op_mode) {
      IWL_ERR(drv, "Error switching op modes\n");
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    return iwl_module_request("%s", new_op->name);
  }

  return 0;
}
IWL_EXPORT_SYMBOL(iwl_drv_switch_op_mode);

/*
 * iwl_drv_sysfs_show - Returns device information to user
 */
static ssize_t iwl_drv_sysfs_show(struct device* dev, struct device_attribute* attr, char* buf) {
  struct iwl_drv* drv;
  int op_mode_idx = 0, itr;
  int ret = 0;

  /* Retrieving containing driver */
  drv = iwl_drv_get_dev_container(dev);
  op_mode_idx = iwl_drv_get_op_mode_idx(drv);

  /* Checking if driver and driver information are valid */
  if (op_mode_idx < 0) {
    return op_mode_idx;
  }

  /* Constructing output */
  for (itr = 0; itr < ARRAY_SIZE(iwlwifi_opmode_table); itr++) {
    ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s%-s\n", (itr == op_mode_idx) ? "* " : "  ",
                     iwlwifi_opmode_table[itr].name);
  }

  return ret;
}

/* Attribute for device */
static const DEVICE_ATTR(op_mode, S_IRUGO, iwl_drv_sysfs_show, NULL);

/*
 * iwl_create_sysfs_file - Creates a sysfs entry (under PCI devices),
 * and a symlink under modules/iwlwifi
 */
static int iwl_create_sysfs_file(struct iwl_drv* drv) {
  int ret;

  ret = device_create_file(drv->dev, &dev_attr_op_mode);
  if (!ret) {
    ret = sysfs_create_link(iwl_kobj, &drv->dev->kobj, dev_name(drv->dev));
  }

  return ret;
}

/*
 * iwl_remove_sysfs_file - Removes sysfs entries
 */
static void iwl_remove_sysfs_file(struct iwl_drv* drv) {
  sysfs_remove_link(iwl_kobj, dev_name(drv->dev));
  device_remove_file(drv->dev, &dev_attr_op_mode);
}
#endif /* CPTCFG_IWLXVT */

#define IWL_DEFAULT_SCAN_CHANNELS 40

/*
 * struct fw_sec: Just for the image parsing process.
 * For the fw storage we are using struct fw_desc.
 */
struct fw_sec {
  const void* data; /* the sec data */
  size_t size;      /* section size */
  uint32_t offset;  /* offset of writing in the device */
};

static void iwl_free_fw_desc(struct iwl_drv* drv, struct fw_desc* desc) {
  vfree(desc->data);
  desc->data = NULL;
  desc->len = 0;
}

static void iwl_free_fw_img(struct iwl_drv* drv, struct fw_img* img) {
  int i;
  for (i = 0; i < img->num_sec; i++) {
    iwl_free_fw_desc(drv, &img->sec[i]);
  }
  kfree(img->sec);
}

static void iwl_dealloc_ucode(struct iwl_drv* drv) {
  size_t i;

  kfree(drv->fw.dbg.dest_tlv);
  for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.conf_tlv); i++) {
    kfree(drv->fw.dbg.conf_tlv[i]);
  }
  for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.trigger_tlv); i++) {
    kfree(drv->fw.dbg.trigger_tlv[i]);
  }
  kfree(drv->fw.dbg.mem_tlv);
  kfree(drv->fw.iml);

  for (i = 0; i < IWL_UCODE_TYPE_MAX; i++) {
    iwl_free_fw_img(drv, drv->fw.img + i);
  }
}

static zx_status_t iwl_alloc_fw_desc(struct iwl_drv* drv, struct fw_desc* desc,
                                     struct fw_sec* sec) {
  void* data;

  desc->data = NULL;

  if (!sec || !sec->size) {
    return ZX_ERR_INVALID_ARGS;
  }

  data = vmalloc(sec->size);
  if (!data) {
    return ZX_ERR_NO_MEMORY;
  }

  desc->len = (uint32_t)sec->size;
  desc->offset = sec->offset;
  memcpy(data, sec->data, desc->len);
  desc->data = data;

  return 0;
}

static void iwl_req_fw_callback(struct firmware* ucode_raw, void* context);

static zx_status_t iwl_load_firmware(struct iwl_drv* drv, bool first) {
  const struct iwl_cfg* cfg = drv->trans->cfg;
  char tag[8];
#if defined(CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES)
  char fw_name_temp[64];
#endif

  if (drv->trans->cfg->device_family == IWL_DEVICE_FAMILY_9000 &&
      (CSR_HW_REV_STEP(drv->trans->hw_rev) != SILICON_B_STEP &&
       CSR_HW_REV_STEP(drv->trans->hw_rev) != SILICON_C_STEP)) {
    IWL_ERR(drv, "Only HW steps B and C are currently supported (0x%0x)\n", drv->trans->hw_rev);
    return ZX_ERR_INVALID_ARGS;
  }

  if (first) {
    drv->fw_index = cfg->ucode_api_max;
    sprintf(tag, "%d", drv->fw_index);
  } else {
    drv->fw_index--;
    sprintf(tag, "%d", drv->fw_index);
  }

#ifdef CPTCFG_IWLWIFI_DISALLOW_OLDER_FW
  /* The dbg-cfg check here works because the first time we get
   * here we always load the 'api_max' version, and once that
   * has returned we load the dbg-cfg file.
   */
  if ((drv->fw_index != cfg->ucode_api_max
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
       && !drv->trans->dbg_cfg.load_old_fw
#endif
       ) ||
      drv->fw_index < cfg->ucode_api_min) {
#else
  if (drv->fw_index < cfg->ucode_api_min) {
#endif
    IWL_ERR(drv, "no suitable firmware found!\n");

    if (cfg->ucode_api_min == cfg->ucode_api_max) {
      IWL_ERR(drv, "%s%d is required\n", cfg->fw_name_pre, cfg->ucode_api_max);
    } else {
      IWL_ERR(drv, "minimum version required: %s%d\n", cfg->fw_name_pre, cfg->ucode_api_min);
      IWL_ERR(drv, "maximum version supported: %s%d\n", cfg->fw_name_pre, cfg->ucode_api_max);
    }

    IWL_ERR(drv,
            "check git://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git\n");
    return ZX_ERR_NOT_FOUND;
  }

  snprintf(drv->firmware_name, sizeof(drv->firmware_name), "%s/%s%s.ucode", FIRMWARE_DIR,
           cfg->fw_name_pre, tag);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (drv->trans->dbg_cfg.fw_file_pre) {
    snprintf(fw_name_temp, sizeof(fw_name_temp), "%s%s", drv->trans->dbg_cfg.fw_file_pre,
             drv->firmware_name);
    strncpy(drv->firmware_name, fw_name_temp, sizeof(drv->firmware_name));
  }
#endif /* CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES */

  IWL_INFO(drv, "attempting to load firmware '%s'\n", drv->firmware_name);

  // If we cannot load the firmware file (e.g. the specified version is not included on the system),
  // try the next (lower version) firmware file.
  zx_status_t ret =
      iwl_firmware_request_nowait(drv->trans->dev, drv->firmware_name, iwl_req_fw_callback, drv);
  if (ret == ZX_ERR_NOT_FOUND) {
    IWL_DEBUG_FW(drv, "cannot load the firmware file '%s'. Try next firmware version.\n",
                 drv->firmware_name);
    return iwl_load_firmware(drv, false);
  } else {
    return ret;
  }
}

struct fw_img_parsing {
  struct fw_sec* sec;
  int sec_counter;
};

/*
 * struct fw_sec_parsing: to extract fw section and it's offset from tlv
 */
struct fw_sec_parsing {
  __le32 offset;
  const uint8_t data[];
} __packed;

/**
 * struct iwl_tlv_calib_data - parse the default calib data from TLV
 *
 * @ucode_type: the uCode to which the following default calib relates.
 * @calib: default calibrations.
 */
struct iwl_tlv_calib_data {
  __le32 ucode_type;
  struct iwl_tlv_calib_ctrl calib;
} __packed;

struct iwl_firmware_pieces {
  struct fw_img_parsing img[IWL_UCODE_TYPE_MAX];

  uint32_t init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
  uint32_t inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;

  /* FW debug data parsed for driver usage */
  bool dbg_dest_tlv_init;
  uint8_t* dbg_dest_ver;
  union {
    struct iwl_fw_dbg_dest_tlv* dbg_dest_tlv;
    struct iwl_fw_dbg_dest_tlv_v1* dbg_dest_tlv_v1;
  };
  struct iwl_fw_dbg_conf_tlv* dbg_conf_tlv[FW_DBG_CONF_MAX];
  size_t dbg_conf_tlv_len[FW_DBG_CONF_MAX];
  struct iwl_fw_dbg_trigger_tlv* dbg_trigger_tlv[FW_DBG_TRIGGER_MAX];
  size_t dbg_trigger_tlv_len[FW_DBG_TRIGGER_MAX];
  struct iwl_fw_dbg_mem_seg_tlv* dbg_mem_tlv;
  size_t n_mem_tlv;
};

/*
 * These functions are just to extract uCode section data from the pieces
 * structure.
 */
static struct fw_sec* get_sec(struct iwl_firmware_pieces* pieces, enum iwl_ucode_type type,
                              int sec) {
  return &pieces->img[type].sec[sec];
}

static void alloc_sec_data(struct iwl_firmware_pieces* pieces, enum iwl_ucode_type type, int sec) {
  struct fw_img_parsing* img = &pieces->img[type];
  struct fw_sec* sec_memory;
  int size = sec + 1;
  size_t alloc_size = sizeof(*img->sec) * size;

  if (img->sec && img->sec_counter >= size) {
    return;
  }

  sec_memory = realloc(img->sec, alloc_size);
  if (!sec_memory) {
    return;
  }

  img->sec = sec_memory;
  img->sec_counter = size;
}

static void set_sec_data(struct iwl_firmware_pieces* pieces, enum iwl_ucode_type type, int sec,
                         const void* data) {
  alloc_sec_data(pieces, type, sec);

  pieces->img[type].sec[sec].data = data;
}

static void set_sec_size(struct iwl_firmware_pieces* pieces, enum iwl_ucode_type type, int sec,
                         size_t size) {
  alloc_sec_data(pieces, type, sec);

  pieces->img[type].sec[sec].size = size;
}

static size_t get_sec_size(struct iwl_firmware_pieces* pieces, enum iwl_ucode_type type, int sec) {
  return pieces->img[type].sec[sec].size;
}

static void set_sec_offset(struct iwl_firmware_pieces* pieces, enum iwl_ucode_type type, int sec,
                           uint32_t offset) {
  alloc_sec_data(pieces, type, sec);

  pieces->img[type].sec[sec].offset = offset;
}

static zx_status_t iwl_store_cscheme(struct iwl_fw* fw, const uint8_t* data, const uint32_t len) {
  int i, j;
  struct iwl_fw_cscheme_list* l = (struct iwl_fw_cscheme_list*)data;
  struct iwl_fw_cipher_scheme* fwcs;

  if (len < sizeof(*l) || len < sizeof(l->size) + l->size * sizeof(l->cs[0])) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (i = 0, j = 0; i < IWL_UCODE_MAX_CS && i < l->size; i++) {
    fwcs = &l->cs[j];

    /* we skip schemes with zero cipher suite selector */
    if (!fwcs->cipher) {
      continue;
    }

    fw->cs[j++] = *fwcs;
  }

  return 0;
}

/*
 * Gets uCode section from tlv.
 */
static int iwl_store_ucode_sec(struct iwl_firmware_pieces* pieces, const void* data,
                               enum iwl_ucode_type type, int size) {
  struct fw_img_parsing* img;
  struct fw_sec* sec;
  struct fw_sec_parsing* sec_parse;
  size_t alloc_size;

  if (WARN_ON(!pieces || !data || type >= IWL_UCODE_TYPE_MAX)) {
    return -1;
  }

  sec_parse = (struct fw_sec_parsing*)data;

  img = &pieces->img[type];

  alloc_size = sizeof(*img->sec) * (img->sec_counter + 1);
  sec = realloc(img->sec, alloc_size);
  if (!sec) {
    return ZX_ERR_NO_MEMORY;
  }
  img->sec = sec;

  sec = &img->sec[img->sec_counter];

  sec->offset = le32_to_cpu(sec_parse->offset);
  sec->data = sec_parse->data;
  sec->size = size - sizeof(sec_parse->offset);

  ++img->sec_counter;

  return 0;
}

static zx_status_t iwl_set_default_calib(struct iwl_drv* drv, const uint8_t* data) {
  struct iwl_tlv_calib_data* def_calib = (struct iwl_tlv_calib_data*)data;
  uint32_t ucode_type = le32_to_cpu(def_calib->ucode_type);
  if (ucode_type >= IWL_UCODE_TYPE_MAX) {
    IWL_ERR(drv, "Wrong ucode_type %u for default calibration.\n", ucode_type);
    return ZX_ERR_INVALID_ARGS;
  }
  drv->fw.default_calib[ucode_type].flow_trigger = def_calib->calib.flow_trigger;
  drv->fw.default_calib[ucode_type].event_trigger = def_calib->calib.event_trigger;

  return 0;
}

static void iwl_set_ucode_api_flags(struct iwl_drv* drv, const uint8_t* data,
                                    struct iwl_ucode_capabilities* capa) {
  const struct iwl_ucode_api* ucode_api = (void*)data;
  uint32_t api_index = le32_to_cpu(ucode_api->api_index);
  uint32_t api_flags = le32_to_cpu(ucode_api->api_flags);
  int i;

  if (api_index >= DIV_ROUND_UP(NUM_IWL_UCODE_TLV_API, 32)) {
    IWL_WARN(drv, "api flags index %d larger than supported by driver\n", api_index);
    return;
  }

  for (i = 0; i < 32; i++) {
    if (api_flags & BIT(i)) {
      __set_bit(i + 32 * api_index, capa->_api);
    }
  }
}

static void iwl_set_ucode_capabilities(struct iwl_drv* drv, const uint8_t* data,
                                       struct iwl_ucode_capabilities* capa) {
  const struct iwl_ucode_capa* ucode_capa = (void*)data;
  uint32_t api_index = le32_to_cpu(ucode_capa->api_index);
  uint32_t api_flags = le32_to_cpu(ucode_capa->api_capa);
  int i;

  if (api_index >= DIV_ROUND_UP(NUM_IWL_UCODE_TLV_CAPA, 32)) {
    IWL_WARN(drv, "capa flags index %d larger than supported by driver\n", api_index);
    return;
  }

  for (i = 0; i < 32; i++) {
    if (api_flags & BIT(i)) {
      __set_bit(i + 32 * api_index, capa->_capa);
    }
  }
}

static zx_status_t iwl_parse_v1_v2_firmware(struct iwl_drv* drv, const struct firmware* ucode_raw,
                                            struct iwl_firmware_pieces* pieces) {
  struct iwl_ucode_header* ucode = (void*)ucode_raw->data;
  uint32_t api_ver, hdr_size, build;
  char buildstr[25];
  const uint8_t* src;

  drv->fw.ucode_ver = le32_to_cpu(ucode->ver);
  api_ver = IWL_UCODE_API(drv->fw.ucode_ver);

  switch (api_ver) {
    default:
      hdr_size = 28;
      if (ucode_raw->size < hdr_size) {
        IWL_ERR(drv, "File size too small!\n");
        return ZX_ERR_INVALID_ARGS;
      }
      build = le32_to_cpu(ucode->u.v2.build);
      set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
                   le32_to_cpu(ucode->u.v2.inst_size));
      set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
                   le32_to_cpu(ucode->u.v2.data_size));
      set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
                   le32_to_cpu(ucode->u.v2.init_size));
      set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
                   le32_to_cpu(ucode->u.v2.init_data_size));
      src = ucode->u.v2.data;
      break;
    case 0:
    case 1:
    case 2:
      hdr_size = 24;
      if (ucode_raw->size < hdr_size) {
        IWL_ERR(drv, "File size too small!\n");
        return ZX_ERR_INVALID_ARGS;
      }
      build = 0;
      set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
                   le32_to_cpu(ucode->u.v1.inst_size));
      set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
                   le32_to_cpu(ucode->u.v1.data_size));
      set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
                   le32_to_cpu(ucode->u.v1.init_size));
      set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
                   le32_to_cpu(ucode->u.v1.init_data_size));
      src = ucode->u.v1.data;
      break;
  }

  if (build) {
    sprintf(buildstr, " build %u", build);
  } else {
    buildstr[0] = '\0';
  }

  snprintf(drv->fw.fw_version, sizeof(drv->fw.fw_version), "%u.%u.%u.%u%s",
           IWL_UCODE_MAJOR(drv->fw.ucode_ver), IWL_UCODE_MINOR(drv->fw.ucode_ver),
           IWL_UCODE_API(drv->fw.ucode_ver), IWL_UCODE_SERIAL(drv->fw.ucode_ver), buildstr);

  /* Verify size of file vs. image size info in file's header */

  if (ucode_raw->size != hdr_size +
                             get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) +
                             get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) +
                             get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST) +
                             get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA)) {
    IWL_ERR(drv, "uCode file size %d does not match expected size\n", (int)ucode_raw->size);
    return ZX_ERR_INVALID_ARGS;
  }

  set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST, src);
  src += get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST);
  set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST, IWLAGN_RTC_INST_LOWER_BOUND);
  set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA, src);
  src += get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA);
  set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA, IWLAGN_RTC_DATA_LOWER_BOUND);
  set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, src);
  src += get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST);
  set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, IWLAGN_RTC_INST_LOWER_BOUND);
  set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, src);
  src += get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA);
  set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, IWLAGN_RTC_DATA_LOWER_BOUND);
  return 0;
}

static zx_status_t iwl_parse_tlv_firmware(struct iwl_drv* drv, const struct firmware* ucode_raw,
                                          struct iwl_firmware_pieces* pieces,
                                          struct iwl_ucode_capabilities* capa,
                                          bool* usniffer_images) {
  struct iwl_tlv_ucode_header* ucode = (void*)ucode_raw->data;
  struct iwl_ucode_tlv* tlv;
  size_t len = ucode_raw->size;
  const uint8_t* data;
  uint32_t tlv_len;
  uint32_t usniffer_img;
  enum iwl_ucode_tlv_type tlv_type;
  const uint8_t* tlv_data;
  char buildstr[25];
  uint32_t build, paging_mem_size;
  int num_of_cpus;
  bool usniffer_req = false;

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (ucode->magic == cpu_to_le32(IWL_TLV_FW_DBG_MAGIC)) {
    size_t dbg_data_ofs = offsetof(struct iwl_tlv_ucode_header, human_readable);
    data = (void*)ucode_raw->data + dbg_data_ofs;
    len -= dbg_data_ofs;

    goto fw_dbg_conf;
  }
#endif

  if (len < sizeof(*ucode)) {
    IWL_ERR(drv, "uCode has invalid length: %zd\n", len);
    return ZX_ERR_INVALID_ARGS;
  }

  if (ucode->magic != (__le32)cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
    IWL_ERR(drv, "invalid uCode magic: 0X%x\n", le32_to_cpu(ucode->magic));
    return ZX_ERR_INVALID_ARGS;
  }

  drv->fw.ucode_ver = le32_to_cpu(ucode->ver);
  memcpy(drv->fw.human_readable, ucode->human_readable, sizeof(drv->fw.human_readable));
  build = le32_to_cpu(ucode->build);

  if (build) {
    sprintf(buildstr, " build %u", build);
  } else {
    buildstr[0] = '\0';
  }

  snprintf(drv->fw.fw_version, sizeof(drv->fw.fw_version), "%u.%u.%u.%u%s",
           IWL_UCODE_MAJOR(drv->fw.ucode_ver), IWL_UCODE_MINOR(drv->fw.ucode_ver),
           IWL_UCODE_API(drv->fw.ucode_ver), IWL_UCODE_SERIAL(drv->fw.ucode_ver), buildstr);

  data = ucode->data;

  len -= sizeof(*ucode);

  while (len >= sizeof(*tlv)) {
    len -= sizeof(*tlv);
    tlv = (void*)data;

    tlv_len = le32_to_cpu(tlv->length);
    tlv_type = le32_to_cpu(tlv->type);
    tlv_data = tlv->data;

    if (len < tlv_len) {
      IWL_ERR(drv, "invalid TLV len: %zd/%u\n", len, tlv_len);
      return ZX_ERR_INVALID_ARGS;
    }
    len -= IWL_ALIGN(tlv_len, 4);
    data += sizeof(*tlv) + IWL_ALIGN(tlv_len, 4);

    switch (tlv_type) {
      case IWL_UCODE_TLV_INST:
        set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST, tlv_data);
        set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST, tlv_len);
        set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
                       IWLAGN_RTC_INST_LOWER_BOUND);
        break;
      case IWL_UCODE_TLV_DATA:
        set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA, tlv_data);
        set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA, tlv_len);
        set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
                       IWLAGN_RTC_DATA_LOWER_BOUND);
        break;
      case IWL_UCODE_TLV_INIT:
        set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, tlv_data);
        set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, tlv_len);
        set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, IWLAGN_RTC_INST_LOWER_BOUND);
        break;
      case IWL_UCODE_TLV_INIT_DATA:
        set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, tlv_data);
        set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, tlv_len);
        set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, IWLAGN_RTC_DATA_LOWER_BOUND);
        break;
      case IWL_UCODE_TLV_BOOT:
        IWL_ERR(drv, "Found unexpected BOOT ucode\n");
        break;
      case IWL_UCODE_TLV_PROBE_MAX_LEN:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        capa->max_probe_length = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_PAN:
        if (tlv_len) {
          goto invalid_tlv_len;
        }
        capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
        break;
      case IWL_UCODE_TLV_FLAGS:
        /* must be at least one uint32_t */
        if (tlv_len < sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        /* and a proper number of u32s */
        if (tlv_len % sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        /*
         * This driver only reads the first uint32_t as
         * right now no more features are defined,
         * if that changes then either the driver
         * will not work with the new firmware, or
         * it'll not take advantage of new features.
         */
        capa->flags = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_API_CHANGES_SET:
        if (tlv_len != sizeof(struct iwl_ucode_api)) {
          goto invalid_tlv_len;
        }
        iwl_set_ucode_api_flags(drv, tlv_data, capa);
        break;
      case IWL_UCODE_TLV_ENABLED_CAPABILITIES:
        if (tlv_len != sizeof(struct iwl_ucode_capa)) {
          goto invalid_tlv_len;
        }
        iwl_set_ucode_capabilities(drv, tlv_data, capa);
        break;
      case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        pieces->init_evtlog_ptr = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        pieces->init_evtlog_size = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        pieces->init_errlog_ptr = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        pieces->inst_evtlog_ptr = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        pieces->inst_evtlog_size = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        pieces->inst_errlog_ptr = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
        if (tlv_len) {
          goto invalid_tlv_len;
        }
        drv->fw.enhance_sensitivity_table = true;
        break;
      case IWL_UCODE_TLV_WOWLAN_INST:
        set_sec_data(pieces, IWL_UCODE_WOWLAN, IWL_UCODE_SECTION_INST, tlv_data);
        set_sec_size(pieces, IWL_UCODE_WOWLAN, IWL_UCODE_SECTION_INST, tlv_len);
        set_sec_offset(pieces, IWL_UCODE_WOWLAN, IWL_UCODE_SECTION_INST,
                       IWLAGN_RTC_INST_LOWER_BOUND);
        break;
      case IWL_UCODE_TLV_WOWLAN_DATA:
        set_sec_data(pieces, IWL_UCODE_WOWLAN, IWL_UCODE_SECTION_DATA, tlv_data);
        set_sec_size(pieces, IWL_UCODE_WOWLAN, IWL_UCODE_SECTION_DATA, tlv_len);
        set_sec_offset(pieces, IWL_UCODE_WOWLAN, IWL_UCODE_SECTION_DATA,
                       IWLAGN_RTC_DATA_LOWER_BOUND);
        break;
      case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        capa->standard_phy_calibration_size = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_SEC_RT:
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_REGULAR, tlv_len);
        drv->fw.type = IWL_FW_MVM;
        break;
      case IWL_UCODE_TLV_SEC_INIT:
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_INIT, tlv_len);
        drv->fw.type = IWL_FW_MVM;
        break;
      case IWL_UCODE_TLV_SEC_WOWLAN:
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_WOWLAN, tlv_len);
        drv->fw.type = IWL_FW_MVM;
        break;
      case IWL_UCODE_TLV_DEF_CALIB:
        if (tlv_len != sizeof(struct iwl_tlv_calib_data)) {
          goto invalid_tlv_len;
        }
        if (iwl_set_default_calib(drv, tlv_data)) {
          goto tlv_error;
        }
        break;
      case IWL_UCODE_TLV_PHY_SKU:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        drv->fw.phy_config = le32_to_cpup((__le32*)tlv_data);
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
        if (drv->trans->dbg_cfg.valid_ants & ~ANT_ABC) {
          IWL_ERR(drv, "Invalid value for antennas: 0x%x\n", drv->trans->dbg_cfg.valid_ants);
        }
        /* Make sure value stays in range */
        drv->trans->dbg_cfg.valid_ants &= ANT_ABC;
        if (drv->trans->dbg_cfg.valid_ants) {
          uint32_t phy_config = ~(FW_PHY_CFG_TX_CHAIN | FW_PHY_CFG_RX_CHAIN);

          phy_config |= (drv->trans->dbg_cfg.valid_ants << FW_PHY_CFG_TX_CHAIN_POS);
          phy_config |= (drv->trans->dbg_cfg.valid_ants << FW_PHY_CFG_RX_CHAIN_POS);

          drv->fw.phy_config &= phy_config;
        }
#endif
        drv->fw.valid_tx_ant =
            (drv->fw.phy_config & FW_PHY_CFG_TX_CHAIN) >> FW_PHY_CFG_TX_CHAIN_POS;
        drv->fw.valid_rx_ant =
            (drv->fw.phy_config & FW_PHY_CFG_RX_CHAIN) >> FW_PHY_CFG_RX_CHAIN_POS;
        break;
      case IWL_UCODE_TLV_SECURE_SEC_RT:
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_REGULAR, tlv_len);
        drv->fw.type = IWL_FW_MVM;
        break;
      case IWL_UCODE_TLV_SECURE_SEC_INIT:
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_INIT, tlv_len);
        drv->fw.type = IWL_FW_MVM;
        break;
      case IWL_UCODE_TLV_SECURE_SEC_WOWLAN:
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_WOWLAN, tlv_len);
        drv->fw.type = IWL_FW_MVM;
        break;
      case IWL_UCODE_TLV_NUM_OF_CPU:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        num_of_cpus = le32_to_cpup((__le32*)tlv_data);

        if (num_of_cpus == 2) {
          drv->fw.img[IWL_UCODE_REGULAR].is_dual_cpus = true;
          drv->fw.img[IWL_UCODE_INIT].is_dual_cpus = true;
          drv->fw.img[IWL_UCODE_WOWLAN].is_dual_cpus = true;
        } else if ((num_of_cpus > 2) || (num_of_cpus < 1)) {
          IWL_ERR(drv, "Driver support upto 2 CPUs\n");
          return ZX_ERR_INVALID_ARGS;
        }
        break;
      case IWL_UCODE_TLV_CSCHEME:
        if (iwl_store_cscheme(&drv->fw, tlv_data, tlv_len)) {
          goto invalid_tlv_len;
        }
        break;
      case IWL_UCODE_TLV_N_SCAN_CHANNELS:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        capa->n_scan_channels = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_FW_VERSION: {
        __le32* ptr = (void*)tlv_data;
        uint32_t major, minor;
        uint8_t local_comp;

        if (tlv_len != sizeof(uint32_t) * 3) {
          goto invalid_tlv_len;
        }

        major = le32_to_cpup(ptr++);
        minor = le32_to_cpup(ptr++);
        local_comp = (uint8_t)le32_to_cpup(ptr);

        if (strncmp((const char*)drv->fw.human_readable, "stream:", 7))
          snprintf(drv->fw.fw_version, sizeof(drv->fw.fw_version), "%u.%08x.%hhu", major, minor,
                   local_comp);
        else
          snprintf(drv->fw.fw_version, sizeof(drv->fw.fw_version), "%u.%u.%hhu", major, minor,
                   local_comp);
        break;
      }
      case IWL_UCODE_TLV_FW_DBG_DEST: {
        struct iwl_fw_dbg_dest_tlv* dest = NULL;
        struct iwl_fw_dbg_dest_tlv_v1* dest_v1 = NULL;
        uint8_t mon_mode;

        pieces->dbg_dest_ver = (uint8_t*)tlv_data;
        if (*pieces->dbg_dest_ver == 1) {
          dest = (void*)tlv_data;
        } else if (*pieces->dbg_dest_ver == 0) {
          dest_v1 = (void*)tlv_data;
        } else {
          IWL_ERR(drv, "The version is %d, and it is invalid\n", *pieces->dbg_dest_ver);
          break;
        }

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
        if (drv->trans->dbg_cfg.dbm_destination_path) {
          IWL_ERR(drv, "Ignoring destination, ini file present\n");
          break;
        }
#endif
#endif

        if (pieces->dbg_dest_tlv_init) {
          IWL_ERR(drv, "dbg destination ignored, already exists\n");
          break;
        }

        pieces->dbg_dest_tlv_init = true;

        if (dest_v1) {
          pieces->dbg_dest_tlv_v1 = dest_v1;
          mon_mode = dest_v1->monitor_mode;
        } else {
          pieces->dbg_dest_tlv = dest;
          mon_mode = dest->monitor_mode;
        }

        IWL_INFO(drv, "Found debug destination: %s\n", get_fw_dbg_mode_string(mon_mode));

        drv->fw.dbg.n_dest_reg =
            (uint8_t)((dest_v1) ? tlv_len - offsetof(struct iwl_fw_dbg_dest_tlv_v1, reg_ops)
                                : tlv_len - offsetof(struct iwl_fw_dbg_dest_tlv, reg_ops));

        drv->fw.dbg.n_dest_reg /= sizeof(drv->fw.dbg.dest_tlv->reg_ops[0]);

        break;
      }
      case IWL_UCODE_TLV_FW_DBG_CONF: {
        struct iwl_fw_dbg_conf_tlv* conf = (void*)tlv_data;

        if (!pieces->dbg_dest_tlv_init) {
          IWL_ERR(drv, "Ignore dbg config %d - no destination configured\n", conf->id);
          break;
        }

        if (conf->id >= ARRAY_SIZE(drv->fw.dbg.conf_tlv)) {
          IWL_ERR(drv, "Skip unknown configuration: %d\n", conf->id);
          break;
        }

        if (pieces->dbg_conf_tlv[conf->id]) {
          IWL_ERR(drv, "Ignore duplicate dbg config %d\n", conf->id);
          break;
        }

        if (conf->usniffer) {
          usniffer_req = true;
        }

        IWL_INFO(drv, "Found debug configuration: %d\n", conf->id);

        pieces->dbg_conf_tlv[conf->id] = conf;
        pieces->dbg_conf_tlv_len[conf->id] = tlv_len;
        break;
      }
      case IWL_UCODE_TLV_FW_DBG_TRIGGER: {
        struct iwl_fw_dbg_trigger_tlv* trigger = (void*)tlv_data;
        uint32_t trigger_id = le32_to_cpu(trigger->id);

        if (trigger_id >= ARRAY_SIZE(drv->fw.dbg.trigger_tlv)) {
          IWL_ERR(drv, "Skip unknown trigger: %u\n", trigger->id);
          break;
        }

        if (pieces->dbg_trigger_tlv[trigger_id]) {
          IWL_ERR(drv, "Ignore duplicate dbg trigger %u\n", trigger->id);
          break;
        }

        IWL_INFO(drv, "Found debug trigger: %u\n", trigger->id);

        pieces->dbg_trigger_tlv[trigger_id] = trigger;
        pieces->dbg_trigger_tlv_len[trigger_id] = tlv_len;
        break;
      }
      case IWL_UCODE_TLV_FW_DBG_DUMP_LST: {
        if (tlv_len != sizeof(uint32_t)) {
          IWL_ERR(drv, "dbg lst mask size incorrect, skip\n");
          break;
        }

        drv->fw.dbg.dump_mask = le32_to_cpup((__le32*)tlv_data);
        break;
      }
      case IWL_UCODE_TLV_SEC_RT_USNIFFER:
        *usniffer_images = true;
        iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_REGULAR_USNIFFER, tlv_len);
        break;
      case IWL_UCODE_TLV_PAGING:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        paging_mem_size = le32_to_cpup((__le32*)tlv_data);

        IWL_DEBUG_FW(drv, "Paging: paging enabled (size = %u bytes)\n", paging_mem_size);

        if (paging_mem_size > MAX_PAGING_IMAGE_SIZE) {
          IWL_ERR(drv, "Paging: driver supports up to %lu bytes for paging image\n",
                  MAX_PAGING_IMAGE_SIZE);
          return ZX_ERR_INVALID_ARGS;
        }

        if (paging_mem_size & (FW_PAGING_SIZE - 1)) {
          IWL_ERR(drv, "Paging: image isn't multiple %lu\n", FW_PAGING_SIZE);
          return ZX_ERR_INVALID_ARGS;
        }

        drv->fw.img[IWL_UCODE_REGULAR].paging_mem_size = paging_mem_size;
        usniffer_img = IWL_UCODE_REGULAR_USNIFFER;
        drv->fw.img[usniffer_img].paging_mem_size = paging_mem_size;
        break;
      case IWL_UCODE_TLV_FW_GSCAN_CAPA:
        /* ignored */
        break;
      case IWL_UCODE_TLV_FW_MEM_SEG: {
        struct iwl_fw_dbg_mem_seg_tlv* dbg_mem = (void*)tlv_data;
        size_t size;
        struct iwl_fw_dbg_mem_seg_tlv* n;

        if (tlv_len != (sizeof(*dbg_mem))) {
          goto invalid_tlv_len;
        }

        IWL_DEBUG_INFO(drv, "Found debug memory segment: %u\n", dbg_mem->data_type);

        size = sizeof(*pieces->dbg_mem_tlv) * (pieces->n_mem_tlv + 1);
        n = realloc(pieces->dbg_mem_tlv, size);
        if (!n) {
          return ZX_ERR_NO_MEMORY;
        }
        pieces->dbg_mem_tlv = n;
        pieces->dbg_mem_tlv[pieces->n_mem_tlv] = *dbg_mem;
        pieces->n_mem_tlv++;
        break;
      }
      case IWL_UCODE_TLV_IML: {
        drv->fw.iml_len = tlv_len;
        drv->fw.iml = kmemdup(tlv_data, tlv_len);
        if (!drv->fw.iml) {
          return ZX_ERR_NO_MEMORY;
        }
        break;
      }
#if IS_ENABLED(CPTCFG_IWLFMAC)
      case IWL_UCODE_TLV_FW_FMAC_API_VERSION:
        if (tlv_len != sizeof(uint32_t)) {
          goto invalid_tlv_len;
        }
        capa->fmac_api_version = le32_to_cpup((__le32*)tlv_data);
        break;
      case IWL_UCODE_TLV_FW_FMAC_RECOVERY_INFO: {
        struct {
          __le32 buf_addr;
          __le32 buf_size;
        }* recov_info = (void*)tlv_data;

        if (tlv_len != sizeof(*recov_info)) {
          goto invalid_tlv_len;
        }
        capa->fmac_error_log_addr = le32_to_cpu(recov_info->buf_addr);
        capa->fmac_error_log_size = le32_to_cpu(recov_info->buf_size);
      } break;
#endif
      case IWL_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
      case IWL_UCODE_TLV_TYPE_HCMD:
      case IWL_UCODE_TLV_TYPE_REGIONS:
      case IWL_UCODE_TLV_TYPE_TRIGGERS:
      case IWL_UCODE_TLV_TYPE_DEBUG_FLOW:
#if 0   // NEEDS_PORTING
            if (iwlwifi_mod_params.enable_ini) {
				iwl_dbg_tlv_alloc(drv->trans, tlv, false);
            }
            break;
		case IWL_UCODE_TLV_CMD_VERSIONS:
			if (tlv_len % sizeof(struct iwl_fw_cmd_version)) {
				IWL_ERR(drv,
					"Invalid length for command versions: %u\n",
					tlv_len);
				tlv_len /= sizeof(struct iwl_fw_cmd_version);
				tlv_len *= sizeof(struct iwl_fw_cmd_version);
			}
			if (WARN_ON(capa->cmd_versions))
				return -EINVAL;
			capa->cmd_versions = kmemdup(tlv_data, tlv_len,
						     GFP_KERNEL);
			if (!capa->cmd_versions)
				return -ENOMEM;
			capa->n_cmd_versions =
				tlv_len / sizeof(struct iwl_fw_cmd_version);
			break;
		case IWL_UCODE_TLV_PHY_INTEGRATION_VERSION:
			if (drv->fw.phy_integration_ver) {
				IWL_ERR(drv,
					"phy integration str ignored, already exists\n");
				break;
			}

			drv->fw.phy_integration_ver =
				kmemdup(tlv_data, tlv_len, GFP_KERNEL);
			if (!drv->fw.phy_integration_ver)
				return -ENOMEM;
			drv->fw.phy_integration_ver_len = tlv_len;
			break;
		case IWL_UCODE_TLV_SEC_TABLE_ADDR:
		case IWL_UCODE_TLV_D3_KEK_KCK_ADDR:
			iwl_drv_set_dump_exclude(drv, tlv_type,
						 tlv_data, tlv_len);
			break;
#endif  // NEEDS_PORTING
      default:
        IWL_DEBUG_INFO(drv, "unknown TLV: %d\n", tlv_type);
        break;
    }
  }

  if (!fw_has_capa(capa, IWL_UCODE_TLV_CAPA_USNIFFER_UNIFIED) && usniffer_req &&
      !*usniffer_images) {
    IWL_ERR(drv,
            "user selected to work with usniffer but usniffer image isn't "
            "available in ucode "
            "package\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (len) {
    IWL_ERR(drv, "invalid TLV after parsing: %zd\n", len);
    iwl_print_hex_dump(drv, IWL_DL_FW, (uint8_t*)data, len);
    return ZX_ERR_INVALID_ARGS;
  }

#if IS_ENABLED(CPTCFG_IWLFMAC)
  if (fw_has_capa(capa, IWL_UCODE_TLV_CAPA_MLME_OFFLOAD)) {
    drv->fw.type = IWL_FW_FMAC;
  }
#endif

  return 0;

invalid_tlv_len:
  IWL_ERR(drv, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
tlv_error:
  iwl_print_hex_dump(drv, IWL_DL_FW, tlv_data, tlv_len);

  return ZX_ERR_INVALID_ARGS;
}

static int iwl_alloc_ucode(struct iwl_drv* drv, struct iwl_firmware_pieces* pieces,
                           enum iwl_ucode_type type) {
  int i;
  struct fw_desc* sec;

  sec = calloc(pieces->img[type].sec_counter, sizeof(*sec));
  if (!sec) {
    return ZX_ERR_NO_MEMORY;
  }
  drv->fw.img[type].sec = sec;
  drv->fw.img[type].num_sec = pieces->img[type].sec_counter;

  for (i = 0; i < pieces->img[type].sec_counter; i++)
    if (iwl_alloc_fw_desc(drv, &sec[i], get_sec(pieces, type, i))) {
      return ZX_ERR_NO_MEMORY;
    }

  return 0;
}

static int validate_sec_sizes(struct iwl_drv* drv, struct iwl_firmware_pieces* pieces,
                              const struct iwl_cfg* cfg) {
  IWL_DEBUG_INFO(drv, "f/w package hdr runtime inst size = %zd\n",
                 get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST));
  IWL_DEBUG_INFO(drv, "f/w package hdr runtime data size = %zd\n",
                 get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA));
  IWL_DEBUG_INFO(drv, "f/w package hdr init inst size = %zd\n",
                 get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST));
  IWL_DEBUG_INFO(drv, "f/w package hdr init data size = %zd\n",
                 get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA));

  /* Verify that uCode images will fit in card's SRAM. */
  if (get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) > cfg->max_inst_size) {
    IWL_ERR(drv, "uCode instr len %zd too large to fit in\n",
            get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST));
    return -1;
  }

  if (get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) > cfg->max_data_size) {
    IWL_ERR(drv, "uCode data len %zd too large to fit in\n",
            get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA));
    return -1;
  }

  if (get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST) > cfg->max_inst_size) {
    IWL_ERR(drv, "uCode init instr len %zd too large to fit in\n",
            get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST));
    return -1;
  }

  if (get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA) > cfg->max_data_size) {
    IWL_ERR(drv, "uCode init data len %zd too large to fit in\n",
            get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA));
    return -1;
  }
  return 0;
}

static struct iwl_op_mode* _iwl_op_mode_start(struct iwl_drv* drv,
                                              struct iwlwifi_opmode_table* op) {
  const struct iwl_op_mode_ops* ops = op->ops;
  struct dentry* dbgfs_dir = NULL;
  struct iwl_op_mode* op_mode = NULL;

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  drv->dbgfs_op_mode = debugfs_create_dir(op->name, drv->dbgfs_drv);
  if (!drv->dbgfs_op_mode) {
    IWL_ERR(drv, "failed to create opmode debugfs directory\n");
    return op_mode;
  }
  dbgfs_dir = drv->dbgfs_op_mode;
#endif

  op_mode = ops->start(drv->trans, drv->trans->cfg, &drv->fw, dbgfs_dir);

  if (!op_mode) {
#ifdef CPTCFG_IWLWIFI_DEBUGFS
    debugfs_remove_recursive(drv->dbgfs_op_mode);
    drv->dbgfs_op_mode = NULL;
    return NULL;
#endif
  }

  iwl_stats_init(drv->trans->dev->irq_dispatcher);
  iwl_stats_start_reporting();

  return op_mode;
}

static void _iwl_op_mode_stop(struct iwl_drv* drv) {
  /* op_mode can be NULL if its start failed */
  if (drv->op_mode) {
    iwl_op_mode_stop(drv->op_mode);
    drv->op_mode = NULL;

#ifdef CPTCFG_IWLWIFI_DEBUGFS
    debugfs_remove_recursive(drv->dbgfs_op_mode);
    drv->dbgfs_op_mode = NULL;
#endif
  }
}

/**
 * iwl_req_fw_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_req_fw_callback(struct firmware* ucode_raw, void* context) {
  struct iwl_drv* drv = context;
  struct iwl_fw* fw = &drv->fw;
  struct iwl_ucode_header* ucode;
  struct iwlwifi_opmode_table* op;
  int err;
  struct iwl_firmware_pieces* pieces;
  const unsigned int api_max = drv->trans->cfg->ucode_api_max;
  const unsigned int api_min = drv->trans->cfg->ucode_api_min;
  size_t trigger_tlv_sz[FW_DBG_TRIGGER_MAX];
  uint32_t api_ver;
  size_t i;
  bool load_module = false;
  bool usniffer_images = false;

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  const struct firmware* fw_dbg_config;
  int load_fw_dbg_err = ZX_ERR_NOT_FOUND;
#endif

  fw->ucode_capa.max_probe_length = IWL_DEFAULT_MAX_PROBE_LENGTH;
  fw->ucode_capa.standard_phy_calibration_size = IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE;
  fw->ucode_capa.n_scan_channels = IWL_DEFAULT_SCAN_CHANNELS;
  /* dump all fw memory areas by default except d3 debug data */
  fw->dbg.dump_mask = 0xfffdffff;

  pieces = calloc(1, sizeof(*pieces));
  if (!pieces) {
    goto out_free_fw;
  }

  if (!ucode_raw) {
    goto try_again;
  }

  IWL_DEBUG_INFO(drv, "Loaded firmware file '%s' (%zd bytes).\n", drv->firmware_name,
                 ucode_raw->size);

  /* Make sure that we got at least the API version number */
  if (ucode_raw->size < 4) {
    IWL_ERR(drv, "File size way too small!\n");
    goto try_again;
  }

  /* Data from ucode file:  header followed by uCode images */
  ucode = (struct iwl_ucode_header*)ucode_raw->data;

  if (ucode->ver) {
    err = iwl_parse_v1_v2_firmware(drv, ucode_raw, pieces);
  } else {
    err = iwl_parse_tlv_firmware(drv, ucode_raw, pieces, &fw->ucode_capa, &usniffer_images);
  }

  if (err) {
    goto try_again;
  }

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (!ucode->ver && drv->trans->dbg_cfg.fw_dbg_conf) {
    load_fw_dbg_err =
        iwl_firmware_request(drv->trans->dev, &fw_dbg_config, drv->trans->dbg_cfg.fw_dbg_conf);
    if (!load_fw_dbg_err) {
      err = iwl_parse_tlv_firmware(drv, fw_dbg_config, pieces, &fw->ucode_capa, &usniffer_images);
      if (err) {
        IWL_ERR(drv, "Failed to configure FW DBG data!\n");
      }
    }
  }
#endif

  if (fw_has_api(&drv->fw.ucode_capa, IWL_UCODE_TLV_API_NEW_VERSION)) {
    api_ver = drv->fw.ucode_ver;
  } else {
    api_ver = IWL_UCODE_API(drv->fw.ucode_ver);
  }

  /*
   * api_ver should match the api version forming part of the
   * firmware filename ... but we don't check for that and only rely
   * on the API version read from firmware header from here on forward
   */
  if (api_ver < api_min || api_ver > api_max) {
    IWL_ERR(drv,
            "Driver unable to support your firmware API. "
            "Driver supports v%u, firmware is v%u.\n",
            api_max, api_ver);
    goto try_again;
  }

  /*
   * In mvm uCode there is no difference between data and instructions
   * sections.
   */
  if (fw->type == IWL_FW_DVM && validate_sec_sizes(drv, pieces, drv->trans->cfg)) {
    goto try_again;
  }

  /* Allocate ucode buffers for card's bus-master loading ... */

  /* Runtime instructions and 2 copies of data:
   * 1) unmodified from disk
   * 2) backup cache for save/restore during power-downs
   */
  for (i = 0; i < IWL_UCODE_TYPE_MAX; i++)
    if (iwl_alloc_ucode(drv, pieces, (enum iwl_ucode_type)i)) {
      goto out_free_fw;
    }

  if (pieces->dbg_dest_tlv_init) {
    size_t dbg_dest_size = sizeof(*drv->fw.dbg.dest_tlv) +
                           sizeof(drv->fw.dbg.dest_tlv->reg_ops[0]) * drv->fw.dbg.n_dest_reg;

    drv->fw.dbg.dest_tlv = malloc(dbg_dest_size);

    if (!drv->fw.dbg.dest_tlv) {
      goto out_free_fw;
    }

    if (*pieces->dbg_dest_ver == 0) {
      memcpy(drv->fw.dbg.dest_tlv, pieces->dbg_dest_tlv_v1, dbg_dest_size);
    } else {
      struct iwl_fw_dbg_dest_tlv_v1* dest_tlv = drv->fw.dbg.dest_tlv;

      dest_tlv->version = pieces->dbg_dest_tlv->version;
      dest_tlv->monitor_mode = pieces->dbg_dest_tlv->monitor_mode;
      dest_tlv->size_power = pieces->dbg_dest_tlv->size_power;
      dest_tlv->wrap_count = pieces->dbg_dest_tlv->wrap_count;
      dest_tlv->write_ptr_reg = pieces->dbg_dest_tlv->write_ptr_reg;
      dest_tlv->base_shift = pieces->dbg_dest_tlv->base_shift;
      memcpy(dest_tlv->reg_ops, pieces->dbg_dest_tlv->reg_ops,
             sizeof(drv->fw.dbg.dest_tlv->reg_ops[0]) * drv->fw.dbg.n_dest_reg);

      /* In version 1 of the destination tlv, which is
       * relevant for internal buffer exclusively,
       * the base address is part of given with the length
       * of the buffer, and the size shift is give instead of
       * end shift. We now store these values in base_reg,
       * and end shift, and when dumping the data we'll
       * manipulate it for extracting both the length and
       * base address */
      dest_tlv->base_reg = pieces->dbg_dest_tlv->cfg_reg;
      dest_tlv->end_shift = pieces->dbg_dest_tlv->size_shift;
    }
  }

  for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.conf_tlv); i++) {
    if (pieces->dbg_conf_tlv[i]) {
      drv->fw.dbg.conf_tlv[i] = kmemdup(pieces->dbg_conf_tlv[i], pieces->dbg_conf_tlv_len[i]);
      if (!pieces->dbg_conf_tlv_len[i]) {
        goto out_free_fw;
      }
    }
  }

  memset(&trigger_tlv_sz, 0xff, sizeof(trigger_tlv_sz));

  trigger_tlv_sz[FW_DBG_TRIGGER_MISSED_BEACONS] = sizeof(struct iwl_fw_dbg_trigger_missed_bcon);
  trigger_tlv_sz[FW_DBG_TRIGGER_CHANNEL_SWITCH] = 0;
  trigger_tlv_sz[FW_DBG_TRIGGER_FW_NOTIF] = sizeof(struct iwl_fw_dbg_trigger_cmd);
  trigger_tlv_sz[FW_DBG_TRIGGER_MLME] = sizeof(struct iwl_fw_dbg_trigger_mlme);
  trigger_tlv_sz[FW_DBG_TRIGGER_STATS] = sizeof(struct iwl_fw_dbg_trigger_stats);
  trigger_tlv_sz[FW_DBG_TRIGGER_RSSI] = sizeof(struct iwl_fw_dbg_trigger_low_rssi);
  trigger_tlv_sz[FW_DBG_TRIGGER_TXQ_TIMERS] = sizeof(struct iwl_fw_dbg_trigger_txq_timer);
  trigger_tlv_sz[FW_DBG_TRIGGER_TIME_EVENT] = sizeof(struct iwl_fw_dbg_trigger_time_event);
  trigger_tlv_sz[FW_DBG_TRIGGER_BA] = sizeof(struct iwl_fw_dbg_trigger_ba);
#ifdef CPTCFG_MAC80211_LATENCY_MEASUREMENTS
  trigger_tlv_sz[FW_DBG_TRIGGER_TX_LATENCY] = sizeof(struct iwl_fw_dbg_trigger_tx_latency);
#endif /* CPTCFG_MAC80211_LATENCY_MEASUREMENTS */
  trigger_tlv_sz[FW_DBG_TRIGGER_TDLS] = sizeof(struct iwl_fw_dbg_trigger_tdls);

  for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.trigger_tlv); i++) {
    if (pieces->dbg_trigger_tlv[i]) {
      /*
       * If the trigger isn't long enough, WARN and exit.
       * Someone is trying to debug something and he won't
       * be able to catch the bug he is trying to chase.
       * We'd better be noisy to be sure he knows what's
       * going on.
       */
      if (WARN_ON(pieces->dbg_trigger_tlv_len[i] <
                  (trigger_tlv_sz[i] + sizeof(struct iwl_fw_dbg_trigger_tlv)))) {
        goto out_free_fw;
      }
      drv->fw.dbg.trigger_tlv_len[i] = pieces->dbg_trigger_tlv_len[i];
      drv->fw.dbg.trigger_tlv[i] =
          kmemdup(pieces->dbg_trigger_tlv[i], drv->fw.dbg.trigger_tlv_len[i]);
      if (!drv->fw.dbg.trigger_tlv[i]) {
        goto out_free_fw;
      }
    }
  }

  /* Now that we can no longer fail, copy information */

  drv->fw.dbg.mem_tlv = pieces->dbg_mem_tlv;
  pieces->dbg_mem_tlv = NULL;
  drv->fw.dbg.n_mem_tlv = pieces->n_mem_tlv;

  /*
   * The (size - 16) / 12 formula is based on the information recorded
   * for each event, which is of mode 1 (including timestamp) for all
   * new microcodes that include this information.
   */
  fw->init_evtlog_ptr = pieces->init_evtlog_ptr;
  if (pieces->init_evtlog_size) {
    fw->init_evtlog_size = (pieces->init_evtlog_size - 16) / 12;
  } else {
    fw->init_evtlog_size = drv->trans->cfg->base_params->max_event_log_size;
  }
  fw->init_errlog_ptr = pieces->init_errlog_ptr;
  fw->inst_evtlog_ptr = pieces->inst_evtlog_ptr;
  if (pieces->inst_evtlog_size) {
    fw->inst_evtlog_size = (pieces->inst_evtlog_size - 16) / 12;
  } else {
    fw->inst_evtlog_size = drv->trans->cfg->base_params->max_event_log_size;
  }
  fw->inst_errlog_ptr = pieces->inst_errlog_ptr;

  /*
   * figure out the offset of chain noise reset and gain commands
   * base on the size of standard phy calibration commands table size
   */
  if (fw->ucode_capa.standard_phy_calibration_size > IWL_MAX_PHY_CALIBRATE_TBL_SIZE) {
    fw->ucode_capa.standard_phy_calibration_size = IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;
  }

  /* We have our copies now, allow OS release its copies */
  iwl_firmware_release(ucode_raw);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (!load_fw_dbg_err) {
    iwl_firmware_release(fw_dbg_config);
  }
#endif

  mtx_lock(&iwlwifi_opmode_table_mtx);
  switch (fw->type) {
    case IWL_FW_DVM:
      op = &iwlwifi_opmode_table[DVM_OP_MODE];
      break;
    default:
      WARN(1, "Invalid fw type %d\n", fw->type);
      __FALLTHROUGH;
    case IWL_FW_MVM:
      op = &iwlwifi_opmode_table[MVM_OP_MODE];
      break;
#if IS_ENABLED(CPTCFG_IWLFMAC)
    case IWL_FW_FMAC:
      op = &iwlwifi_opmode_table[FMAC_OP_MODE];
      break;
#endif
  }

#if IS_ENABLED(CPTCFG_IWLXVT)
  if (iwlwifi_mod_params.xvt_default_mode && drv->fw.type == IWL_FW_MVM) {
    op = &iwlwifi_opmode_table[XVT_OP_MODE];
  }

  drv->xvt_mode_on = (op == &iwlwifi_opmode_table[XVT_OP_MODE]);
#endif

#if IS_ENABLED(CPTCFG_IWLTEST)
  if (iwlwifi_mod_params.trans_test) {
    op = &iwlwifi_opmode_table[TRANS_TEST_OP_MODE];
  }
#endif
  IWL_INFO(drv, "loaded firmware version %s op_mode %s\n", drv->fw.fw_version, op->name);

  /* add this device to the list of devices using this op_mode */
  list_add_tail(&op->drv, &drv->list);

  if (op->ops) {
    drv->op_mode = _iwl_op_mode_start(drv, op);

    if (!drv->op_mode) {
      mtx_unlock(&iwlwifi_opmode_table_mtx);
      goto out_unbind;
    }
  } else {
    load_module = true;
  }
  mtx_unlock(&iwlwifi_opmode_table_mtx);

  /*
   * Complete the firmware request last so that
   * a driver unbind (stop) doesn't run while we
   * are doing the start() above.
   */
  sync_completion_signal(&drv->request_firmware_complete);

  /*
   * Load the module last so we don't block anything
   * else from proceeding if the module fails to load
   * or hangs loading.
   */
  if (load_module) {
    iwl_module_request("%s", op->name);
  }

  goto free;

try_again:
  /* try next, if any */
  iwl_firmware_release(ucode_raw);
  if (iwl_load_firmware(drv, false)) {
    goto out_unbind;
  }
  goto free;

out_free_fw:
  iwl_dealloc_ucode(drv);
  iwl_firmware_release(ucode_raw);
out_unbind:
  sync_completion_signal(&drv->request_firmware_complete);
  iwl_device_release(drv->trans->dev);
free:
  if (pieces) {
    for (i = 0; i < ARRAY_SIZE(pieces->img); i++) {
      kfree(pieces->img[i].sec);
    }
    kfree(pieces->dbg_mem_tlv);
    kfree(pieces);
  }
}

zx_status_t iwl_drv_start(struct iwl_trans* trans, struct iwl_drv** out_drv) {
  struct iwl_drv* drv;
  zx_status_t status = ZX_OK;

  drv = calloc(1, sizeof(*drv));
  if (!drv) {
    status = ZX_ERR_NO_MEMORY;
    goto err;
  }

  drv->trans = trans;
  drv->dev = trans->dev;

  drv->request_firmware_complete = SYNC_COMPLETION_INIT;
  list_initialize(&drv->list);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  trans->dbg_cfg = current_dbg_config;
  iwl_dbg_cfg_load_ini(drv->trans->dev, &drv->trans->dbg_cfg);

  iwl_load_fw_dbg_tlv(drv->trans->dev, drv->trans);
#endif

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  /* Create the device debugfs entries. */
  drv->dbgfs_drv = debugfs_create_dir(dev_name(trans->dev), iwl_dbgfs_root);

  if (!drv->dbgfs_drv) {
    IWL_ERR(drv, "failed to create debugfs directory\n");
    status = ZX_ERR_NO_MEMORY;
    goto err_free_tlv;
  }

  /* Create transport layer debugfs dir */
  drv->trans->dbgfs_dir = debugfs_create_dir("trans", drv->dbgfs_drv);

  if (!drv->trans->dbgfs_dir) {
    IWL_ERR(drv, "failed to create transport debugfs directory\n");
    status = ZX_ERR_NO_MEMORY;
    goto err_free_dbgfs;
  }
#endif

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  iwl_tm_gnl_add(drv->trans);
#endif

  status = iwl_load_firmware(drv, true);
  if (status != ZX_OK) {
    IWL_ERR(trans, "Couldn't request the fw\n");
    goto err_fw;
  }

#if IS_ENABLED(CPTCFG_IWLXVT)
  status = iwl_create_sysfs_file(drv);
  if (status != ZX_OK) {
    IWL_ERR(trans, "Couldn't create sysfs entry\n");
    goto err_fw;
  }
#endif

  *out_drv = drv;
  return status;

err_fw:
#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  iwl_tm_gnl_remove(drv->trans);
#endif
#ifdef CPTCFG_IWLWIFI_DEBUGFS
err_free_dbgfs:
  debugfs_remove_recursive(drv->dbgfs_drv);
err_free_tlv:
  iwl_fw_dbg_free(drv->trans);
#endif
  kfree(drv);
err:
  return status;
}

void iwl_drv_stop(struct iwl_drv* drv) {
  if (!drv) {
    return;
  }

  sync_completion_wait(&drv->request_firmware_complete, ZX_SEC(5));

  _iwl_op_mode_stop(drv);

  iwl_dealloc_ucode(drv);

  mtx_lock(&iwlwifi_opmode_table_mtx);
  /*
   * List is empty (this item wasn't added)
   * when firmware loading failed -- in that
   * case we can't remove it from any list.
   */
  if (!list_is_empty(&drv->list)) {
    list_remove_tail(&drv->list);
  }
  mtx_unlock(&iwlwifi_opmode_table_mtx);
  mtx_destroy(&iwlwifi_opmode_table_mtx);

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  drv->trans->ops->debugfs_cleanup(drv->trans);

  debugfs_remove_recursive(drv->dbgfs_drv);
#endif

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  iwl_dbg_cfg_free(&drv->trans->dbg_cfg);
#endif
#if 0   // NEEDS_PORTING
    iwl_fw_dbg_free(drv->trans);
#endif  // NEEDS_PORTING

#if IS_ENABLED(CPTCFG_IWLXVT)
  iwl_remove_sysfs_file(drv);
#endif

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  iwl_tm_gnl_remove(drv->trans);
#endif

  kfree(drv);
}

/* shared module parameters */
struct iwl_mod_params iwlwifi_mod_params = {
    .fw_restart = true,
    .bt_coex_active = true,
    .power_level = IWL_POWER_INDEX_1,
    .d0i3_disable = IS_ENABLED(CPTCFG_IWLWIFI_D0I3_DEFAULT_DISABLE),
    .d0i3_timeout = 1000,
    .uapsd_disable = IWL_DISABLE_UAPSD_BSS | IWL_DISABLE_UAPSD_P2P_CLIENT,
    /* the rest are 0 by default */
};

zx_status_t iwl_opmode_register(const char* name, const struct iwl_op_mode_ops* ops) {
  size_t i;
  struct iwl_drv* drv;
  struct iwlwifi_opmode_table* op;

  mtx_lock(&iwlwifi_opmode_table_mtx);
  for (i = 0; i < ARRAY_SIZE(iwlwifi_opmode_table); i++) {
    op = &iwlwifi_opmode_table[i];
    if (strcmp(op->name, name)) {
      continue;
    }
    op->ops = ops;

    list_for_every_entry (&op->drv, drv, struct iwl_drv, list) {
      drv->op_mode = _iwl_op_mode_start(drv, op);
      if (!drv->op_mode) {
        mtx_unlock(&iwlwifi_opmode_table_mtx);
        return ZX_ERR_INTERNAL;
      }
    }

    mtx_unlock(&iwlwifi_opmode_table_mtx);
    return ZX_OK;
  }
  mtx_unlock(&iwlwifi_opmode_table_mtx);
  return ZX_ERR_IO;
}

void iwl_opmode_deregister(const char* name) {
#if 0   // NEEDS_PORTING
    int i;
    struct iwl_drv* drv;

    mutex_lock(&iwlwifi_opmode_table_mtx);
    for (i = 0; i < ARRAY_SIZE(iwlwifi_opmode_table); i++) {
        if (strcmp(iwlwifi_opmode_table[i].name, name)) {
            continue;
        }
        iwlwifi_opmode_table[i].ops = NULL;

        /* call the stop routine for all devices */
        list_for_each_entry(drv, &iwlwifi_opmode_table[i].drv, list)
        _iwl_op_mode_stop(drv);

        mutex_unlock(&iwlwifi_opmode_table_mtx);
        return;
    }
    mutex_unlock(&iwlwifi_opmode_table_mtx);
#endif  // NEEDS_PORTING
}

zx_status_t iwl_drv_init(void) {
  size_t i;

  mtx_init(&iwlwifi_opmode_table_mtx, mtx_plain);

  for (i = 0; i < ARRAY_SIZE(iwlwifi_opmode_table); i++) {
    list_initialize(&iwlwifi_opmode_table[i].drv);
  }

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
  if (iwl_tm_gnl_init()) {
    return -EFAULT;
  }
#endif

#if IS_ENABLED(CPTCFG_IWLXVT)
  iwl_kobj = kobject_create_and_add("devices", &THIS_MODULE->mkobj.kobj);
  if (!iwl_kobj) {
    return -ENOMEM;
  }
#endif

  IWL_INFO(nullptr, DRV_DESCRIPTION "\n");
  IWL_INFO(nullptr, DRV_COPYRIGHT "\n");

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  /* Create the root of iwlwifi debugfs subsystem. */
  iwl_dbgfs_root = debugfs_create_dir(DRV_NAME, NULL);

  if (!iwl_dbgfs_root) {
    return -EFAULT;
  }
#endif

#if 0   // NEEDS_PORTING
    return iwl_pci_register_driver();
#endif  // NEEDS_PORTING
  return ZX_OK;
}

#if 0  // NEEDS_PORTING
static void __exit iwl_drv_exit(void) {
    iwl_pci_unregister_driver();

#ifdef CONFIG_IWLWIFI_DEBUGFS
    debugfs_remove_recursive(iwl_dbgfs_root);
#endif
}
module_exit(iwl_drv_exit);

#ifdef CONFIG_IWLWIFI_DEBUG
module_param_named(debug, iwlwifi_mod_params.debug_level, uint, 0644);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

module_param_named(swcrypto, iwlwifi_mod_params.swcrypto, int, 0444);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(11n_disable, iwlwifi_mod_params.disable_11n, uint, 0444);
MODULE_PARM_DESC(11n_disable,
                 "disable 11n functionality, bitmap: 1: full, 2: disable agg TX, 4: disable agg RX, 8 enable agg TX");
module_param_named(amsdu_size, iwlwifi_mod_params.amsdu_size, int, 0444);
MODULE_PARM_DESC(amsdu_size,
		 "amsdu size 0: 12K for multi Rx queue devices, 2K for AX210 devices, "
		 "4K for other devices 1:4K 2:8K 3:12K (16K buffers) 4: 2K (default 0)");
module_param_named(fw_restart, iwlwifi_mod_params.fw_restart, bool, 0444);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error (default true)");

module_param_named(nvm_file, iwlwifi_mod_params.nvm_file, charp, 0444);
MODULE_PARM_DESC(nvm_file, "NVM file name");

module_param_named(uapsd_disable, iwlwifi_mod_params.uapsd_disable, uint, 0644);
MODULE_PARM_DESC(uapsd_disable,
                 "disable U-APSD functionality bitmap 1: BSS 2: P2P Client (default: 3)");

static int enable_ini_set(const char *arg, const struct kernel_param *kp)
{
	int ret = 0;
	bool res;
	__u32 new_enable_ini;

	/* in case the argument type is a number */
	ret = kstrtou32(arg, 0, &new_enable_ini);
	if (!ret) {
		if (new_enable_ini > ENABLE_INI) {
			pr_err("enable_ini cannot be %d, in range 0-16\n", new_enable_ini);
			return -EINVAL;
		}
		goto out;
	}

	/* in case the argument type is boolean */
	ret = kstrtobool(arg, &res);
	if (ret)
		return ret;
	new_enable_ini = (res ? ENABLE_INI : 0);

out:
	iwlwifi_mod_params.enable_ini = new_enable_ini;
	return 0;
}

static const struct kernel_param_ops enable_ini_ops = {
	.set = enable_ini_set
};

module_param_cb(enable_ini, &enable_ini_ops, &iwlwifi_mod_params.enable_ini, 0644);
MODULE_PARM_DESC(enable_ini,
		 "0:disable, 1-15:FW_DBG_PRESET Values, 16:enabled without preset value defined,"
		 "Debug INI TLV FW debug infrastructure (default: 16)");

/*
 * set bt_coex_active to true, uCode will do kill/defer
 * every time the priority line is asserted (BT is sending signals on the
 * priority line in the PCIx).
 * set bt_coex_active to false, uCode will ignore the BT activity and
 * perform the normal operation
 *
 * User might experience transmit issue on some platform due to WiFi/BT
 * co-exist problem. The possible behaviors are:
 *   Able to scan and finding all the available AP
 *   Not able to associate with any AP
 * On those platforms, WiFi communication can be restored by set
 * "bt_coex_active" module parameter to "false"
 *
 * default: bt_coex_active = true (BT_COEX_ENABLE)
 */
module_param_named(bt_coex_active, iwlwifi_mod_params.bt_coex_active,
                   bool, 0444);
MODULE_PARM_DESC(bt_coex_active, "enable wifi/bt co-exist (default: enable)");

module_param_named(led_mode, iwlwifi_mod_params.led_mode, int, 0444);
MODULE_PARM_DESC(led_mode, "0=system default, "
                 "1=On(RF On)/Off(RF Off), 2=blinking, 3=Off (default: 0)");

module_param_named(power_save, iwlwifi_mod_params.power_save, bool, 0444);
MODULE_PARM_DESC(power_save,
                 "enable WiFi power management (default: disable)");

module_param_named(power_level, iwlwifi_mod_params.power_level, int, 0444);
MODULE_PARM_DESC(power_level,
                 "default power save level (range from 1 - 5, default: 1)");

module_param_named(disable_11ac, iwlwifi_mod_params.disable_11ac, bool, 0444);
MODULE_PARM_DESC(disable_11ac, "Disable VHT capabilities (default: false)");

module_param_named(remove_when_gone,
                   iwlwifi_mod_params.remove_when_gone, bool, 0444);
MODULE_PARM_DESC(remove_when_gone,
                 "Remove dev from PCIe bus if it is deemed inaccessible (default: false)");

module_param_named(disable_11ax, iwlwifi_mod_params.disable_11ax, bool,
		   S_IRUGO);
MODULE_PARM_DESC(disable_11ax, "Disable HE capabilities (default: false)");
#endif  // NEEDS_PORTING
