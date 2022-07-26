/** @file mlan_sta_rx.c
 *
 *  @brief This file contains the handling of RX in MLAN
 *  module.
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

/********************************************************
Change log:
    10/27/2008: initial version
********************************************************/

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_11n_aggr.h"
#include "mlan_11n_rxreorder.h"
#ifdef DRV_EMBEDDED_SUPPLICANT
#include "authenticator_api.h"
#endif

/********************************************************
		Local Variables
********************************************************/

/** Ethernet II header */
typedef struct {
	/** Ethernet II header destination address */
	t_u8 dest_addr[MLAN_MAC_ADDR_LENGTH];
	/** Ethernet II header source address */
	t_u8 src_addr[MLAN_MAC_ADDR_LENGTH];
	/** Ethernet II header length */
	t_u16 ethertype;

} EthII_Hdr_t;

/** IPv4 ARP request header */
typedef MLAN_PACK_START struct {
	/** Hardware type */
	t_u16 Htype;
	/** Protocol type */
	t_u16 Ptype;
	/** Hardware address length */
	t_u8 addr_len;
	/** Protocol address length */
	t_u8 proto_len;
	/** Operation code */
	t_u16 op_code;
	/** Source mac address */
	t_u8 src_mac[MLAN_MAC_ADDR_LENGTH];
	/** Sender IP address */
	t_u8 src_ip[4];
	/** Destination mac address */
	t_u8 dst_mac[MLAN_MAC_ADDR_LENGTH];
	/** Destination IP address */
	t_u8 dst_ip[4];
} MLAN_PACK_END IPv4_ARP_t;

/** IPv6 Nadv packet header */
typedef MLAN_PACK_START struct {
	/** IP protocol version */
	t_u8 version;
	/** flow label */
	t_u8 flow_lab[3];
	/** Payload length */
	t_u16 payload_len;
	/** Next header type */
	t_u8 next_hdr;
	/** Hot limit */
	t_u8 hop_limit;
	/** Source address */
	t_u8 src_addr[16];
	/** Destination address */
	t_u8 dst_addr[16];
	/** ICMP type */
	t_u8 icmp_type;
	/** IPv6 Code */
	t_u8 ipv6_code;
	/** IPv6 Checksum */
	t_u16 ipv6_checksum;
	/** Flags */
	t_u32 flags;
	/** Target address */
	t_u8 taget_addr[16];
	/** Reserved */
	t_u8 rev[8];
} MLAN_PACK_END IPv6_Nadv_t;

/********************************************************
		Global functions
********************************************************/
/**
 *  @brief This function check and discard IPv4 and IPv6 gratuitous broadcast
 * packets
 *
 *  @param prx_pkt     A pointer to RxPacketHdr_t structure of received packet
 *  @param pmadapter   A pointer to pmlan_adapter structure
 *  @return            TRUE if found such type of packets, FALSE not found
 */
static t_u8
discard_gratuitous_ARP_msg(RxPacketHdr_t *prx_pkt, pmlan_adapter pmadapter)
{
	t_u8 proto_ARP_type[] = { 0x08, 0x06 };
	t_u8 proto_ARP_type_v6[] = { 0x86, 0xDD };
	IPv4_ARP_t *parp_hdr;
	IPv6_Nadv_t *pNadv_hdr;
	t_u8 ret = MFALSE;

	/* IPV4 pkt check
	 * A gratuitous ARP is an ARP packet
	 * where the source and destination IP are both set to
	 * the IP of the machine issuing the packet.
	 */
	if (memcmp(pmadapter, proto_ARP_type, &prx_pkt->eth803_hdr.h803_len,
		   sizeof(proto_ARP_type)) == 0) {
		parp_hdr = (IPv4_ARP_t *)(&prx_pkt->rfc1042_hdr);
		/* Graguitous ARP can be ARP request or ARP reply */
		if ((parp_hdr->op_code == mlan_htons(0x01)) ||
		    (parp_hdr->op_code == mlan_htons(0x02)))
			if (memcmp(pmadapter, parp_hdr->src_ip,
				   parp_hdr->dst_ip, 4) == 0)
				ret = MTRUE;
	}

	/* IPV6 pkt check
	 * An unsolicited Neighbor Advertisement pkt is
	 * marked by a cleared Solicited Flag
	 */
	if (memcmp(pmadapter, proto_ARP_type_v6, &prx_pkt->eth803_hdr.h803_len,
		   sizeof(proto_ARP_type_v6)) == 0) {
		pNadv_hdr = (IPv6_Nadv_t *)(&prx_pkt->rfc1042_hdr);
		/* Check Nadv type: next header is ICMPv6 and
		 * icmp type is Nadv */
		if (pNadv_hdr->next_hdr == 0x3A && pNadv_hdr->icmp_type == 0x88)
			if ((pNadv_hdr->flags & mlan_htonl(0x40000000)) == 0)
				ret = MTRUE;
	}

	return ret;
}

