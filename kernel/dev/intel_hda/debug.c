// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_LIB_CONSOLE

#include <lib/console.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "codec.h"
#include "intel_hda.h"

typedef struct flag_lut_entry {
    uint32_t    flag_bit;
    const char* flag_name;
} flag_lut_entry_t;

static void ihda_dump_flags(uint32_t flags,
                            const flag_lut_entry_t* table,
                            size_t table_size,
                            const char* suffix,
                            const char* no_flags_text) {
    bool got_one = false;
    for (size_t i = 0; i < table_size; ++i) {
        if (flags & table[i].flag_bit) {
            printf("%s%s", got_one ? " " : "", table[i].flag_name);
            got_one = true;
        }
    }

    printf("%s\n", got_one ? suffix : no_flags_text);
}

#define DUMP_FLAGS(flags, table, suffix, no_flags_text) \
    ihda_dump_flags(flags, table, countof(table), suffix, no_flags_text)

static const flag_lut_entry_t POWER_STATE_FLAGS[] = {
    { IHDA_PWR_STATE_EPSS,     "EPSS" },
    { IHDA_PWR_STATE_CLKSTOP,  "CLKSTOP" },
    { IHDA_PWR_STATE_S3D3COLD, "S3D3COLD" },
    { IHDA_PWR_STATE_D3COLD,   "D3COLD" },
    { IHDA_PWR_STATE_D3,       "D3HOT" },
    { IHDA_PWR_STATE_D2,       "D2" },
    { IHDA_PWR_STATE_D1,       "D1" },
    { IHDA_PWR_STATE_D0,       "D0" },
};

static const flag_lut_entry_t PCM_RATE_FLAGS[] = {
    { IHDA_PCM_RATE_384000, "384000" },
    { IHDA_PCM_RATE_192000, "192000" },
    { IHDA_PCM_RATE_176400, "176400" },
    { IHDA_PCM_RATE_96000,   "96000" },
    { IHDA_PCM_RATE_88200,   "88200" },
    { IHDA_PCM_RATE_48000,   "48000" },
    { IHDA_PCM_RATE_44100,   "44100" },
    { IHDA_PCM_RATE_32000,   "32000" },
    { IHDA_PCM_RATE_22050,   "22050" },
    { IHDA_PCM_RATE_16000,   "16000" },
    { IHDA_PCM_RATE_11025,   "11025" },
    { IHDA_PCM_RATE_8000,     "8000" },
};

static const flag_lut_entry_t PCM_SIZE_FLAGS[] = {
    { IHDA_PCM_SIZE_32BITS, "32" },
    { IHDA_PCM_SIZE_24BITS, "24" },
    { IHDA_PCM_SIZE_20BITS, "20" },
    { IHDA_PCM_SIZE_16BITS, "16" },
    { IHDA_PCM_SIZE_8BITS,   "8" },
};

static const flag_lut_entry_t PCM_FMT_FLAGS[] = {
    { IHDA_PCM_FORMAT_AC3,     "AC3" },
    { IHDA_PCM_FORMAT_FLOAT32, "FLOAT32" },
    { IHDA_PCM_FORMAT_PCM,     "PCM" },
};

static const flag_lut_entry_t AW_CAPS_FLAGS[] = {
    { AW_CAPS_FLAG_AMP_PARAM_OVERRIDE, "AmpParamOverride" },
    { AW_CAPS_FLAG_FORMAT_OVERRIDE,    "FormatOverride" },
    { AW_CAPS_FLAG_STRIP_SUPPORTED,    "StripingSupported" },
    { AW_CAPS_FLAG_PROC_WIDGET,        "HasProcessingControls" },
    { AW_CAPS_FLAG_CAN_SEND_UNSOL,     "CanSendUnsolicited" },
    { AW_CAPS_FLAG_DIGITAL,            "Digital" },
    { AW_CAPS_FLAG_CAN_LR_SWAP,        "CanSwapLR" },
    { AW_CAPS_FLAG_HAS_CONTENT_PROT,   "HasContentProtection" },
};

