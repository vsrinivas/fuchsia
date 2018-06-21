/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

//#include <linux/bcm47xx_nvram.h>
//#include <linux/device.h>
//#include <linux/firmware.h>
//#include <linux/kernel.h>
//#include <linux/module.h>
//#include <linux/slab.h>

#include "firmware.h"

#include <zircon/syscalls.h>

#include "common.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "linuxisms.h"

#define BRCMF_FW_MAX_NVRAM_SIZE 64000
#define BRCMF_FW_NVRAM_DEVPATH_LEN 19 /* devpath0=pcie/1/4/ */
#define BRCMF_FW_NVRAM_PCIEDEV_LEN 10 /* pcie/1/4/ + \0 */
#define BRCMF_FW_DEFAULT_BOARDREV "boardrev=0xff"

enum nvram_parser_state { IDLE, KEY, VALUE, COMMENT, END };

/**
 * struct nvram_parser - internal info for parser.
 *
 * @state: current parser state.
 * @data: input buffer being parsed.
 * @nvram: output buffer with parse result.
 * @nvram_len: lenght of parse result.
 * @line: current line.
 * @column: current column in line.
 * @pos: byte offset in input buffer.
 * @entry: start position of key,value entry.
 * @multi_dev_v1: detect pcie multi device v1 (compressed).
 * @multi_dev_v2: detect pcie multi device v2.
 * @boardrev_found: nvram contains boardrev information.
 */
struct nvram_parser {
    enum nvram_parser_state state;
    const uint8_t* data;
    uint8_t* nvram;
    uint32_t nvram_len;
    uint32_t line;
    uint32_t column;
    uint32_t pos;
    uint32_t entry;
    bool multi_dev_v1;
    bool multi_dev_v2;
    bool boardrev_found;
};

/**
 * is_nvram_char() - check if char is a valid one for NVRAM entry
 *
 * It accepts all printable ASCII chars except for '#' which opens a comment.
 * Please note that ' ' (space) while accepted is not a valid key name char.
 */
static bool is_nvram_char(char c) {
    /* comment marker excluded */
    if (c == '#') {
        return false;
    }

    /* key and value may have any other readable character */
    return (c >= 0x20 && c < 0x7f);
}

static bool is_whitespace(char c) {
    return (c == ' ' || c == '\r' || c == '\n' || c == '\t');
}

static enum nvram_parser_state brcmf_nvram_handle_idle(struct nvram_parser* nvp) {
    char c;

    c = nvp->data[nvp->pos];
    if (c == '\n') {
        return COMMENT;
    }
    if (is_whitespace(c) || c == '\0') {
        goto proceed;
    }
    if (c == '#') {
        return COMMENT;
    }
    if (is_nvram_char(c)) {
        nvp->entry = nvp->pos;
        return KEY;
    }
    brcmf_dbg(INFO, "warning: ln=%d:col=%d: ignoring invalid character\n", nvp->line, nvp->column);
proceed:
    nvp->column++;
    nvp->pos++;
    return IDLE;
}

static enum nvram_parser_state brcmf_nvram_handle_key(struct nvram_parser* nvp) {
    enum nvram_parser_state st = nvp->state;
    char c;

    c = nvp->data[nvp->pos];
    if (c == '=') {
        /* ignore RAW1 by treating as comment */
        if (strncmp((char*)&nvp->data[nvp->entry], "RAW1", 4) == 0) {
            st = COMMENT;
        } else {
            st = VALUE;
        }
        if (strncmp((char*)&nvp->data[nvp->entry], "devpath", 7) == 0) {
            nvp->multi_dev_v1 = true;
        }
        if (strncmp((char*)&nvp->data[nvp->entry], "pcie/", 5) == 0) {
            nvp->multi_dev_v2 = true;
        }
        if (strncmp((char*)&nvp->data[nvp->entry], "boardrev", 8) == 0) {
            nvp->boardrev_found = true;
        }
    } else if (!is_nvram_char(c) || c == ' ') {
        brcmf_dbg(INFO, "warning: ln=%d:col=%d: '=' expected, skip invalid key entry\n", nvp->line,
                  nvp->column);
        return COMMENT;
    }

    nvp->column++;
    nvp->pos++;
    return st;
}

static enum nvram_parser_state brcmf_nvram_handle_value(struct nvram_parser* nvp) {
    char c;
    char* skv;
    char* ekv;
    uint32_t cplen;

