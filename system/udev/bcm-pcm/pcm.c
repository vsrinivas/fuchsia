// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hw/arch_ops.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/device/audio2.h>
#include <magenta/device/i2c.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <magenta/threads.h>

#include <bcm/bcm28xx.h>
#include <bcm/clockman.h>
#include <bcm/dma.h>
#include <bcm/gpio.h>
#include <bcm/ioctl.h>

#include "codec/hifi-berry.h"
#include "pcm.h"

#define TRACE 0
// clang-format off
#define xprintf(fmt...) \
    do { \
        if (TRACE) { \
            printf("BCMPCM: "fmt); \
        } \
    } while (false)

#define BCM_FIFO_DEADLINE_MS 100

//ideally we would get this from system, just putting it here now for reference
#define BCM_PCM_PAGE_SIZE   4096

// Raspberry Pi reference crystal 19.2MHz
#define BCM_PCM_REF_FREQUENCY   19200000
#define BCM_PCM_BCLK_PER_FRAME  64

#define DMA_CHAN 11
#define PCM_TRACE 0
// clang-format on

typedef union {
    audio2_cmd_hdr_t hdr;
    audio2_rb_cmd_get_buffer_req_t get_buffer_req;
    audio2_stream_cmd_set_format_req_t set_fmt_req;
    audio2_rb_cmd_start_req_t start_req;
    audio2_rb_cmd_stop_req_t stop_req;
    audio2_rb_cmd_get_fifo_depth_req_t get_fifo_req;
} buffer_packet_t;

typedef struct {

    mx_device_t* mxdev;
    mx_device_t* parent;
    bcm_pcm_regs_t* control_regs;
    bcm_gpio_ctrl_t* gpio_regs;
    volatile void* clock_regs;

    bcm_dma_t dma;

    mx_handle_t stream_ch;
    mx_handle_t buffer_ch;
    mx_handle_t pcm_port;

    mx_handle_t buffer_vmo;
    size_t buffer_size; // size of buffer in bytes
    uint32_t buffer_notifications;

    thrd_t notify_thrd;
    thrd_t port_thrd;
    volatile atomic_bool notify_running;

    mtx_t pcm_lock;

    volatile uint32_t state;

    uint32_t* sample_rates;
    int sample_rate_count;

    uint32_t sample_rate;
    int num_channels;
    int audio_frame_size; // size of an audio frame

} bcm_pcm_t;

static mx_status_t pcm_dma_init(bcm_pcm_t* ctx);
static mx_status_t pcm_deinit_buffer_locked(bcm_pcm_t* ctx);

static void set_pcm_clock(bcm_pcm_t* pcm_ctx) {

    volatile uint32_t* pcmclk = (uint32_t*)(pcm_ctx->clock_regs + BCM_CLOCKMAN_PCMCTL);
    volatile uint32_t* pcmdiv = (uint32_t*)(pcm_ctx->clock_regs + BCM_CLOCKMAN_PCMDIV);
    //Set the divider as a 4.12 number

    uint64_t divider = ((uint64_t)BCM_PCM_REF_FREQUENCY * 4096) /
                        (pcm_ctx->sample_rate * BCM_PCM_BCLK_PER_FRAME);

    //Disable the clock so we can change its source and divider
    *pcmclk = BCM_CLOCKMAN_PASSWORD     |
              BCM_CLOCKMAN_CONTROL_KILL |
              BCM_CLOCKMAN_CONTROL_SRC_OSC;
    hw_wmb();
    //Write divider value (4.12 number)
    *pcmdiv = BCM_CLOCKMAN_PASSWORD | (uint32_t)divider;
    hw_wmb();
    //Enable the clock with new settings
    *pcmclk = BCM_CLOCKMAN_PASSWORD |
              BCM_CLOCKMAN_CONTROL_MASH_ONE_STAGE |
              BCM_CLOCKMAN_CONTROL_ENAB |
              BCM_CLOCKMAN_CONTROL_SRC_OSC;
    hw_wmb();
}