static const flag_lut_entry_t PIN_CAPS_FLAGS[] = {
    { AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE,  "ImpedanceSense" },
    { AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED,     "TrigReq" },
    { AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT,  "PresDetect" },
    { AW_PIN_CAPS_FLAG_CAN_DRIVE_HEADPHONES, "HeadphoneDrive" },
    { AW_PIN_CAPS_FLAG_CAN_OUTPUT,           "CanOutput" },
    { AW_PIN_CAPS_FLAG_CAN_INPUT,            "CanInput" },
    { AW_PIN_CAPS_FLAG_BALANCED_IO,          "Balanced" },
    { AW_PIN_CAPS_FLAG_HDMI,                 "HDMI" },
    { AW_PIN_CAPS_FLAG_VREF_HIZ,             "VREF_HIZ" },
    { AW_PIN_CAPS_FLAG_VREF_50_PERCENT,      "VREF_50%" },
    { AW_PIN_CAPS_FLAG_VREF_GROUND,          "VREF_GND" },
    { AW_PIN_CAPS_FLAG_VREF_80_PERCENT,      "VREF_80%" },
    { AW_PIN_CAPS_FLAG_VREF_100_PERCENT,     "VREF_100%" },
    { AW_PIN_CAPS_FLAG_CAN_EAPD,             "EAPD" },
    { AW_PIN_CAPS_FLAG_DISPLAY_PORT,         "DisplayPort" },
    { AW_PIN_CAPS_FLAG_HIGH_BIT_RATE,        "HighBitRate" },
};

static void cmd_ihda_list_cbk(intel_hda_device_t* dev, void* ctx) {
    printf("Device #%d\n", dev->dev_id);
}

static int cmd_ihda_list(int argc, const cmd_args *argv) {
    DEBUG_ASSERT(argc >= 2);

    if (argc != 2)
        goto usage;

    printf("Listing currently active Intel HDA Devices...\n");
    intel_hda_foreach(cmd_ihda_list_cbk, NULL);
    printf("done\n");

    return NO_ERROR;

usage:
    printf("usage: %s %s\n", argv[0].str, argv[1].str);
    return NO_ERROR;
}