    c = nvp->data[nvp->pos];
    if (!is_nvram_char(c)) {
        /* key,value pair complete */
        ekv = (char*)&nvp->data[nvp->pos];
        skv = (char*)&nvp->data[nvp->entry];
        cplen = ekv - skv;
        if (nvp->nvram_len + cplen + 1 >= BRCMF_FW_MAX_NVRAM_SIZE) {
            return END;
        }
        /* copy to output buffer */
        memcpy(&nvp->nvram[nvp->nvram_len], skv, cplen);
        nvp->nvram_len += cplen;
        nvp->nvram[nvp->nvram_len] = '\0';
        nvp->nvram_len++;
        return IDLE;
    }
    nvp->pos++;
    nvp->column++;
    return VALUE;
}

static enum nvram_parser_state brcmf_nvram_handle_comment(struct nvram_parser* nvp) {
    char* eoc;
    char* sol;

    sol = (char*)&nvp->data[nvp->pos];
    eoc = strchr(sol, '\n');
    if (!eoc) {
        eoc = strchr(sol, '\0');
        if (!eoc) {
            return END;
        }
    }

    /* eat all moving to next line */
    nvp->line++;
    nvp->column = 1;
    nvp->pos += (eoc - sol) + 1;
    return IDLE;
}

static enum nvram_parser_state brcmf_nvram_handle_end(struct nvram_parser* nvp) {
    /* final state */
    return END;
}

static enum nvram_parser_state (*nv_parser_states[])(struct nvram_parser* nvp) = {
    brcmf_nvram_handle_idle, brcmf_nvram_handle_key, brcmf_nvram_handle_value,
    brcmf_nvram_handle_comment, brcmf_nvram_handle_end
};

static zx_status_t brcmf_init_nvram_parser(struct nvram_parser* nvp, const uint8_t* data,
                                           size_t data_len) {
    size_t size;

    memset(nvp, 0, sizeof(*nvp));
    nvp->data = data;
    /* Limit size to MAX_NVRAM_SIZE, some files contain lot of comment */
    if (data_len > BRCMF_FW_MAX_NVRAM_SIZE) {
        size = BRCMF_FW_MAX_NVRAM_SIZE;
    } else {
        size = data_len;
    }
    /* Alloc for extra 0 byte + roundup by 4 + length field */
    size += 1 + 3 + sizeof(uint32_t);
    nvp->nvram = calloc(1, size);
    if (!nvp->nvram) {
        return ZX_ERR_NO_MEMORY;
    }

    nvp->line = 1;
    nvp->column = 1;
    return ZX_OK;
}

/* brcmf_fw_strip_multi_v1 :Some nvram files contain settings for multiple
 * devices. Strip it down for one device, use domain_nr/bus_nr to determine
 * which data is to be returned. v1 is the version where nvram is stored
 * compressed and "devpath" maps to index for valid entries.
 */
static void brcmf_fw_strip_multi_v1(struct nvram_parser* nvp, uint16_t domain_nr, uint16_t bus_nr) {
    /* Device path with a leading '=' key-value separator */
    char pci_path[] = "=pci/?/?";
    size_t pci_len;
    char pcie_path[] = "=pcie/?/?";
    size_t pcie_len;

    uint32_t i, j;
    bool found;
    uint8_t* nvram;
    uint8_t id;

    nvram = calloc(1, nvp->nvram_len + 1 + 3 + sizeof(uint32_t));
    if (!nvram) {
        goto fail;
    }

    /* min length: devpath0=pcie/1/4/ + 0:x=y */
    if (nvp->nvram_len < BRCMF_FW_NVRAM_DEVPATH_LEN + 6) {
        goto fail;
    }

    /* First search for the devpathX and see if it is the configuration
     * for domain_nr/bus_nr. Search complete nvp
     */
    snprintf(pci_path, sizeof(pci_path), "=pci/%d/%d", domain_nr, bus_nr);
    pci_len = strlen(pci_path);
    snprintf(pcie_path, sizeof(pcie_path), "=pcie/%d/%d", domain_nr, bus_nr);
    pcie_len = strlen(pcie_path);
    found = false;
    i = 0;
    while (i < nvp->nvram_len - BRCMF_FW_NVRAM_DEVPATH_LEN) {
        /* Format: devpathX=pcie/Y/Z/
         * Y = domain_nr, Z = bus_nr, X = virtual ID
         */
        if (strncmp((char*)&nvp->nvram[i], "devpath", 7) == 0 &&
                (!strncmp((char*)&nvp->nvram[i + 8], pci_path, pci_len) ||
                 !strncmp((char*)&nvp->nvram[i + 8], pcie_path, pcie_len))) {
            id = nvp->nvram[i + 7] - '0';
            found = true;
            break;
        }
        while (nvp->nvram[i] != 0) {
            i++;
        }
        i++;
    }
    if (!found) {
        goto fail;
    }

    /* Now copy all valid entries, release old nvram and assign new one */
    i = 0;
    j = 0;
    while (i < nvp->nvram_len) {
        if ((nvp->nvram[i] - '0' == id) && (nvp->nvram[i + 1] == ':')) {
            i += 2;
            if (strncmp((char*)&nvp->nvram[i], "boardrev", 8) == 0) {
                nvp->boardrev_found = true;
            }
            while (nvp->nvram[i] != 0) {
                nvram[j] = nvp->nvram[i];
                i++;
                j++;
            }
            nvram[j] = 0;
            j++;
        }
        while (nvp->nvram[i] != 0) {
            i++;
        }
        i++;
    }
    free(nvp->nvram);
    nvp->nvram = nvram;
    nvp->nvram_len = j;
    return;

fail:
    free(nvram);
    nvp->nvram_len = 0;
}