/**
 *  @brief This function process tdls action frame
 *
 *  @param priv        A pointer to mlan_private structure
 *  @param pbuf        A pointer to tdls action frame buffer
 *  @param len         len of tdls action frame buffer
 *  @return            N/A
 */
void
wlan_process_tdls_action_frame(pmlan_private priv, t_u8 *pbuf, t_u32 len)
{
	sta_node *sta_ptr = MNULL;
	IEEEtypes_VendorHeader_t *pvendor_ie = MNULL;
	const t_u8 wmm_oui[] = { 0x00, 0x50, 0xf2, 0x02 };
	t_u8 *peer;
	t_u8 *pos, *end;
	t_u8 action;
	int ie_len = 0;
	t_u8 i;
	int rate_len;

#define TDLS_PAYLOAD_TYPE 2
#define TDLS_CATEGORY 0x0c
#define TDLS_REQ_FIX_LEN 6
#define TDLS_RESP_FIX_LEN 8
#define TDLS_CONFIRM_FIX_LEN 6
	if (len < (sizeof(EthII_Hdr_t) + 3))
		return;
	if (*(t_u8 *)(pbuf + sizeof(EthII_Hdr_t)) != TDLS_PAYLOAD_TYPE)
		/*TDLS payload type = 2 */
		return;
	if (*(t_u8 *)(pbuf + sizeof(EthII_Hdr_t) + 1) != TDLS_CATEGORY)
		/*TDLS category = 0xc */
		return;
	peer = pbuf + MLAN_MAC_ADDR_LENGTH;

	action = *(t_u8 *)(pbuf + sizeof(EthII_Hdr_t) + 2);
	/*2= payload type + category */

	if (action > TDLS_SETUP_CONFIRM) {
		/*just handle TDLS setup request/response/confirm */
		PRINTM(MMSG, "Recv TDLS Action: peer=" MACSTR ", action=%d\n",
		       MAC2STR(peer), action);
		return;
	}

	sta_ptr = wlan_add_station_entry(priv, peer);
	if (!sta_ptr)
		return;
	if (action == TDLS_SETUP_REQUEST) {	/*setup request */
		sta_ptr->status = TDLS_NOT_SETUP;
		PRINTM(MMSG, "Recv TDLS SETUP Request: peer=" MACSTR "\n",
		       MAC2STR(peer));
		wlan_hold_tdls_packets(priv, peer);
		if (len < (sizeof(EthII_Hdr_t) + TDLS_REQ_FIX_LEN))
			return;
		pos = pbuf + sizeof(EthII_Hdr_t) + 4;
		/*payload 1+ category 1 + action 1 +dialog 1 */
		sta_ptr->capability = mlan_ntohs(*(t_u16 *)pos);
		ie_len = len - sizeof(EthII_Hdr_t) - TDLS_REQ_FIX_LEN;
		pos += 2;
	} else if (action == 1) {	/*setup respons */
		PRINTM(MMSG, "Recv TDLS SETUP Response: peer=" MACSTR "\n",
		       MAC2STR(peer));
		if (len < (sizeof(EthII_Hdr_t) + TDLS_RESP_FIX_LEN))
			return;
		pos = pbuf + sizeof(EthII_Hdr_t) + 6;
		/*payload 1+ category 1 + action 1 +dialog 1 +status 2 */
		sta_ptr->capability = mlan_ntohs(*(t_u16 *)pos);
		ie_len = len - sizeof(EthII_Hdr_t) - TDLS_RESP_FIX_LEN;
		pos += 2;
	} else {		/*setup confirm */
		PRINTM(MMSG, "Recv TDLS SETUP Confirm: peer=" MACSTR "\n",
		       MAC2STR(peer));
		if (len < (sizeof(EthII_Hdr_t) + TDLS_CONFIRM_FIX_LEN))
			return;
		pos = pbuf + sizeof(EthII_Hdr_t) + TDLS_CONFIRM_FIX_LEN;
		/*payload 1+ category 1 + action 1 +dialog 1 + status 2 */
		ie_len = len - sizeof(EthII_Hdr_t) - TDLS_CONFIRM_FIX_LEN;
	}
	for (end = pos + ie_len; pos + 1 < end; pos += 2 + pos[1]) {
		if (pos + 2 + pos[1] > end)
			break;
		switch (*pos) {
		case SUPPORTED_RATES:
			sta_ptr->rate_len =
				MIN(pos[1], sizeof(sta_ptr->support_rate));
			for (i = 0; i < sta_ptr->rate_len; i++)
				sta_ptr->support_rate[i] = pos[2 + i];
			break;
		case EXTENDED_SUPPORTED_RATES:
			rate_len = MIN(pos[1], sizeof(sta_ptr->support_rate) -
				       sta_ptr->rate_len);
			for (i = 0; i < rate_len; i++)
				sta_ptr->support_rate[sta_ptr->rate_len + i] =
					pos[2 + i];
			sta_ptr->rate_len += rate_len;
			break;
		case HT_CAPABILITY:
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->HTcap, pos,
				   sizeof(IEEEtypes_HTCap_t),
				   sizeof(IEEEtypes_HTCap_t));
			sta_ptr->is_11n_enabled = 1;
			DBG_HEXDUMP(MDAT_D, "TDLS HT capability",
				    (t_u8 *)(&sta_ptr->HTcap),
				    MIN(sizeof(IEEEtypes_HTCap_t),
					MAX_DATA_DUMP_LEN));
			break;
		case HT_OPERATION:
			memcpy_ext(priv->adapter, &sta_ptr->HTInfo, pos,
				   sizeof(IEEEtypes_HTInfo_t),
				   sizeof(IEEEtypes_HTInfo_t));
			DBG_HEXDUMP(MDAT_D, "TDLS HT info",
				    (t_u8 *)(&sta_ptr->HTInfo),
				    MIN(sizeof(IEEEtypes_HTInfo_t),
					MAX_DATA_DUMP_LEN));
			break;
		case BSSCO_2040:
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->BSSCO_20_40,
				   pos, sizeof(IEEEtypes_2040BSSCo_t),
				   sizeof(IEEEtypes_2040BSSCo_t));
			break;
		case EXT_CAPABILITY:
			sta_ptr->ExtCap.ieee_hdr.len =
				MIN(pos[1], sizeof(ExtCap_t));
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->ExtCap, pos,
				   sta_ptr->ExtCap.ieee_hdr.len +
				   sizeof(IEEEtypes_Header_t),
				   sizeof(IEEEtypes_ExtCap_t));
			DBG_HEXDUMP(MDAT_D, "TDLS Extended capability",
				    (t_u8 *)(&sta_ptr->ExtCap),
				    sta_ptr->ExtCap.ieee_hdr.len + 2);
			break;
		case RSN_IE:
			sta_ptr->rsn_ie.ieee_hdr.len =
				MIN(pos[1], IEEE_MAX_IE_SIZE -
				    sizeof(IEEEtypes_Header_t));
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->rsn_ie, pos,
				   sta_ptr->rsn_ie.ieee_hdr.len +
				   sizeof(IEEEtypes_Header_t),
				   sizeof(IEEEtypes_Generic_t));
			DBG_HEXDUMP(MDAT_D, "TDLS Rsn ie ",
				    (t_u8 *)(&sta_ptr->rsn_ie),
				    sta_ptr->rsn_ie.ieee_hdr.len +
				    sizeof(IEEEtypes_Header_t));
			break;
		case QOS_INFO:
			sta_ptr->qos_info = pos[2];
			PRINTM(MDAT_D, "TDLS qos info %x\n", sta_ptr->qos_info);
			break;
		case VENDOR_SPECIFIC_221:
			pvendor_ie = (IEEEtypes_VendorHeader_t *)pos;
			if (!memcmp(priv->adapter, pvendor_ie->oui, wmm_oui,
				    sizeof(wmm_oui))) {
				sta_ptr->qos_info = pos[8]; /** qos info in wmm
							       parameters in
							       response and
							       confirm */
				PRINTM(MDAT_D, "TDLS qos info %x\n",
				       sta_ptr->qos_info);
			}
			break;
		case LINK_ID:
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->link_ie,
				   pos, sizeof(IEEEtypes_LinkIDElement_t),
				   sizeof(IEEEtypes_LinkIDElement_t));
			break;

		case VHT_CAPABILITY:
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->vht_cap,
				   pos, sizeof(IEEEtypes_VHTCap_t),
				   sizeof(IEEEtypes_VHTCap_t));
			sta_ptr->is_11ac_enabled = 1;
			DBG_HEXDUMP(MDAT_D, "TDLS VHT capability",
				    (t_u8 *)(&sta_ptr->vht_cap),
				    MIN(sizeof(IEEEtypes_VHTCap_t),
					MAX_DATA_DUMP_LEN));
			break;
		case VHT_OPERATION:
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->vht_oprat,
				   pos, sizeof(IEEEtypes_VHTOprat_t),
				   sizeof(IEEEtypes_VHTOprat_t));
			DBG_HEXDUMP(MDAT_D, "TDLS VHT Operation",
				    (t_u8 *)(&sta_ptr->vht_oprat),
				    MIN(sizeof(IEEEtypes_VHTOprat_t),
					MAX_DATA_DUMP_LEN));
			break;
		case AID_INFO:
			memcpy_ext(priv->adapter, (t_u8 *)&sta_ptr->aid_info,
				   pos, sizeof(IEEEtypes_AID_t),
				   sizeof(IEEEtypes_AID_t));
			DBG_HEXDUMP(MDAT_D, "TDLS AID Info",
				    (t_u8 *)(&sta_ptr->aid_info),
				    MIN(sizeof(IEEEtypes_AID_t),
					MAX_DATA_DUMP_LEN));
			break;
		default:
			break;
		}
	}
	return;
}