static void pcm_close_handle(mx_handle_t* handle) {
    if (*handle != MX_HANDLE_INVALID) {
        mx_handle_close(*handle);
        *handle = MX_HANDLE_INVALID;
    }
}

static void pcm_deinit_locked(bcm_pcm_t* ctx) {

    xprintf("deiniting buffer\n");
    pcm_deinit_buffer_locked(ctx);

    pcm_close_handle(&ctx->buffer_ch);

    xprintf("closing stream\n");
    pcm_close_handle(&ctx->stream_ch);

    xprintf("closing port\n");
    pcm_close_handle(&ctx->pcm_port);

    ctx->state = BCM_PCM_STATE_SHUTDOWN;

    xprintf("done with deinit\n");

}

static void pcm_deinit(bcm_pcm_t* ctx) {

    mtx_lock(&ctx->pcm_lock);
    pcm_deinit_locked(ctx);
    mtx_unlock(&ctx->pcm_lock);
}

static int pcm_notify_thread(void* arg) {

    bcm_pcm_t* ctx = arg;

    uint32_t offset = 0;
    ctx->notify_running = true;

    mx_status_t status = NO_ERROR;

    double notify_time = (1000000.0 * ctx->buffer_size) /
                         (ctx->sample_rate * ctx->audio_frame_size * ctx->buffer_notifications);

    uint64_t notify_period_us = (uint64_t)notify_time;

    xprintf("notification interval = %lu  %fuS\n", notify_period_us, notify_time);
    xprintf("buffer size = %lu\n", ctx->buffer_size);
    xprintf("sample rate = %u\n", ctx->sample_rate);
    xprintf("notifications = %u\n", ctx->buffer_notifications);

    while ((ctx->state & BCM_PCM_STATE_RUNNING) && !(ctx->state & BCM_PCM_STATE_SHUTTING_DOWN)) {
        mx_nanosleep(mx_deadline_after(MX_USEC(notify_period_us)));

        mx_paddr_t pos = bcm_dma_get_position(&ctx->dma);
        bcm_dma_paddr_to_offset(&ctx->dma, pos, &offset);

        audio2_rb_position_notify_t resp;
        resp.hdr.cmd = AUDIO2_RB_POSITION_NOTIFY;
        resp.ring_buffer_pos = offset;

        status = mx_channel_write(ctx->buffer_ch, 0, &resp, sizeof(resp), NULL, 0);
        if (status != NO_ERROR)
            break;
    }
    xprintf("notification thread shutting down\n");
    ctx->notify_running = false;
    return 0;
}

static mx_status_t pcm_get_fifo_depth(bcm_pcm_t* ctx, audio2_rb_cmd_get_fifo_depth_req_t req) {
    audio2_rb_cmd_get_fifo_depth_resp_t resp;
    resp.hdr = req.hdr;
    resp.result = NO_ERROR;
    resp.fifo_depth = 64;

    return mx_channel_write(ctx->buffer_ch, 0, &resp, sizeof(resp), NULL, 0);
}

static mx_status_t pcm_stop_locked(bcm_pcm_t* ctx) {
    mx_status_t res;

    if (!(ctx->state & BCM_PCM_STATE_RUNNING)) {
        res = ERR_BAD_STATE;
    } else {
        ctx->state &= ~BCM_PCM_STATE_RUNNING;
        if (ctx->notify_running) {
            thrd_join(ctx->notify_thrd, NULL);
        }
        hifiberry_stop();
        bcm_dma_stop(&ctx->dma);

        res = NO_ERROR;
    }
    return res;
}

static mx_status_t pcm_stop_req(bcm_pcm_t* ctx, audio2_rb_cmd_stop_req_t req) {

    mtx_lock(&ctx->pcm_lock);
    mx_status_t res;

    res = pcm_stop_locked(ctx);

    audio2_rb_cmd_stop_resp_t resp;
    resp.result = res;
    resp.hdr = req.hdr;

    res = mx_channel_write(ctx->buffer_ch, 0, &resp, sizeof(resp), NULL, 0);
    mtx_unlock(&ctx->pcm_lock);
    return res;
}