/* brcmf_fw_strip_multi_v2 :Some nvram files contain settings for multiple
 * devices. Strip it down for one device, use domain_nr/bus_nr to determine
 * which data is to be returned. v2 is the version where nvram is stored
 * uncompressed, all relevant valid entries are identified by
 * pcie/domain_nr/bus_nr:
 */
static void brcmf_fw_strip_multi_v2(struct nvram_parser* nvp, uint16_t domain_nr, uint16_t bus_nr) {
    char prefix[BRCMF_FW_NVRAM_PCIEDEV_LEN];
    size_t len;
    uint32_t i, j;
    uint8_t* nvram;

    nvram = calloc(1, nvp->nvram_len + 1 + 3 + sizeof(uint32_t));
    if (!nvram) {
        goto fail;
    }

    /* Copy all valid entries, release old nvram and assign new one.
     * Valid entries are of type pcie/X/Y/ where X = domain_nr and
     * Y = bus_nr.
     */
    snprintf(prefix, sizeof(prefix), "pcie/%d/%d/", domain_nr, bus_nr);
    len = strlen(prefix);
    i = 0;
    j = 0;
    while (i < nvp->nvram_len - len) {
        if (strncmp((char*)&nvp->nvram[i], prefix, len) == 0) {
            i += len;
            if (strncmp((char*)&nvp->nvram[i], "boardrev", 8) == 0) {
                nvp->boardrev_found = true;
            }
            while (nvp->nvram[i] != 0) {
                nvram[j] = nvp->nvram[i];
                i++;
                j++;
            }
            nvram[j] = 0;
            j++;
        }
        while (nvp->nvram[i] != 0) {
            i++;
        }
        i++;
    }
    free(nvp->nvram);
    nvp->nvram = nvram;
    nvp->nvram_len = j;
    return;
fail:
    free(nvram);
    nvp->nvram_len = 0;
}

static void brcmf_fw_add_defaults(struct nvram_parser* nvp) {
    if (nvp->boardrev_found) {
        return;
    }

    memcpy(&nvp->nvram[nvp->nvram_len], &BRCMF_FW_DEFAULT_BOARDREV,
           strlen(BRCMF_FW_DEFAULT_BOARDREV));
    nvp->nvram_len += strlen(BRCMF_FW_DEFAULT_BOARDREV);
    nvp->nvram[nvp->nvram_len] = '\0';
    nvp->nvram_len++;
}

/* brcmf_nvram_strip :Takes a buffer of "<var>=<value>\n" lines read from a fil
 * and ending in a NUL. Removes carriage returns, empty lines, comment lines,
 * and converts newlines to NULs. Shortens buffer as needed and pads with NULs.
 * End of buffer is completed with token identifying length of buffer.
 */
static void* brcmf_fw_nvram_strip(const uint8_t* data, size_t data_len, uint32_t* new_length,
                                  uint16_t domain_nr, uint16_t bus_nr) {
    struct nvram_parser nvp;
    uint32_t pad;
    uint32_t token;
    uint32_t token_le;

    if (brcmf_init_nvram_parser(&nvp, data, data_len) < 0) {
        return NULL;
    }

    while (nvp.pos < data_len) {
        nvp.state = nv_parser_states[nvp.state](&nvp);
        if (nvp.state == END) {
            break;
        }
    }
    if (nvp.multi_dev_v1) {
        nvp.boardrev_found = false;
        brcmf_fw_strip_multi_v1(&nvp, domain_nr, bus_nr);
    } else if (nvp.multi_dev_v2) {
        nvp.boardrev_found = false;
        brcmf_fw_strip_multi_v2(&nvp, domain_nr, bus_nr);
    }

    if (nvp.nvram_len == 0) {
        free(nvp.nvram);
        return NULL;
    }

    brcmf_fw_add_defaults(&nvp);

    pad = nvp.nvram_len;
    *new_length = roundup(nvp.nvram_len + 1, 4);
    while (pad != *new_length) {
        nvp.nvram[pad] = 0;
        pad++;
    }

    token = *new_length / 4;
    token = (~token << 16) | (token & 0x0000FFFF);
    token_le = token;

    memcpy(&nvp.nvram[*new_length], &token_le, sizeof(token_le));
    *new_length += sizeof(token_le);

    return nvp.nvram;
}

