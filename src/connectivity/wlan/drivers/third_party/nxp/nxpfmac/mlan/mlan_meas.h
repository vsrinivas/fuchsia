/**
 *  @file mlan_meas.h
 *
 *  @brief Interface for the measurement module implemented in mlan_meas.c
 *
 *  Driver interface functions and type declarations for the measurement module
 *    implemented in mlan_meas.c
 *
 *  @sa mlan_meas.c
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

/*************************************************************
Change Log:
    03/25/2009: initial version
************************************************************/

#ifndef _MLAN_MEAS_H_
#define _MLAN_MEAS_H_

#include "mlan_fw.h"

/* Send a given measurement request to the firmware, report back the result */
extern int

wlan_meas_util_send_req(pmlan_private pmpriv,
			pHostCmd_DS_MEASUREMENT_REQUEST pmeas_req,
			t_u32 wait_for_resp_timeout,
			pmlan_ioctl_req pioctl_req,
			pHostCmd_DS_MEASUREMENT_REPORT pmeas_rpt);

/* Setup a measurement command before it is sent to the firmware */
extern int wlan_meas_cmd_process(mlan_private *pmpriv,
				 HostCmd_DS_COMMAND *pcmd_ptr,
				 const t_void *pinfo_buf);

/* Handle a given measurement command response from the firmware */
extern int wlan_meas_cmdresp_process(mlan_private *pmpriv,
				     const HostCmd_DS_COMMAND *resp);

#endif /* _MLAN_MEAS_H_ */