static mx_status_t pcm_start(bcm_pcm_t* ctx, audio2_rb_cmd_start_req_t req) {

    audio2_rb_cmd_start_resp_t resp;
    mx_status_t status = NO_ERROR;

    mtx_lock(&ctx->pcm_lock);

    if (ctx->state & BCM_PCM_STATE_RUNNING) {
        status = ERR_BAD_STATE;
        goto pcm_start_out;
    }
    // Enable and clear error flags
    ctx->control_regs->cs = BCM_PCM_CS_ENABLE | BCM_PCM_CS_DMAEN |
                            BCM_PCM_CS_RXERR  | BCM_PCM_CS_TXERR;
    hw_wmb();
    bcm_dma_start(&ctx->dma);
    // turn on i2s transmitter
    ctx->control_regs->cs = BCM_PCM_CS_ENABLE | BCM_PCM_CS_DMAEN | BCM_PCM_CS_TXON;
    hw_wmb();
    //i2s is running at this point
    resp.start_ticks = mx_ticks_get();
    ctx->state |= BCM_PCM_STATE_RUNNING;

    hifiberry_start();

    if (ctx->buffer_notifications > 0) {

        int thrd_rc = thrd_create_with_name(&ctx->notify_thrd,
                                            pcm_notify_thread, ctx,
                                            "pcm_notify_thread");
        if (thrd_rc != thrd_success) {
            hifiberry_stop();
            bcm_dma_stop(&ctx->dma);
            ctx->state &= BCM_PCM_STATE_RUNNING;
            status = thrd_status_to_mx_status(thrd_rc);
        }
    }

pcm_start_out:
    resp.result = status;
    resp.hdr = req.hdr;

    status = mx_channel_write(ctx->buffer_ch, 0, &resp, sizeof(resp), NULL, 0);
    mtx_unlock(&ctx->pcm_lock);
    return status;
}

static mx_status_t pcm_deinit_buffer_locked(bcm_pcm_t* ctx) {

    ctx->state |= BCM_PCM_STATE_SHUTTING_DOWN;

    hifiberry_release();

    if (ctx->notify_running) {
        xprintf("waiting on notify thread shutdown\n");
        thrd_join(ctx->notify_thrd, NULL);
    }

    //NOTE: Always shut down the dma before stopping the pcm
    if (ctx->dma.state != BCM_DMA_STATE_SHUTDOWN) {
        xprintf("Deiniting DMA...\n");
        bcm_dma_deinit(&ctx->dma);
    }

    // Turn off PCM TX/RX, Clear FIFOs, Clear Errors
    ctx->control_regs->cs = BCM_PCM_CS_ENABLE | BCM_PCM_CS_TXCLR | BCM_PCM_CS_RXCLR;

    ctx->control_regs->mode = BCM_PCM_MODE_INITIAL_STATE;
    ctx->control_regs->txc = BCM_PCM_TXC_INITIAL_STATE;
    ctx->control_regs->rxc = BCM_PCM_RXC_INITIAL_STATE;
    ctx->control_regs->dreq_lvl = BCM_PCM_DREQ_LVL_INITIAL_STATE;
    ctx->control_regs->cs = BCM_PCM_CS_INITIAL_STATE;

    pcm_close_handle(&ctx->buffer_vmo);

    ctx->state &= ~BCM_PCM_STATE_SHUTTING_DOWN;

    return NO_ERROR;
}