void brcmf_fw_nvram_free(void* nvram) {
    free(nvram);
}

struct brcmf_fw {
    struct brcmf_device* dev;
    uint16_t flags;
    const struct brcmf_firmware* code;
    const char* nvram_name;
    uint16_t domain_nr;
    uint16_t bus_nr;
    void (*done)(struct brcmf_device* dev, zx_status_t err, const struct brcmf_firmware* fw,
                 void* nvram_image, uint32_t nvram_len);
};

static zx_status_t brcmf_fw_request_nvram_done(const struct brcmf_firmware* fw, void* ctx) {
    struct brcmf_fw* fwctx = ctx;
    uint32_t nvram_length = 0;
    void* nvram = NULL;
    uint8_t* data = NULL;
    size_t data_len;
    bool raw_nvram;

    brcmf_dbg(TRACE, "enter: dev=%s\n", dev_name(fwctx->dev));
    if (fw && fw->data) {
        data = (uint8_t*)fw->data;
        data_len = fw->size;
        raw_nvram = false;
    } else {
        data = bcm47xx_nvram_get_contents(&data_len);
        if (!data && !(fwctx->flags & BRCMF_FW_REQ_NV_OPTIONAL)) {
            goto fail;
        }
        raw_nvram = true;
    }

    if (data)
        nvram =
            brcmf_fw_nvram_strip(data, data_len, &nvram_length, fwctx->domain_nr, fwctx->bus_nr);

    if (raw_nvram) {
        bcm47xx_nvram_release_contents(data);
    }
    if (!nvram && !(fwctx->flags & BRCMF_FW_REQ_NV_OPTIONAL)) {
        goto fail;
    }

    fwctx->done(fwctx->dev, ZX_OK, fwctx->code, nvram, nvram_length);
    free(fwctx);
    return ZX_OK;

fail:
    brcmf_dbg(TRACE, "failed: dev=%s\n", dev_name(fwctx->dev));
    fwctx->done(fwctx->dev, ZX_ERR_NOT_FOUND, NULL, NULL, 0);
    free(fwctx);
    return ZX_ERR_NO_RESOURCES;
}