/**
 *  @brief This function processes received packet and forwards it
 *          to kernel/upper layer
 *
 *  @param pmadapter A pointer to mlan_adapter
 *  @param pmbuf   A pointer to mlan_buffer which includes the received packet
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_rx_packet(pmlan_adapter pmadapter, pmlan_buffer pmbuf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	RxPacketHdr_t *prx_pkt;
	RxPD *prx_pd;
	int hdr_chop;
	EthII_Hdr_t *peth_hdr;
	t_u8 rfc1042_eth_hdr[MLAN_MAC_ADDR_LENGTH] = { 0xaa, 0xaa, 0x03,
		0x00, 0x00, 0x00
	};
	t_u8 snap_oui_802_h[MLAN_MAC_ADDR_LENGTH] = { 0xaa, 0xaa, 0x03,
		0x00, 0x00, 0xf8
	};
	t_u8 appletalk_aarp_type[2] = { 0x80, 0xf3 };
	t_u8 ipx_snap_type[2] = { 0x81, 0x37 };
	t_u8 tdls_action_type[2] = { 0x89, 0x0d };
#ifdef DRV_EMBEDDED_SUPPLICANT
	t_u8 eapol_type[2] = { 0x88, 0x8e };
#endif
	t_u8 ext_rate_info = 0;

	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	prx_pkt = (RxPacketHdr_t *)((t_u8 *)prx_pd + prx_pd->rx_pkt_offset);

/** Small debug type */
#define DBG_TYPE_SMALL 2
/** Size of debugging structure */
#define SIZE_OF_DBG_STRUCT 4
	if (prx_pd->rx_pkt_type == PKT_TYPE_DEBUG) {
		t_u8 dbg_type;
		dbg_type = *(t_u8 *)&prx_pkt->eth803_hdr;
		if (dbg_type == DBG_TYPE_SMALL) {
			PRINTM(MFW_D, "\n");
			DBG_HEXDUMP(MFW_D, "FWDBG",
				    (char *)((t_u8 *)&prx_pkt->eth803_hdr +
					     SIZE_OF_DBG_STRUCT),
				    prx_pd->rx_pkt_length);
			PRINTM(MFW_D, "FWDBG::\n");
		}
		goto done;
	}

	PRINTM(MINFO,
	       "RX Data: data_len - prx_pd->rx_pkt_offset = %d - %d = %d\n",
	       pmbuf->data_len, prx_pd->rx_pkt_offset,
	       pmbuf->data_len - prx_pd->rx_pkt_offset);

	HEXDUMP("RX Data: Dest", prx_pkt->eth803_hdr.dest_addr,
		sizeof(prx_pkt->eth803_hdr.dest_addr));
	HEXDUMP("RX Data: Src", prx_pkt->eth803_hdr.src_addr,
		sizeof(prx_pkt->eth803_hdr.src_addr));

	if ((memcmp(pmadapter, &prx_pkt->rfc1042_hdr, snap_oui_802_h,
		    sizeof(snap_oui_802_h)) == 0) ||
	    ((memcmp(pmadapter, &prx_pkt->rfc1042_hdr, rfc1042_eth_hdr,
		     sizeof(rfc1042_eth_hdr)) == 0) &&
	     memcmp(pmadapter, &prx_pkt->rfc1042_hdr.snap_type,
		    appletalk_aarp_type, sizeof(appletalk_aarp_type)) &&
	     memcmp(pmadapter, &prx_pkt->rfc1042_hdr.snap_type, ipx_snap_type,
		    sizeof(ipx_snap_type)))) {
		/*
		 * Replace the 803 header and rfc1042 header (llc/snap) with an
		 * EthernetII header, keep the src/dst and snap_type
		 * (ethertype). The firmware only passes up SNAP frames
		 * converting all RX Data from 802.11 to 802.2/LLC/SNAP frames.
		 * To create the Ethernet II, just move the src, dst address
		 * right before the snap_type.
		 */
		peth_hdr =
			(EthII_Hdr_t *)((t_u8 *)&prx_pkt->eth803_hdr +
					sizeof(prx_pkt->eth803_hdr) +
					sizeof(prx_pkt->rfc1042_hdr) -
					sizeof(prx_pkt->eth803_hdr.dest_addr) -
					sizeof(prx_pkt->eth803_hdr.src_addr) -
					sizeof(prx_pkt->rfc1042_hdr.snap_type));

		memcpy_ext(pmadapter, peth_hdr->src_addr,
			   prx_pkt->eth803_hdr.src_addr,
			   sizeof(peth_hdr->src_addr),
			   sizeof(peth_hdr->src_addr));
		memcpy_ext(pmadapter, peth_hdr->dest_addr,
			   prx_pkt->eth803_hdr.dest_addr,
			   sizeof(peth_hdr->dest_addr),
			   sizeof(peth_hdr->dest_addr));

		/* Chop off the RxPD + the excess memory from the 802.2/llc/snap
		 *  header that was removed.
		 */
		hdr_chop = (t_u32)((t_ptr)peth_hdr - (t_ptr)prx_pd);
	} else {
		HEXDUMP("RX Data: LLC/SNAP", (t_u8 *)&prx_pkt->rfc1042_hdr,
			sizeof(prx_pkt->rfc1042_hdr));
		if ((priv->hotspot_cfg & HOTSPOT_ENABLED) &&
		    discard_gratuitous_ARP_msg(prx_pkt, pmadapter)) {
			ret = MLAN_STATUS_SUCCESS;
			PRINTM(MDATA,
			       "Bypass sending Gratuitous ARP frame to Kernel.\n");
			goto done;
		}
		if (!memcmp(pmadapter, &prx_pkt->eth803_hdr.h803_len,
			    tdls_action_type, sizeof(tdls_action_type))) {
			wlan_process_tdls_action_frame(priv,
						       ((t_u8 *)prx_pd +
							prx_pd->rx_pkt_offset),
						       prx_pd->rx_pkt_length);
		}
		/* Chop off the RxPD */
		hdr_chop = (t_u32)((t_ptr)&prx_pkt->eth803_hdr - (t_ptr)prx_pd);
	}

	/* Chop off the leading header bytes so the it points to the start of
	 *   either the reconstructed EthII frame or the 802.2/llc/snap frame
	 */
	pmbuf->data_len -= hdr_chop;
	pmbuf->data_offset += hdr_chop;
	pmbuf->pparent = MNULL;
	DBG_HEXDUMP(MDAT_D, "RxPD", (t_u8 *)prx_pd,
		    MIN(sizeof(RxPD), MAX_DATA_DUMP_LEN));
	DBG_HEXDUMP(MDAT_D, "Rx Payload",
		    ((t_u8 *)prx_pd + prx_pd->rx_pkt_offset),
		    MIN(prx_pd->rx_pkt_length, MAX_DATA_DUMP_LEN));

	priv->rxpd_rate = prx_pd->rx_rate;
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmbuf->out_ts_sec,
						  &pmbuf->out_ts_usec);
	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA, "%lu.%06lu : Data => kernel seq_num=%d tid=%d\n",
	       pmbuf->out_ts_sec, pmbuf->out_ts_usec, prx_pd->seq_num,
	       prx_pd->priority);