static mx_status_t pcm_set_stream_fmt(bcm_pcm_t* ctx, audio2_stream_cmd_set_format_req_t req) {
    mx_status_t status;
    audio2_stream_cmd_set_format_resp_t resp;
    mx_handle_t ret_handle = MX_HANDLE_INVALID;

    mtx_lock(&ctx->pcm_lock);

    if (!hifiberry_is_valid_mode(req)) {
        status = ERR_NOT_SUPPORTED;
        xprintf("Mode not supported\n");
        goto set_stream_done;
    }

    if (ctx->buffer_ch != MX_HANDLE_INVALID) {
        if (ctx->state & BCM_PCM_STATE_RUNNING) {
            // Currently running a previous configuration, client needs to issue a stop
            //  before attempting a new set_strem_fmt.
            status = ERR_BAD_STATE;
            xprintf("Already running with valid buffer\n");
            goto set_stream_done;
        } else {
            // We weren't running, but there was a buffer/buffer_ch configured,
            //  need to clear out previous state.
            pcm_deinit_buffer_locked(ctx);
            pcm_close_handle(&ctx->buffer_ch);
        }
    }

    ctx->sample_rate = req.frames_per_second;
    ctx->num_channels = req.channels;
    //TODO(hollande) After adding more modes, use the format size instead of constant
    ctx->audio_frame_size = ctx->num_channels * 2;
    set_pcm_clock(ctx);

    // TODO(hollande) This will need to change once we have new modes
    ctx->control_regs->cs = BCM_PCM_CS_ENABLE | BCM_PCM_CS_TXCLR;
    ctx->control_regs->mode = BCM_PCM_MODE_I2S_16BIT_64BCLK;
    ctx->control_regs->txc = BCM_PCM_TXC_I2S_16BIT_64BCLK;
    ctx->control_regs->cs = BCM_PCM_CS_ENABLE | BCM_PCM_CS_TXCLR;

    //Allow pcm state to sync before turning on DMA to prevent frame sync
    // issues.  (only and issue when a frame is > 32-bits, but leaving here for reference)
    mx_nanosleep(mx_deadline_after(MX_MSEC(10)));

    status = pcm_dma_init(ctx);
    if (status != NO_ERROR)
        goto set_stream_fail;

    // Might make sense to split the codec init vs codec start
    status = hifiberry_init();
    if (status != NO_ERROR)
        goto set_stream_fail;

    status = mx_channel_create(0, &ctx->buffer_ch, &ret_handle);
    if (status != NO_ERROR)
        goto set_stream_fail;

    status = mx_port_bind(ctx->pcm_port, (uint64_t)ctx->buffer_ch, ctx->buffer_ch, MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);
    if (status == NO_ERROR)
        goto set_stream_done;

set_stream_fail:
    xprintf("set stream FAIL\n");
    pcm_deinit_buffer_locked(ctx);
    pcm_close_handle(&ctx->buffer_ch);

set_stream_done:
    resp.hdr.transaction_id = req.hdr.transaction_id;
    resp.hdr.cmd = AUDIO2_STREAM_CMD_SET_FORMAT;
    resp.result = status;

    status = mx_channel_write(ctx->stream_ch, 0, &resp, sizeof(resp), &ret_handle, 1);
    mtx_unlock(&ctx->pcm_lock);
    return status;
}

static void pcm_audio_sink_release(void* ctx) {
    bcm_pcm_t* pcm = ctx;
    free(pcm);
}

static void pcm_audio_sink_unbind(void* ctx) {
    bcm_pcm_t* pcm = ctx;

    mtx_lock(&pcm->pcm_lock);
    pcm_stop_locked(pcm);
    pcm_deinit_locked(pcm);
    mtx_unlock(&pcm->pcm_lock);

    device_remove(pcm->mxdev);
}