static int ihda_dump32(const char* name, void* base, size_t offset, bool crlf) {
    uint32_t val = pcie_read32((uint32_t*)((intptr_t)base + offset));
    return printf("[%02zx] %10s : %08x (%u)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static int ihda_dump24(const char* name, void* base, size_t offset, bool crlf) {
    uint32_t val = pcie_read32((uint32_t*)((intptr_t)base + offset)) & 0xFFFFFF;
    return printf("[%02zx] %10s : %06x   (%u)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static int ihda_dump16(const char* name, void* base, size_t offset, bool crlf) {
    uint16_t val = pcie_read16((uint16_t*)((intptr_t)base + offset));
    return printf("[%02zx] %10s : %04hx     (%hu)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static int ihda_dump8(const char* name, void* base, size_t offset, bool crlf) {
    uint8_t val = pcie_read8((uint8_t*)((intptr_t)base + offset));
    return printf("[%02zx] %10s : %02x       (%u)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static void pad(int done, int width) {
    if (done < 0) return;
    while (done < width) {
        printf(" ");
        done++;
    }
}

static void ihda_dump_stream_regs(const char* name,
                                  size_t count,
                                  hda_stream_desc_regs_t* regs) {
    static const struct {
        const char* name;
        int (*dump_fn)(const char*, void*, size_t, bool);
        size_t offset;
    } STREAM_REGS[] = {
        { "CTL",   ihda_dump24, offsetof(hda_stream_desc_regs_t, ctl) },
        { "STS",   ihda_dump8,  offsetof(hda_stream_desc_regs_t, sts) },
        { "LPIB",  ihda_dump32, offsetof(hda_stream_desc_regs_t, lpib) },
        { "CBL",   ihda_dump32, offsetof(hda_stream_desc_regs_t, cbl) },
        { "LVI",   ihda_dump16, offsetof(hda_stream_desc_regs_t, lvi) },
        { "FIFOD", ihda_dump16, offsetof(hda_stream_desc_regs_t, fifod) },
        { "FMT",   ihda_dump16, offsetof(hda_stream_desc_regs_t, fmt) },
        { "BDPL",  ihda_dump32, offsetof(hda_stream_desc_regs_t, bdpl) },
        { "BDPU",  ihda_dump32, offsetof(hda_stream_desc_regs_t, bdpu) },
    };
    static const size_t COLUMNS = 4;
    static const int    COLUMN_WIDTH = 45;
    int done;

    for (size_t i = 0; i < count; i += COLUMNS) {
        size_t todo = MIN(count - i, COLUMNS);

        printf("\n");
        for (size_t j = 0; j < todo; ++j) {
            hda_stream_desc_regs_t* r = regs + i + j;
            done = printf("%s %zu/%zu (base vaddr %p)", name, i + j + 1, count, r);
            if ((j + 1) < todo)
                pad(done, COLUMN_WIDTH);
        }
        printf("\n");

        for (size_t reg = 0; reg < countof(STREAM_REGS); ++reg) {
            for (size_t j = 0; j < todo; ++j) {
                hda_stream_desc_regs_t* r = regs + i + j;
                done = STREAM_REGS[reg].dump_fn(STREAM_REGS[reg].name,
                                                r,
                                                STREAM_REGS[reg].offset,
                                                false);
                if ((j + 1) < todo)
                    pad(done, COLUMN_WIDTH);
            }
            printf("\n");
        }
    }
}

static void ihda_dump_conn_list(intel_hda_widget_t* widget) {
    if (!widget->conn_list_len) {
        printf("empty\n");
        return;
    }

    for (uint i = 0; i < widget->conn_list_len; ++i) {
        printf("%s%hu", i ? " " : "", widget->conn_list[i]);
    }

    printf("\n");
}

static void ihda_dump_amp_caps(intel_hda_codec_amp_caps_t* caps) {
    DEBUG_ASSERT(caps);
    if (!caps->step_size || !caps->num_steps) {
        printf("none\n");
        return;
    } else if (caps->num_steps == 1) {
        printf("fixed 0 dB gain");
    } else {
        static const char* FRAC_LUT[] = { ".00", ".25", ".50", ".75" };
        int start, stop, step;

        step  = caps->step_size;
        start = -((int)caps->offset) * step;
        stop  = start + (((int)caps->num_steps - 1) * step);

        printf("[%d%s, %d%s] dB in %d%s dB steps",
                start >> 2, FRAC_LUT[start & 0x3],
                stop  >> 2, FRAC_LUT[stop  & 0x3],
                step  >> 2, FRAC_LUT[step  & 0x3]);
    }

    printf(" (Can%s mute)\n", caps->can_mute ? "" : "'t");
}

static void ihda_dump_delay(uint8_t delay) {
    if (delay)
        printf("%u samples\n", delay);
    else
        printf("unknown\n");
}

static const char* ihda_get_widget_type_string(uint8_t type_id) {
    switch (type_id) {
        case AW_TYPE_OUTPUT:      return "Audio Output";
        case AW_TYPE_INPUT:       return "Audio Input";
        case AW_TYPE_MIXER:       return "Audio Mixer";
        case AW_TYPE_SELECTOR:    return "Audio Selector";
        case AW_TYPE_PIN_COMPLEX: return "Pin Complex";
        case AW_TYPE_POWER:       return "Power Widget";
        case AW_TYPE_VOLUME_KNOB: return "Volume Knob";
        case AW_TYPE_BEEP_GEN:    return "Beep Generator";
        case AW_TYPE_VENDOR:      return "Vendor";
        default:                  return "Unknown";
    }
}

static const char* ihda_get_fn_group_type_string(intel_hda_codec_audio_fn_group_t* fn_group) {
    uint8_t type_id = fn_group ? fn_group->fn_group_type : 0x00;
    if (type_id >= 0x80)
        return "Vendor";

    switch (type_id) {
        case 0x01: return "Audio";
        case 0x02: return "Modem";
        default:   return "Unknown";
    }
}

#define FMT(fmt) "%s%17s : " fmt, widget_pad
static const char* widget_pad = "+----- ";

static void ihda_dump_widget(intel_hda_codec_audio_fn_group_t* fn_group, uint id) {
    DEBUG_ASSERT(fn_group && fn_group->widgets && (id < fn_group->widget_count));

    intel_hda_widget_t* widget = &fn_group->widgets[id];

    printf("%sWidget %u/%u\n", widget_pad, id + 1, fn_group->widget_count);
    printf(FMT("%hu\n"), "Node ID", widget->nid);
    printf(FMT("[%02x] %s\n"),  "Type", widget->type, ihda_get_widget_type_string(widget->type));

    printf(FMT(""), "Flags");
    DUMP_FLAGS(widget->raw_caps, AW_CAPS_FLAGS, "", "none");

    printf(FMT(""), "Delay");
    ihda_dump_delay(widget->delay);

    printf(FMT("%u\n"), "MaxChan", widget->ch_count);

    if (AW_CAPS_INPUT_AMP_PRESENT(widget->raw_caps)) {
        printf(FMT(""), "InputAmp");
        ihda_dump_amp_caps(&widget->input_amp_caps);
    }

    if (AW_CAPS_OUTPUT_AMP_PRESENT(widget->raw_caps)) {
        printf(FMT(""), "OutputAmp");
        ihda_dump_amp_caps(&widget->output_amp_caps);
    }

    if (AW_CAPS_FORMAT_OVERRIDE(widget->raw_caps)) {
        printf(FMT(""), "PCM Rates");
        DUMP_FLAGS(widget->pcm_size_rate, PCM_RATE_FLAGS, "", "none");

        printf(FMT(""), "PCM Sizes");
        DUMP_FLAGS(widget->pcm_size_rate, PCM_SIZE_FLAGS, " bits", "none");

        printf(FMT(""), "PCM Formats");
        DUMP_FLAGS(widget->pcm_formats, PCM_FMT_FLAGS, "", "none");
    }

    if (widget->type == AW_TYPE_PIN_COMPLEX) {
        printf(FMT(""), "Pin Caps");
        DUMP_FLAGS(widget->pin_caps, PIN_CAPS_FLAGS, "", "none");
    }

    if (AW_CAPS_HAS_POWER_CTL(widget->raw_caps)) {
        printf(FMT(""), "Pwr States");
        DUMP_FLAGS(widget->power_states, POWER_STATE_FLAGS, "", "none");
    }

    if (AW_CAPS_HAS_CONN_LIST(widget->raw_caps)) {
        printf(FMT(""), "ConnList");
        ihda_dump_conn_list(widget);
    }

    if (AW_CAPS_PROC_WIDGET(widget->raw_caps)) {
        printf(FMT("%s\n"), "Can Bypass Proc", widget->can_bypass_processing ? "yes" : "no");
        printf(FMT("%u\n"), "Proc Coefficients", widget->processing_coefficient_count);
    }

    if (widget->type == AW_TYPE_VOLUME_KNOB) {
        printf(FMT("%s\n"), "Vol Knob Type",  widget->vol_knob_is_delta ? "delta" : "absolute");
        printf(FMT("%u\n"), "Vol Knob Steps", widget->vol_knob_steps);
    }

    printf("%s\n", widget_pad);
}
#undef FMT

#define FMT(fmt) "%s%26s : " fmt, pad
static void ihda_dump_codec_fn_group(intel_hda_codec_t* codec, uint id) {
    DEBUG_ASSERT(codec && codec->fn_groups && (id < codec->fn_group_count));
    static const char* pad = "+--- ";
    intel_hda_codec_audio_fn_group_t* fn_group = codec->fn_groups[id];

    printf("%sFunction Group %u/%u\n", pad, id + 1, codec->fn_group_count);
    printf(FMT("%u\n"), "Node ID", codec->fn_group_starting_id + id);
    printf(FMT("%s\n"),  "Type", ihda_get_fn_group_type_string(fn_group));

    if (!fn_group)
        return;

    printf(FMT("Can%s send unsolicited responses\n"), "Unsol", fn_group->can_send_unsolicited ? "" : "not");
    printf(FMT("%s\n"), "Beep Gen", fn_group->has_beep_gen ? "yes" : "no");

    printf(FMT(""), "Input Path Delay");
    ihda_dump_delay(fn_group->path_input_delay);

    printf(FMT(""), "Output Path Delay");
    ihda_dump_delay(fn_group->path_output_delay);

    printf(FMT(""), "Default PCM Rates");
    DUMP_FLAGS(fn_group->default_pcm_size_rate, PCM_RATE_FLAGS, "", "none");

    printf(FMT(""), "Default PCM Sizes");
    DUMP_FLAGS(fn_group->default_pcm_size_rate, PCM_SIZE_FLAGS, " bits", "none");

    printf(FMT(""), "Default PCM Formats");
    DUMP_FLAGS(fn_group->default_pcm_formats, PCM_FMT_FLAGS, "", "none");

    printf(FMT(""), "Default Input Amp Caps");
    ihda_dump_amp_caps(&fn_group->default_input_amp_caps);

    printf(FMT(""), "Default Output Amp Caps");
    ihda_dump_amp_caps(&fn_group->default_output_amp_caps);

    printf(FMT(""), "Supported Power States");
    DUMP_FLAGS(fn_group->power_states, POWER_STATE_FLAGS, "", "none");

    printf(FMT("%u\n"), "GPIOs", fn_group->gpio_count);
    printf(FMT("%u\n"), "GPIs",  fn_group->gpi_count);
    printf(FMT("%u\n"), "GPOs",  fn_group->gpo_count);
    printf(FMT("%s\n"), "GPIOs can wake", fn_group->gpio_can_wake ? "yes" : "no");
    printf(FMT("%s\n"), "GPIOs can send unsolicited",
           fn_group->gpio_can_send_unsolicited ? "yes" : "no");

    printf(FMT("%u\n"), "Widgets", fn_group->widget_count);

    for (uint16_t i = 0; i < fn_group->widget_count; ++i) {
        ihda_dump_widget(fn_group, i);
    }
}
#undef FMT

#define FMT(fmt) "%s%10s : " fmt, pad
static void ihda_dump_codec(intel_hda_codec_t* codec) {
    static const char* pad = "+- ";

    printf(FMT("0x%04hx:0x%04hx\n"), "VID/DID", codec->vendor_id, codec->device_id);
    printf(FMT("%u.%u\n"), "Rev", codec->major_rev, codec->minor_rev);
    printf(FMT("%u.%u\n"), "Vendor Rev", codec->vendor_rev_id, codec->vendor_stepping_id);
    printf("%s%u function group%s\n",
           pad,  codec->fn_group_count, codec->fn_group_count == 1 ? "" : "s");

    for (uint16_t i = 0; i < codec->fn_group_count; ++i) {
        ihda_dump_codec_fn_group(codec, i);
    }
}
#undef FMT

static void ihda_dump_codecs(intel_hda_device_t* dev) {
    /* Note: This is only safe because we do not currently support hot
     * unplugging of codecs.  Once a codec exists, it cannot cease to exist.
     * Neither can any of its function groups, nor their widgets.  If/when hot
     * unplugging of codecs becomes a thing, this code will need to be
     * revisited.
     */
    DEBUG_ASSERT(dev);

    /* Count the number of active codecs */
    uint codec_count = 0;
    for (size_t i = 0; i < countof(dev->codecs); ++i)
        if (dev->codecs[i])
            codec_count++;

    printf("Intel HDA Audio Controller @%02x:%02x.%01x has %u active codec%s\n",
            dev->pci_device->bus_id,
            dev->pci_device->dev_id,
            dev->pci_device->func_id,
            codec_count,
            codec_count == 1 ? "" : "s");

    /* Print the header for each active codec, then proceed to dumping the
     * function groups */
    uint codec_ndx = 0;
    for (size_t i = 0; i < countof(dev->codecs); ++i) {
        intel_hda_codec_t* codec = dev->codecs[i];

        if (codec) {
            codec_ndx++;
            printf("Codec %u/%u (Codec Address %zu) has %u function group%s\n",
                    codec_ndx, codec_count, i, codec->fn_group_count,
                    codec->fn_group_count == 1 ? "" : "s");
            ihda_dump_codec(codec);
        }
    }
}

static int cmd_ihda_regs(int argc, const cmd_args *argv) {
    DEBUG_ASSERT(argc >= 2);

    long dev_id = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp("-d", argv[i].str) && (++i < argc)) {
            dev_id = argv[i].i;
        } else {
            goto usage;
        }
    }

    intel_hda_device_t* dev = intel_hda_acquire(dev_id);
    if (dev) {
        hda_registers_t* r = dev->regs;

        DEBUG_ASSERT(r);
        printf("Registers for Intel HDA Device #%ld (base vaddr %p)\n", dev_id, r);

        ihda_dump16("GCAP",       r, offsetof(hda_registers_t, gcap), true);
        ihda_dump8 ("VMIN",       r, offsetof(hda_registers_t, vmin), true);
        ihda_dump8 ("VMAJ",       r, offsetof(hda_registers_t, vmaj), true);
        ihda_dump16("OUTPAY",     r, offsetof(hda_registers_t, outpay), true);
        ihda_dump16("INPAY",      r, offsetof(hda_registers_t, inpay), true);
        ihda_dump32("GCTL",       r, offsetof(hda_registers_t, gctl), true);
        ihda_dump16("WAKEEN",     r, offsetof(hda_registers_t, wakeen), true);
        ihda_dump16("STATESTS",   r, offsetof(hda_registers_t, statests), true);
        ihda_dump16("GSTS",       r, offsetof(hda_registers_t, gsts), true);
        ihda_dump16("OUTSTRMPAY", r, offsetof(hda_registers_t, outstrmpay), true);
        ihda_dump16("INSTRMPAY",  r, offsetof(hda_registers_t, instrmpay), true);
        ihda_dump32("INTCTL",     r, offsetof(hda_registers_t, intctl), true);
        ihda_dump32("INTSTS",     r, offsetof(hda_registers_t, intsts), true);
        ihda_dump32("WALCLK",     r, offsetof(hda_registers_t, walclk), true);
        ihda_dump32("SSYNC",      r, offsetof(hda_registers_t, ssync), true);
        ihda_dump32("CORBLBASE",  r, offsetof(hda_registers_t, corblbase), true);
        ihda_dump32("CORBUBASE",  r, offsetof(hda_registers_t, corbubase), true);
        ihda_dump16("CORBWP",     r, offsetof(hda_registers_t, corbwp), true);
        ihda_dump16("CORBRP",     r, offsetof(hda_registers_t, corbrp), true);
        ihda_dump8 ("CORBCTL",    r, offsetof(hda_registers_t, corbctl), true);
        ihda_dump8 ("CORBSTS",    r, offsetof(hda_registers_t, corbsts), true);
        ihda_dump8 ("CORBSIZE",   r, offsetof(hda_registers_t, corbsize), true);
        ihda_dump32("RIRBLBASE",  r, offsetof(hda_registers_t, rirblbase), true);
        ihda_dump32("RIRBUBASE",  r, offsetof(hda_registers_t, rirbubase), true);
        ihda_dump16("RIRBWP",     r, offsetof(hda_registers_t, rirbwp), true);
        ihda_dump16("RINTCNT",    r, offsetof(hda_registers_t, rintcnt), true);
        ihda_dump8 ("RIRBCTL",    r, offsetof(hda_registers_t, rirbctl), true);
        ihda_dump8 ("RIRBSTS",    r, offsetof(hda_registers_t, rirbsts), true);
        ihda_dump8 ("RIRBSIZE",   r, offsetof(hda_registers_t, rirbsize), true);
        ihda_dump32("ICOI",       r, offsetof(hda_registers_t, icoi), true);
        ihda_dump32("ICII",       r, offsetof(hda_registers_t, icii), true);
        ihda_dump16("ICIS",       r, offsetof(hda_registers_t, icis), true);
        ihda_dump32("DPIBLBASE",  r, offsetof(hda_registers_t, dpiblbase), true);
        ihda_dump32("DPIBUBASE",  r, offsetof(hda_registers_t, dpibubase), true);

        ihda_dump_stream_regs("Input Stream",  dev->input_strm_cnt,  dev->input_strm_regs);
        ihda_dump_stream_regs("Output Stream", dev->output_strm_cnt, dev->output_strm_regs);
        ihda_dump_stream_regs("Bi-dir Stream", dev->bidir_strm_cnt,  dev->bidir_strm_regs);

        intel_hda_release(dev);
    } else {
        printf("Intel HDA Device #%ld not found!\n", dev_id);
    }

    return NO_ERROR;

usage:
    printf("usage: %s %s [-d <dev_id>]\n", argv[0].str, argv[1].str);
    return NO_ERROR;
}

static int cmd_ihda_codecs(int argc, const cmd_args *argv) {
    DEBUG_ASSERT(argc >= 2);

    long dev_id = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp("-d", argv[i].str) && (++i < argc)) {
            dev_id = argv[i].i;
        } else {
            goto usage;
        }
    }

    intel_hda_device_t* dev = intel_hda_acquire(dev_id);
    if (dev) {
        ihda_dump_codecs(dev);
        intel_hda_release(dev);
    } else {
        printf("Intel HDA Device #%ld not found!\n", dev_id);
    }

    return NO_ERROR;

usage:
    printf("usage: %s %s [-d <dev_id>]\n", argv[0].str, argv[1].str);
    return NO_ERROR;
}


static int cmd_ihda(int argc, const cmd_args *argv)
{
    static const struct {
        const char* name;
        int (*subcmd)(int, const cmd_args*);
    } SUBCMDS[] = {
        { "list",   cmd_ihda_list },
        { "regs",   cmd_ihda_regs },
        { "codecs", cmd_ihda_codecs },
    };

    if (argc >= 2) {
        for (size_t i = 0; i < countof(SUBCMDS); ++i)
            if (!strcmp(argv[1].str, SUBCMDS[i].name))
                return SUBCMDS[i].subcmd(argc, argv);
    }

    printf("usage: %s <cmd> [args]\n"
           "Valid cmds are...\n"
           "\thelp   : Show this message\n"
           "\tlist   : List currently active device IDs\n"
           "\tregs   : Dump the registers for the specified device ID\n"
           "\tcodecs : Dump the codec description for the specified device ID\n",
           argv[0].str);

    return NO_ERROR;
}

STATIC_COMMAND_START
STATIC_COMMAND("ihda",
               "Low level commands to manipulate Intel High Definition Audio devices",
                &cmd_ihda)
STATIC_COMMAND_END(intel_hda_commands);

#endif  // WITH_LIB_CONSOLE