zx_status_t request_firmware_nowait(bool b, const char* name, struct brcmf_device* dev,
                                    uint32_t flags, struct brcmf_fw* ctx,
                                    zx_status_t (*callback)(const struct brcmf_firmware* fw,
                                                            void* ctx)) {
    zx_status_t result;
    zx_handle_t fw_vmo;
    struct brcmf_firmware fw;

    result = load_firmware(dev->zxdev, name, &fw_vmo, &fw.size);
    brcmf_dbg(TEMP, "load_firmware of '%s' -> ret %d, size %ld", name, result, fw.size);
    if (result != ZX_OK) {
        return result;
    }
    if (fw.size == 0) {
        zx_handle_close(fw_vmo);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    char* fw_buf = malloc(fw.size);
    if (fw_buf == NULL) {
        zx_handle_close(fw_vmo);
        return ZX_ERR_NO_MEMORY;
    }
    // TODO(cphoenix): Use vmar_map/destroy to save an unnecessary copy
    result = zx_vmo_read(fw_vmo, fw_buf, 0, fw.size);
    if (result == ZX_OK) {
        fw.data = fw_buf;
        result = callback(&fw, ctx);
    }
    free(fw_buf);
    zx_handle_close(fw_vmo);
    return result;
}

static zx_status_t brcmf_fw_request_code_done(const struct brcmf_firmware* fw, void* ctx) {
    struct brcmf_fw* fwctx = ctx;
    zx_status_t result = ZX_OK;

    brcmf_dbg(TRACE, "enter: dev=%s\n", dev_name(fwctx->dev));
    if (!fw) {
        result = ZX_ERR_INVALID_ARGS;
        goto fail;
    }
    /* only requested code so done here */
    if (!(fwctx->flags & BRCMF_FW_REQUEST_NVRAM)) {
        goto done;
    }

    fwctx->code = fw;
    result = request_firmware_nowait(true, fwctx->nvram_name, fwctx->dev, GFP_KERNEL,
                                  fwctx, brcmf_fw_request_nvram_done);

    /* pass NULL to nvram callback for bcm47xx fallback */
    if (result != ZX_OK) {
        brcmf_fw_request_nvram_done(NULL, fwctx);
    }
    return result;

fail:
    brcmf_dbg(TRACE, "failed: dev=%s\n", dev_name(fwctx->dev));
done:
    fwctx->done(fwctx->dev, result, fw, NULL, 0);
    free(fwctx);
    return result;
}

zx_status_t brcmf_fw_get_firmwares_pcie(struct brcmf_device* dev, uint16_t flags, const char* code,
                                        const char* nvram,
                                        void (*fw_cb)(struct brcmf_device* dev, zx_status_t err,
                                                      const struct brcmf_firmware* fw,
                                                      void* nvram_image, uint32_t nvram_len),
                                        uint16_t domain_nr, uint16_t bus_nr) {
    struct brcmf_fw* fwctx;

    brcmf_dbg(TRACE, "enter: dev=%s\n", dev_name(dev));
    if (!fw_cb || !code) {
        return ZX_ERR_INVALID_ARGS;
    }

    if ((flags & BRCMF_FW_REQUEST_NVRAM) && !nvram) {
        return ZX_ERR_INVALID_ARGS;
    }

    fwctx = calloc(1, sizeof(*fwctx));
    if (!fwctx) {
        return ZX_ERR_NO_MEMORY;
    }

    fwctx->dev = dev;
    fwctx->flags = flags;
    fwctx->done = fw_cb;
    if (flags & BRCMF_FW_REQUEST_NVRAM) {
        fwctx->nvram_name = nvram;
    }
    fwctx->domain_nr = domain_nr;
    fwctx->bus_nr = bus_nr;

    return request_firmware_nowait(true, code, dev, GFP_KERNEL, fwctx,
                                   brcmf_fw_request_code_done);
}

zx_status_t brcmf_fw_get_firmwares(struct brcmf_device* dev, uint16_t flags, const char* code,
                                   const char* nvram,
                                   void (*fw_cb)(struct brcmf_device* dev, zx_status_t err,
                                                 const struct brcmf_firmware* fw, void* nvram_image,
                                                 uint32_t nvram_len)) {
    return brcmf_fw_get_firmwares_pcie(dev, flags, code, nvram, fw_cb, 0, 0);
}

zx_status_t brcmf_fw_map_chip_to_name(uint32_t chip, uint32_t chiprev,
                                      struct brcmf_firmware_mapping mapping_table[],
                                      uint32_t table_size, char fw_name[BRCMF_FW_NAME_LEN],
                                      char nvram_name[BRCMF_FW_NAME_LEN]) {
    uint32_t i;
    char end;

    for (i = 0; i < table_size; i++) {
        if (mapping_table[i].chipid == chip && mapping_table[i].revmask & BIT(chiprev)) {
            break;
        }
    }

    if (i == table_size) {
        brcmf_err("Unknown chipid %d [%d]\n", chip, chiprev);
        return ZX_ERR_NOT_FOUND;
    }

    /* check if firmware path is provided by module parameter */
    if (brcmf_mp_global.firmware_path[0] != '\0') {
        strlcpy(fw_name, brcmf_mp_global.firmware_path, BRCMF_FW_NAME_LEN);
        if ((nvram_name) && (mapping_table[i].nvram)) {
            strlcpy(nvram_name, brcmf_mp_global.firmware_path, BRCMF_FW_NAME_LEN);
        }

        end = brcmf_mp_global.firmware_path[strlen(brcmf_mp_global.firmware_path) - 1];
        if (end != '/') {
            strlcat(fw_name, "/", BRCMF_FW_NAME_LEN);
            if ((nvram_name) && (mapping_table[i].nvram)) {
                strlcat(nvram_name, "/", BRCMF_FW_NAME_LEN);
            }
        }
    }
    strlcat(fw_name, mapping_table[i].fw, BRCMF_FW_NAME_LEN);
    if ((nvram_name) && (mapping_table[i].nvram)) {
        strlcat(nvram_name, mapping_table[i].nvram, BRCMF_FW_NAME_LEN);
    }

    brcmf_dbg(TEMP, "using %s for chip %#08x(%d) rev %#08x\n", fw_name, chip, chip, chiprev);

    return ZX_OK;
}