static mx_status_t pcm_get_buffer(bcm_pcm_t* ctx, audio2_rb_cmd_get_buffer_req_t req) {

    mtx_lock(&ctx->pcm_lock);

    mx_status_t status = NO_ERROR;

    audio2_rb_cmd_get_buffer_resp_t resp;
    resp.hdr = req.hdr;

    if (ctx->buffer_vmo != MX_HANDLE_INVALID) {
        if (ctx->state & BCM_PCM_STATE_RUNNING) {
            status = ERR_BAD_STATE;
            goto gb_fail2; // Already running, don't interrupt, but return bad state.
        } else {
            // We already have a buffer configured, need to clean it up.
            pcm_deinit_buffer_locked(ctx);
            status = pcm_dma_init(ctx);
            if (status != NO_ERROR)
                goto gb_fail2;
        }
    }

    ctx->buffer_size = req.min_ring_buffer_frames * ctx->audio_frame_size;

    status = mx_vmo_create(ctx->buffer_size, 0, &ctx->buffer_vmo);
    if (status != NO_ERROR)
        goto gb_fail;

    status = mx_vmo_op_range(ctx->buffer_vmo, MX_VMO_OP_COMMIT, 0, ctx->buffer_size, NULL, 0);
    if (status != NO_ERROR)
        goto gb_fail;

    mx_handle_t ret_handle;
    status = mx_handle_duplicate(ctx->buffer_vmo, MX_RIGHT_TRANSFER |
                                                  MX_RIGHT_READ     |
                                                  MX_RIGHT_WRITE    |
                                                  MX_RIGHT_MAP,
                                                  &ret_handle);
    if (status != NO_ERROR)
        goto gb_fail;

    xprintf("created %lu byte vmo\n", ctx->buffer_size);

    ctx->buffer_notifications = req.notifications_per_ring;

    uint32_t transfer_info = BCM_DMA_DREQ_ID_PCM_TX << 16 |
                             BCM_DMA_TI_DEST_DREQ |
                             BCM_DMA_TI_SRC_INC |
                             BCM_DMA_TI_WAIT_RESP;

    mx_paddr_t dest_addr = BCM_PERIPH_BASE_BUS |
                          (BCM_PERIPH_ADDR_MASK & (uintptr_t)&((bcm_pcm_regs_t*)I2S_BASE)->fifo);

    status = bcm_dma_init_vmo_to_fifo_trans(&ctx->dma, ctx->buffer_vmo, transfer_info, dest_addr,
                                            BCM_DMA_FLAGS_USE_MEM_INDEX |
                                                BCM_DMA_FLAGS_CIRCULAR);
    if (status != NO_ERROR) {
        xprintf("VMO dma linking failed (%d)\n", status);
        goto gb_fail;
    }
    resp.result = status;
    status = mx_channel_write(ctx->buffer_ch, 0, &resp, sizeof(resp), &ret_handle, 1);
    mtx_unlock(&ctx->pcm_lock);
    return status;

gb_fail:
    pcm_close_handle(&ctx->buffer_vmo);
gb_fail2:
    resp.result = status;
    status = mx_channel_write(ctx->buffer_ch, 0, &resp, sizeof(resp), NULL, 0);
    mtx_unlock(&ctx->pcm_lock);
    return status;
}

#define HANDLE_REQ(_ioctl, _payload, _handler)      \
    case _ioctl:                                    \
        if (req_size != sizeof(req._payload)) {     \
            printf("Bad " #_payload                 \
                   " reqonse length (%u != %zu)\n", \
                   req_size, sizeof(req._payload)); \
            return ERR_INVALID_ARGS;                \
        }                                           \
        _handler(ctx, req._payload);                \
        break;