#ifdef DRV_EMBEDDED_SUPPLICANT
	if (supplicantIsEnabled(priv->psapriv) &&
	    (!memcmp(pmadapter, &prx_pkt->eth803_hdr.h803_len, eapol_type,
		     sizeof(eapol_type)))) {
		// BML_SET_OFFSET(bufDesc, offset);
		if (ProcessEAPoLPkt(priv->psapriv, pmbuf)) {
			pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
			ret = MLAN_STATUS_SUCCESS;
			PRINTM(MMSG,
			       "host supplicant eapol pkt process done.\n");

			LEAVE();
			return ret;
		}
	}
#endif

	if (MFALSE || priv->rx_pkt_info) {

		ext_rate_info = (t_u8)(prx_pd->rx_info >> 16);
		pmbuf->u.rx_info.data_rate =
			wlan_index_to_data_rate(priv->adapter, prx_pd->rx_rate,
						prx_pd->rate_info,
						ext_rate_info);

		pmbuf->u.rx_info.channel =
			(prx_pd->rx_info & RXPD_CHAN_MASK) >> 5;
		pmbuf->u.rx_info.antenna = prx_pd->antenna;
		pmbuf->u.rx_info.rssi = prx_pd->snr - prx_pd->nf;
	}
	ret = pmadapter->callbacks.moal_recv_packet(pmadapter->pmoal_handle,
						    pmbuf);
	if (ret == MLAN_STATUS_FAILURE) {
		pmbuf->status_code = MLAN_ERROR_PKT_INVALID;
		PRINTM(MERROR,
		       "STA Rx Error: moal_recv_packet returned error\n");
	}
