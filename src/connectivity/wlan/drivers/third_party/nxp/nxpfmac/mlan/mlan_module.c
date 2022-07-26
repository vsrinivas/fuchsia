/** @file mlan_module.c
 *
 *  @brief This file declares the exported symbols from MLAN.
 *
 *
 *  Copyright 2008-2020 NXP
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

/******************************************************
Change log:
    12/08/2008: initial version
******************************************************/

#ifdef LINUX
#include <linux/module.h>
#include "mlan_decl.h"
#include "mlan_ioctl.h"

EXPORT_SYMBOL(mlan_register);
EXPORT_SYMBOL(mlan_unregister);
EXPORT_SYMBOL(mlan_init_fw);
EXPORT_SYMBOL(mlan_set_init_param);
EXPORT_SYMBOL(mlan_dnld_fw);
EXPORT_SYMBOL(mlan_shutdown_fw);
#ifdef USB
EXPORT_SYMBOL(mlan_write_data_async_complete);
EXPORT_SYMBOL(mlan_recv);
#endif
EXPORT_SYMBOL(mlan_send_packet);
EXPORT_SYMBOL(mlan_ioctl);
EXPORT_SYMBOL(mlan_main_process);
EXPORT_SYMBOL(mlan_rx_process);
EXPORT_SYMBOL(mlan_select_wmm_queue);
EXPORT_SYMBOL(mlan_process_deaggr_pkt);
#if defined(SDIO) || defined(PCIE)
EXPORT_SYMBOL(mlan_interrupt);
#if defined(SYSKT)
EXPORT_SYMBOL(mlan_hs_callback);
#endif /* SYSKT_MULTI || SYSKT */
#endif /* SDIO || PCIE */

EXPORT_SYMBOL(mlan_pm_wakeup_card);
EXPORT_SYMBOL(mlan_is_main_process_running);
#ifdef PCIE
EXPORT_SYMBOL(mlan_set_int_mode);
#endif
EXPORT_SYMBOL(mlan_disable_host_int);
EXPORT_SYMBOL(mlan_enable_host_int);

MODULE_DESCRIPTION("M-WLAN MLAN Driver");
MODULE_AUTHOR("NXP");
MODULE_VERSION(MLAN_RELEASE_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
#endif /* LINUX */
