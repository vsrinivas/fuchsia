/** @file mlan_uap.h
 *
 *  @brief This file contains related macros, enum, and struct
 *  of uap functionalities
 *
 *
 *  Copyright 2009-2020 NXP
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

/********************************************************
Change log:
    02/05/2009: initial version
********************************************************/

#ifndef _MLAN_UAP_H_
#define _MLAN_UAP_H_

mlan_status wlan_uap_get_channel(pmlan_private pmpriv);

mlan_status wlan_uap_set_channel(pmlan_private pmpriv,
				 Band_Config_t uap_band_cfg, t_u8 channel);

mlan_status wlan_uap_get_beacon_dtim(pmlan_private pmpriv);

mlan_status wlan_ops_uap_ioctl(t_void *adapter, pmlan_ioctl_req pioctl_req);

mlan_status wlan_ops_uap_prepare_cmd(t_void *priv, t_u16 cmd_no,
				     t_u16 cmd_action, t_u32 cmd_oid,
				     t_void *pioctl_buf,
				     t_void *pdata_buf, t_void *pcmd_buf);

mlan_status wlan_ops_uap_process_cmdresp(t_void *priv, t_u16 cmdresp_no,
					 t_void *pcmd_buf, t_void *pioctl);

mlan_status wlan_ops_uap_process_rx_packet(t_void *adapter, pmlan_buffer pmbuf);

mlan_status wlan_ops_uap_process_event(t_void *priv);

t_void *wlan_ops_uap_process_txpd(t_void *priv, pmlan_buffer pmbuf);

mlan_status wlan_ops_uap_init_cmd(t_void *priv, t_u8 first_bss);

#endif /* _MLAN_UAP_H_ */