static int pcm_port_thread(void* arg) {

    bcm_pcm_t* ctx = arg;
    mx_status_t status;

    mx_io_packet_t port_out;

    buffer_packet_t req;
    xprintf("Port thread running\n");
    while ((ctx->stream_ch != MX_HANDLE_INVALID) || (ctx->buffer_ch != MX_HANDLE_INVALID)) {
        status = mx_port_wait(ctx->pcm_port, MX_TIME_INFINITE, &port_out, sizeof(port_out));
        if (status != NO_ERROR)
            break;

        mx_handle_t channel = (mx_handle_t)port_out.hdr.key;

        uint32_t req_size;

        if (port_out.signals == MX_CHANNEL_READABLE) {

            status = mx_channel_read(channel, 0, &req, NULL, sizeof(req), 0, &req_size, NULL);
            if (status != NO_ERROR) {
                if (channel == ctx->buffer_ch) {
                    xprintf("error reading buffer channel...\n");
                    break;
                } else {
                    xprintf("error reading stream channel...closing channel\n");
                    pcm_close_handle(&ctx->stream_ch);
                }
            }

            if (channel == ctx->stream_ch) {
                switch (req.hdr.cmd) {
                    HANDLE_REQ(AUDIO2_STREAM_CMD_SET_FORMAT, set_fmt_req, pcm_set_stream_fmt);
                default:
                    xprintf("unrecognized stream command\n");
                    break;
                }
            } else if (channel == ctx->buffer_ch) {
                switch (req.hdr.cmd) {
                    HANDLE_REQ(AUDIO2_RB_CMD_START, start_req, pcm_start);
                    HANDLE_REQ(AUDIO2_RB_CMD_STOP, stop_req, pcm_stop_req);
                    HANDLE_REQ(AUDIO2_RB_CMD_GET_BUFFER, get_buffer_req, pcm_get_buffer);
                    HANDLE_REQ(AUDIO2_RB_CMD_GET_FIFO_DEPTH, get_fifo_req, pcm_get_fifo_depth);
                case AUDIO2_RB_POSITION_NOTIFY:
                default:
                    xprintf("unrecognized buffer command\n");
                    break;
                }
            }
        } else if (port_out.signals == MX_CHANNEL_PEER_CLOSED) {
            if (channel == ctx->stream_ch) {
                xprintf("stream channel closed by peer\n");
                pcm_close_handle(&ctx->stream_ch);
            }
            if (channel == ctx->buffer_ch) {
                xprintf("buffer channel closed by peer\n");
                break; //need to tear the pcm session down
            }
        }
    }
    xprintf("tearing down...\n");

    pcm_deinit(ctx);

    xprintf("done\n");

    return 0;
}
#undef HANDLE_REQ

static mx_status_t pcm_audio2_sink_ioctl(void* ctx, uint32_t op,
                                         const void* in_buf, size_t in_len,
                                         void* out_buf, size_t out_len, size_t* out_actual) {

    bcm_pcm_t* pcm = ctx;
    mtx_lock(&pcm->pcm_lock);

    mx_status_t status = NO_ERROR;
    mx_handle_t* reply = out_buf;

    if (op != AUDIO2_IOCTL_GET_CHANNEL) {
        status = ERR_INVALID_ARGS;
        goto pcm_ioctl_end;
    }

    if (pcm->state != BCM_PCM_STATE_SHUTDOWN) {
        status = ERR_BAD_STATE;
        goto pcm_ioctl_end;
    }

    MX_DEBUG_ASSERT(pcm->stream_ch == MX_HANDLE_INVALID);
    MX_DEBUG_ASSERT(pcm->pcm_port == MX_HANDLE_INVALID);

    mx_handle_t ret_handle;
    status = mx_channel_create(0, &pcm->stream_ch, &ret_handle);
    if (status != NO_ERROR) {
        status = ERR_INTERNAL;
        goto pcm_ioctl_end;
    }
    *reply = ret_handle;

    status = mx_port_create(0, &pcm->pcm_port);
    if (status != NO_ERROR) {
        xprintf("error creating port\n");
        mx_handle_close(pcm->stream_ch);
        goto pcm_ioctl_end;
    }

    status = mx_port_bind(pcm->pcm_port, (uint64_t)pcm->stream_ch, pcm->stream_ch,
                                MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);

    if (status != NO_ERROR) {
        xprintf("error binding port to stream_ch\n");
        mx_handle_close(pcm->stream_ch);
        mx_handle_close(pcm->pcm_port);
        goto pcm_ioctl_end;
    }

    int thrd_rc = thrd_create_with_name(&pcm->port_thrd,
                                        pcm_port_thread, pcm,
                                        "pcm_port_thread");
    if (thrd_rc != thrd_success) {
        mx_handle_close(pcm->stream_ch);
        mx_handle_close(pcm->pcm_port);
        status = thrd_status_to_mx_status(thrd_rc);
        goto pcm_ioctl_end;
    }
    pcm->state |= BCM_PCM_STATE_CLIENT_ACTIVE;
    xprintf("Client request successful...\n");
pcm_ioctl_end:
    mtx_unlock(&pcm->pcm_lock);
    if (status == NO_ERROR) {
        *out_actual = sizeof(ret_handle);
    } else {
        xprintf("Problem with client request: status=%d\n", status);
    }
    return status;
}

