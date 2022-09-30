/** @file moal_utils.cc
 *
 *  @brief This file contains private ioctl functions
 *
 *
 *  Copyright 2008-2021 NXP
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ASIS AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/moal_utils.h"

#include <ctype.h>
#include <zircon/errors.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {
/** HostCmd_Header */
typedef struct hostcmd_header {
  /** Command */
  uint16_t command;
  /** Size */
  uint16_t size;
} HostCmd_Header;

constexpr char kHostCmdStr[] = "MRVL_CMDhostcmd";
constexpr uint16_t kHostCmdBufLen = 2048;
static zx_status_t priv_hostcmd(IoctlAdapter *ioctl_adapter, char *respbuf, uint32_t respbuflen) {
  char *data_ptr;
  uint32_t buf_len = 0;
  HostCmd_Header cmd_header;
  mlan_ds_misc_cfg misc_cfg = {};
  uint32_t ret;

  if ((strlen(kHostCmdStr) + sizeof(buf_len) + sizeof(cmd_header)) < respbuflen) {
    data_ptr = respbuf + strlen(kHostCmdStr);
    buf_len = *((uint32_t *)data_ptr);
    memcpy(&cmd_header, data_ptr + sizeof(buf_len), sizeof(HostCmd_Header));
  } else {
    NXPF_ERR("Buffer len: %d is insufficient", respbuflen);
    return ZX_ERR_NO_SPACE;
  }

  NXPF_INFO("Host command len = %d", cmd_header.size);
  if (cmd_header.size > MRVDRV_SIZE_OF_CMD_BUFFER) {
    NXPF_ERR("Cmd size: %d is larger than cmd buffer: %d", cmd_header.size,
             MRVDRV_SIZE_OF_CMD_BUFFER);
    return ZX_ERR_NO_SPACE;
  }

  misc_cfg.sub_command = MLAN_OID_MISC_HOST_CMD;
  misc_cfg.param.hostcmd.len = cmd_header.size;
  /* get the whole command */
  memcpy(misc_cfg.param.hostcmd.cmd, data_ptr + sizeof(buf_len), misc_cfg.param.hostcmd.len);
  IoctlRequest<mlan_ds_misc_cfg> request(MLAN_IOCTL_MISC_CFG, MLAN_ACT_SET, 0, misc_cfg);

  IoctlStatus ioctl_status = ioctl_adapter->IssueIoctlSync(&request);
  if (ioctl_status != IoctlStatus::Success) {
    NXPF_ERR("Hostcmd Ioctl failed sts: %d", ioctl_status);
    return ZX_ERR_INTERNAL;
  }
  ret = misc_cfg.param.hostcmd.len + sizeof(buf_len) + strlen(kHostCmdStr);
  if (ret > respbuflen) {
    NXPF_ERR("Ioctl response indicates data: %d larger than buffer: %d", ret, respbuflen);
    return ZX_ERR_NO_SPACE;
  }
  memcpy(data_ptr + sizeof(buf_len), misc_cfg.param.hostcmd.cmd, misc_cfg.param.hostcmd.len);
  memcpy(data_ptr, &misc_cfg.param.hostcmd.len, sizeof(uint32_t));
  return ZX_OK;
}

static int get_hex_val(char chr) {
  if (chr >= '0' && chr <= '9')
    return chr - '0';
  if (chr >= 'A' && chr <= 'F')
    return chr - 'A' + 10;
  if (chr >= 'a' && chr <= 'f')
    return chr - 'a' + 10;

  return 0;
}

static int atox(char **pos, char *end_pos) {
  int i = 0;
  char *a = *pos;

  while ((a < end_pos) && isxdigit(*a)) {
    i = i * 16 + get_hex_val(*a++);
  }
  *pos = a;

  return i;
}

zx_status_t process_hostcmd_cfg(IoctlAdapter *ioctl_adapter, char *data, uint32_t size) {
  zx_status_t status = ZX_OK;
  char *pos = data;
  char *end_pos = data + size;
  char *intf_s, *intf_e;
  char buf[kHostCmdBufLen];
  char *ptr = nullptr;
  char *end_ptr = nullptr;
  uint32_t cmd_len = 0;
  bool start_raw = false;

  ptr = buf;
  end_ptr = ptr + sizeof(buf);
  if ((ptr + strlen(kHostCmdStr) + sizeof(uint32_t)) < end_ptr) {
    strcpy(ptr, kHostCmdStr);
    ptr = buf + strlen(kHostCmdStr) + sizeof(uint32_t);
  } else {
    NXPF_ERR("Output buffer overrun");
    return ZX_ERR_NO_SPACE;
  }
  // while ((pos - data) < size) {
  while (pos < end_pos) {
    while ((pos < end_pos) && (*pos == ' ' || *pos == '\t')) {
      pos++;
    }
    if (pos >= end_pos) {
      // No more data, return.
      return status;
    }
    if (*pos == '#') { /* Line comment */
      while ((pos < end_pos) && (*pos != '\n')) {
        pos++;
      }
      if (pos < end_pos) {
        pos++;
      } else {
        // No more data, return.
        return status;
      }
    }
    if (pos < end_pos) {
      if ((*pos == '\r' && ((pos + 1 < end_pos) && *(pos + 1) == '\n')) || *pos == '\n' ||
          *pos == '\0') {
        pos++;
        continue; /* Needn't process this line */
      }
    }

    if ((pos < end_pos) && (*pos == '}')) {
      cmd_len = *((uint16_t *)(buf + strlen(kHostCmdStr) + sizeof(uint32_t) + sizeof(uint16_t)));
      memcpy(buf + strlen(kHostCmdStr), &cmd_len, sizeof(uint32_t));

      /* fire the hostcommand from here */
      status = priv_hostcmd(ioctl_adapter, buf, kHostCmdBufLen);
      if (status != ZX_OK) {
        NXPF_ERR("%s priv_hostcmd failed status: %d", __func__, status);
        return status;
      }
      memset(buf + strlen(kHostCmdStr), 0, kHostCmdBufLen - strlen(kHostCmdStr));
      ptr = buf + strlen(kHostCmdStr) + sizeof(uint32_t);
      if (ptr >= end_ptr) {
        NXPF_ERR("Output buffer overrun");
        return ZX_ERR_NO_SPACE;
      }
      start_raw = false;
      pos++;
      continue;
    }

    if (start_raw == false) {
      intf_s = (char *)memchr(pos, '=', end_pos - pos);
      if (intf_s)
        intf_e = (char *)memchr(intf_s, '{', end_pos - intf_s);
      else
        intf_e = NULL;

      if (intf_s && intf_e) {
        start_raw = true;
        pos = intf_e + 1;
        continue;
      }
    }

    if (start_raw) {
      /* Raw data block exists */
      while ((pos < end_pos) && (*pos != '\n')) {
        if ((*pos <= 'f' && *pos >= 'a') || (*pos <= 'F' && *pos >= 'A') ||
            (*pos <= '9' && *pos >= '0')) {
          if (ptr >= end_ptr) {
            NXPF_ERR("Output buffer overrun");
            return ZX_ERR_NO_SPACE;
          }
          *ptr++ = (char)atox(&pos, end_pos);
        } else
          pos++;
      }
    }
  }
  return ZX_OK;
}

}  // namespace wlan::nxpfmac