done:
	if (ret != MLAN_STATUS_PENDING)
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
#ifdef USB
	else if (IS_USB(pmadapter->card_type))
		pmadapter->callbacks.moal_recv_complete(pmadapter->pmoal_handle,
							MNULL, MLAN_USB_EP_DATA,
							MLAN_STATUS_SUCCESS);
#endif
	LEAVE();

	return ret;
}

/**
 *   @brief This function processes the received buffer
 *
 *   @param adapter A pointer to mlan_adapter
 *   @param pmbuf     A pointer to the received buffer
 *
 *   @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_sta_process_rx_packet(t_void *adapter, pmlan_buffer pmbuf)
{
	pmlan_adapter pmadapter = (pmlan_adapter)adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	RxPD *prx_pd;
	RxPacketHdr_t *prx_pkt;
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	t_u8 ta[MLAN_MAC_ADDR_LENGTH];
	t_u16 rx_pkt_type = 0;
	wlan_mgmt_pkt *pmgmt_pkt_hdr = MNULL;

	sta_node *sta_ptr = MNULL;
	t_u16 adj_rx_rate = 0;
	t_u8 antenna = 0;
	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	/* Endian conversion */
	endian_convert_RxPD(prx_pd);
	if (priv->adapter->pcard_info->v14_fw_api) {
		t_u8 rxpd_rate_info_orig = prx_pd->rate_info;
		prx_pd->rate_info =
			wlan_convert_v14_rx_rate_info(priv,
						      rxpd_rate_info_orig);
		PRINTM(MINFO,
		       "STA RX: v14_fw_api=%d rx_rate =%d rxpd_rate_info=0x%x->0x%x\n",
		       priv->adapter->pcard_info->v14_fw_api, prx_pd->rx_rate,
		       rxpd_rate_info_orig, prx_pd->rate_info);
	}
	rx_pkt_type = prx_pd->rx_pkt_type;
	prx_pkt = (RxPacketHdr_t *)((t_u8 *)prx_pd + prx_pd->rx_pkt_offset);

	if ((prx_pd->rx_pkt_offset + prx_pd->rx_pkt_length) !=
	    (t_u16)pmbuf->data_len) {
		PRINTM(MERROR,
		       "Wrong rx packet: len=%d,rx_pkt_offset=%d,"
		       " rx_pkt_length=%d\n",
		       pmbuf->data_len, prx_pd->rx_pkt_offset,
		       prx_pd->rx_pkt_length);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		ret = MLAN_STATUS_FAILURE;
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
		goto done;
	}
	pmbuf->data_len = prx_pd->rx_pkt_offset + prx_pd->rx_pkt_length;

	if (pmadapter->priv[pmbuf->bss_index]->mgmt_frame_passthru_mask &&
	    prx_pd->rx_pkt_type == PKT_TYPE_MGMT_FRAME) {
		/* Check if this is mgmt packet and needs to
		 * forwarded to app as an event
		 */
		pmgmt_pkt_hdr = (wlan_mgmt_pkt *)((t_u8 *)prx_pd +
						  prx_pd->rx_pkt_offset);
		pmgmt_pkt_hdr->frm_len =
			wlan_le16_to_cpu(pmgmt_pkt_hdr->frm_len);

		if ((pmgmt_pkt_hdr->wlan_header.frm_ctl &
		     IEEE80211_FC_MGMT_FRAME_TYPE_MASK) == 0)
			wlan_process_802dot11_mgmt_pkt(pmadapter->
						       priv[pmbuf->bss_index],
						       (t_u8 *)&pmgmt_pkt_hdr->
						       wlan_header,
						       pmgmt_pkt_hdr->frm_len +
						       sizeof(wlan_mgmt_pkt) -
						       sizeof(pmgmt_pkt_hdr->
							      frm_len), prx_pd);
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
		goto done;
	}
	if (rx_pkt_type != PKT_TYPE_BAR) {
		priv->rxpd_rate_info = prx_pd->rate_info;
		priv->rxpd_rate = prx_pd->rx_rate;
		priv->rxpd_rx_info = (t_u8)(prx_pd->rx_info >> 16);
		if (priv->bss_type == MLAN_BSS_TYPE_STA) {
			antenna = wlan_adjust_antenna(priv, prx_pd);
			adj_rx_rate =
				wlan_adjust_data_rate(priv, priv->rxpd_rate,
						      priv->rxpd_rate_info);
			pmadapter->callbacks.moal_hist_data_add(pmadapter->
								pmoal_handle,
								pmbuf->
								bss_index,
								adj_rx_rate,
								prx_pd->snr,
								prx_pd->nf,
								antenna);
		}
	}

	/*
	 * If the packet is not an unicast packet then send the packet
	 * directly to os. Don't pass thru rx reordering
	 */
	if ((!IS_11N_ENABLED(priv)
	     && !(prx_pd->flags & RXPD_FLAG_PKT_DIRECT_LINK)
	    ) ||
	    memcmp(priv->adapter, priv->curr_addr,
		   prx_pkt->eth803_hdr.dest_addr, MLAN_MAC_ADDR_LENGTH)) {
		priv->snr = prx_pd->snr;
		priv->nf = prx_pd->nf;
		wlan_process_rx_packet(pmadapter, pmbuf);
		goto done;
	}

	if (queuing_ra_based(priv) ||
	    (prx_pd->flags & RXPD_FLAG_PKT_DIRECT_LINK)) {
		memcpy_ext(pmadapter, ta, prx_pkt->eth803_hdr.src_addr,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
		if (prx_pd->priority < MAX_NUM_TID) {
			PRINTM(MDATA, "adhoc/tdls packet %p " MACSTR "\n",
			       pmbuf, MAC2STR(ta));
			sta_ptr = wlan_get_station_entry(priv, ta);
			if (sta_ptr) {
				sta_ptr->rx_seq[prx_pd->priority] =
					prx_pd->seq_num;
				sta_ptr->snr = prx_pd->snr;
				sta_ptr->nf = prx_pd->nf;
				if (prx_pd->flags & RXPD_FLAG_PKT_DIRECT_LINK) {
					pmadapter->callbacks.
						moal_updata_peer_signal
						(pmadapter->pmoal_handle,
						 pmbuf->bss_index, ta,
						 prx_pd->snr, prx_pd->nf);
				}
			}
			if (!sta_ptr || !sta_ptr->is_11n_enabled) {
				wlan_process_rx_packet(pmadapter, pmbuf);
				goto done;
			}
		}
	} else {
		priv->snr = prx_pd->snr;
		priv->nf = prx_pd->nf;
		if ((rx_pkt_type != PKT_TYPE_BAR) &&
		    (prx_pd->priority < MAX_NUM_TID))
			priv->rx_seq[prx_pd->priority] = prx_pd->seq_num;
		memcpy_ext(pmadapter, ta,
			   priv->curr_bss_params.bss_descriptor.mac_address,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
	}
	if ((priv->port_ctrl_mode == MTRUE && priv->port_open == MFALSE) &&
	    (rx_pkt_type != PKT_TYPE_BAR)) {
		mlan_11n_rxreorder_pkt(priv, prx_pd->seq_num, prx_pd->priority,
				       ta, (t_u8)prx_pd->rx_pkt_type,
				       (t_void *)RX_PKT_DROPPED_IN_FW);
		if (rx_pkt_type == PKT_TYPE_AMSDU) {
			pmbuf->data_len = prx_pd->rx_pkt_length;
			pmbuf->data_offset += prx_pd->rx_pkt_offset;
			wlan_11n_deaggregate_pkt(priv, pmbuf);
		} else {
			wlan_process_rx_packet(pmadapter, pmbuf);
		}
		goto done;
	}
	/* Reorder and send to OS */
	ret = mlan_11n_rxreorder_pkt(priv, prx_pd->seq_num, prx_pd->priority,
				     ta, (t_u8)prx_pd->rx_pkt_type,
				     (void *)pmbuf);
	if (ret || (rx_pkt_type == PKT_TYPE_BAR))
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);

done:

	LEAVE();
	return ret;
}