static mx_status_t pcm_dma_init(bcm_pcm_t* ctx) {

    mx_status_t status = bcm_dma_init(&ctx->dma, DMA_CHAN);
    if (status != NO_ERROR)
        return status;

    return NO_ERROR;
}

static mx_protocol_device_t pcm_audio_ctx_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = pcm_audio_sink_unbind,
    .release = pcm_audio_sink_release,
    .ioctl = pcm_audio2_sink_ioctl,
};

static int pcm_bootstrap_thread(void* arg) {

    assert(arg);

    bcm_pcm_t* pcm_ctx = (bcm_pcm_t*)arg;

    // Carve out some address space for the clock control registers
    mx_status_t status = mx_mmap_device_memory(
        get_root_resource(),
        BCM_CM_BASE, 0x1000,
        MX_CACHE_POLICY_UNCACHED_DEVICE, (uintptr_t*)&pcm_ctx->clock_regs);

    if (status != NO_ERROR)
        goto pcm_err;

    // Carve out some address space for the device -- it's memory mapped.
    status = mx_mmap_device_memory(
        get_root_resource(),
        GPIO_BASE, 0x1000,
        MX_CACHE_POLICY_UNCACHED_DEVICE, (uintptr_t*)&pcm_ctx->gpio_regs);

    if (status != NO_ERROR)
        goto pcm_err;

    /* ALT Function 0 is PCM for these pins */
    set_gpio_function(pcm_ctx->gpio_regs, BCM_PCM_CLK_ALT0_PIN, FSEL_ALT0);
    set_gpio_function(pcm_ctx->gpio_regs, BCM_PCM_FS_ALT0_PIN, FSEL_ALT0);
    set_gpio_function(pcm_ctx->gpio_regs, BCM_PCM_DIN_ALT0_PIN, FSEL_ALT0);
    set_gpio_function(pcm_ctx->gpio_regs, BCM_PCM_DOUT_ALT0_PIN, FSEL_ALT0);

    status = mx_mmap_device_memory(
        get_root_resource(),
        I2S_BASE, 0x1000,
        MX_CACHE_POLICY_UNCACHED_DEVICE, (uintptr_t*)&pcm_ctx->control_regs);

    if (status != NO_ERROR)
        goto pcm_err;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "pcm0",
        .ctx = pcm_ctx,
        .ops = &pcm_audio_ctx_device_proto,
        .proto_id = MX_PROTOCOL_AUDIO2_OUTPUT,
    };

    status = device_add(pcm_ctx->parent, &args, &pcm_ctx->mxdev);
    if (status != NO_ERROR)
        goto pcm_err;

    return 0;

pcm_err:
    if (pcm_ctx) {
        free(pcm_ctx);
    }

    return -1;
}

static mx_status_t bcm_pcm_bind(void* ctx, mx_device_t* parent, void** cookie) {

    bcm_pcm_t* pcm_ctx = calloc(1, sizeof(*pcm_ctx));
    if (!pcm_ctx)
        return ERR_NO_MEMORY;

    pcm_ctx->parent = parent;

    thrd_t bootstrap_thrd;
    int thrd_rc = thrd_create_with_name(&bootstrap_thrd,
                                        pcm_bootstrap_thread, pcm_ctx,
                                        "pcm_bootstrap_thread");
    if (thrd_rc != thrd_success) {
        free(pcm_ctx);
        return thrd_status_to_mx_status(thrd_rc);
    }

    thrd_detach(bootstrap_thrd);
    return NO_ERROR;
}

static mx_driver_ops_t bcm_pcm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bcm_pcm_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(bcm_pcm, bcm_pcm_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_BROADCOMM_PCM),
MAGENTA_DRIVER_END(bcm_pcm)
