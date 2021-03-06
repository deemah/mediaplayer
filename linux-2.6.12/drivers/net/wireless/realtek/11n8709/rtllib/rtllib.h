/*
 * Merged with mainline rtllib.h in Aug 2004.  Original ieee802_11
 * remains copyright by the original authors
 *
 * Portions of the merged code are based on Host AP (software wireless
 * LAN access point) driver for Intersil Prism2/2.5/3.
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * Adaption to a generic IEEE 802.11 stack by James Ketrenos
 * <jketreno@linux.intel.com>
 * Copyright (c) 2004, Intel Corporation
 *
 * Modified for Realtek's wi-fi cards by Andrea Merello
 * <andreamrl@tiscali.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */
#ifndef RTLLIB_H
#define RTLLIB_H
#include <linux/if_ether.h> /* ETH_ALEN */
#include <linux/kernel.h>   /* ARRAY_SIZE */
#include <linux/version.h>
#include <linux/module.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
#include <linux/jiffies.h>
#else
#include <linux/jffs.h>
#include <linux/tqueue.h>
#endif
#include <linux/timer.h>
#include <linux/sched.h>

#include <linux/delay.h>
#include <linux/wireless.h>

#include "rtl819x_HT.h"
#include "rtl819x_BA.h"
#include "rtl819x_TS.h"

#include <linux/netdevice.h>
#include <linux/if_arp.h> /* ARPHRD_ETHER */

#ifndef WIRELESS_SPY
#define WIRELESS_SPY		// enable iwspy support
#endif
#include <net/iw_handler.h>	// new driver API

#ifndef RTK_DMP_PLATFORM
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
#ifndef bool
typedef enum{false = 0, true} bool;
#endif
#endif
#endif

#ifndef IW_MODE_MONITOR
#define IW_MODE_MONITOR 6
#endif

#ifndef IWEVCUSTOM
#define IWEVCUSTOM 0x8c02
#endif

#ifndef IW_CUSTOM_MAX
/* Max number of char in custom event - use multiple of them if needed */
#define IW_CUSTOM_MAX	256	/* In bytes */
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#define jiffies_to_msecs(t)  ((t) * 1000 / HZ)
#ifndef __bitwise
#define __bitwise __attribute__((bitwise))
#endif
typedef __u16  __le16;

#if (WIRELESS_EXT < 16)
struct iw_spy_data{
	/* --- Standard spy support --- */
	int 			spy_number;
	u_char 			spy_address[IW_MAX_SPY][ETH_ALEN];
	struct iw_quality	spy_stat[IW_MAX_SPY];
	/* --- Enhanced spy support (event) */
	struct iw_quality	spy_thr_low; /* Low threshold */
	struct iw_quality	spy_thr_high; /* High threshold */
	u_char			spy_thr_under[IW_MAX_SPY];
}; 
#endif
#endif

#ifndef container_of
/**
 * container_of - cast a member of a structure out to the containing structure
 *
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

//YJ,add,090813
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
	#define EXPORT_SYMBOL_RSL(x) EXPORT_SYMBOL(x)
#else
	#define EXPORT_SYMBOL_RSL(x) EXPORT_SYMBOL_NOVERS(x)
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static inline void tq_init(struct tq_struct * task, void(*func)(void *), void *data)
{
	task->routine = func;
	task->data 	= data;
	//task->next = NULL;
	INIT_LIST_HEAD(&task->list);
	task->sync = 0;
}
static inline void setup_timer(struct timer_list * timer, void(*function)(unsigned long), unsigned long data)
{
	timer->function = function;
	timer->data 	= data;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	typedef struct delayed_work delayed_work_struct_rsl;
	#define queue_delayed_work_rsl(x,y,z) queue_delayed_work(x,y,z)
	#define INIT_DELAYED_WORK_RSL(x,y,z) INIT_DELAYED_WORK(x,y)
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,40)
	typedef struct tq_struct delayed_work_struct_rsl;
	#define queue_delayed_work_rsl(x,y,z) schedule_task(y)
	#define INIT_DELAYED_WORK_RSL(x,y,z) tq_init(x,y,z)
#else
	typedef struct work_struct delayed_work_struct_rsl;
	#define queue_delayed_work_rsl(x,y,z) queue_delayed_work(x,y,z)
	#define INIT_DELAYED_WORK_RSL(x,y,z) INIT_WORK(x,y,z)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	typedef struct work_struct work_struct_rsl;
	#define queue_work_rsl(x,y) queue_work(x,y)
	#define INIT_WORK_RSL(x,y,z) INIT_WORK(x,y)
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,40)
	typedef struct tq_struct work_struct_rsl;
	#define queue_work_rsl(x,y) schedule_task(y)
	#define INIT_WORK_RSL(x,y,z) tq_init(x,y,z)
#else
	typedef struct work_struct work_struct_rsl;
	#define queue_work_rsl(x,y) queue_work(x,y)
	#define INIT_WORK_RSL(x,y,z) INIT_WORK(x,y,z)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)	
	#define container_of_work_rsl(x,y,z) container_of(x,y,z)
	#define container_of_dwork_rsl(x,y,z) container_of(container_of(x, struct delayed_work, work), y, z) 
#else
	#define container_of_work_rsl(x,y,z) (x)
	#define container_of_dwork_rsl(x,y,z) (x) 
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,4,20)) && (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static inline char *
iwe_stream_add_event_rsl(struct iw_request_info *info,
			char *     stream,         /* Stream of events */
			char *     ends,           /* End of stream */
			struct iw_event *iwe,      /* Payload */
			int        event_len)      /* Real size of payload */
{
        /* Check if it's possible */
        if((stream + event_len) < ends) {
                iwe->len = event_len;
		ndelay(1);   //new
                memcpy(stream, (char *) iwe, event_len);
                stream += event_len;
        }
        return stream;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	#define iwe_stream_add_event_rsl(info,start,stop,iwe,len) iwe_stream_add_event(info,start,stop,iwe,len)
#else
	#define iwe_stream_add_event_rsl(info,start,stop,iwe,len) iwe_stream_add_event(start,stop,iwe,len)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	#define iwe_stream_add_point_rsl(info,start,stop,iwe,p) iwe_stream_add_point(info,start,stop,iwe,p)
#else
	#define iwe_stream_add_point_rsl(info,start,stop,iwe,p) iwe_stream_add_point(start,stop,iwe,p)
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0) 
	#define usb_alloc_urb_rsl(x,y) usb_alloc_urb(x,y)
	#define usb_submit_urb_rsl(x,y) usb_submit_urb(x,y)
#else
	#define usb_alloc_urb_rsl(x,y) usb_alloc_urb(x)
	#define usb_submit_urb_rsl(x,y) usb_submit_urb(x)
#endif

static inline void *netdev_priv_rsl(struct net_device *dev)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0) 
	return netdev_priv(dev);
#else
	return dev->priv;
#endif
}
//YJ, add,090813,end

#define KEY_TYPE_NA		0x0
#define KEY_TYPE_WEP40 		0x1
#define KEY_TYPE_TKIP		0x2
#define KEY_TYPE_CCMP		0x4
#define KEY_TYPE_WEP104		0x5

/* added for rtl819x tx procedure */
#define MAX_QUEUE_SIZE		0x10

#if defined(RTL8192SU) || defined(RTL8190P) ||defined(RTL8192U) ||defined(RTL8192E)
//
// 8192su queue mapping
//
#define BK_QUEUE                               0
#define BE_QUEUE                               1
#define VI_QUEUE                               2
#define VO_QUEUE                               3
#define HCCA_QUEUE                             4
#define TXCMD_QUEUE                            5
#define MGNT_QUEUE                             6
#define HIGH_QUEUE                             7
#define BEACON_QUEUE                           8
#elif defined(RTL8192SE)
//
// 8192se queue mapping
//
#define BK_QUEUE                               0
#define BE_QUEUE                               1
#define VI_QUEUE                               2
#define VO_QUEUE                               3
#define BEACON_QUEUE                          4
#define TXCMD_QUEUE                            5
#define MGNT_QUEUE                             6
#define HIGH_QUEUE                             7
#define HCCA_QUEUE                             8

#elif defined(RTL8192CE)

#define BK_QUEUE						0		
#define BE_QUEUE						1		
#define VI_QUEUE						2		
#define VO_QUEUE						3		
#define HCCA_QUEUE						4		
#define TXCMD_QUEUE						5		
#define MGNT_QUEUE						6	
#define HIGH_QUEUE						7	
#define BEACON_QUEUE					8
#endif

#ifdef RTL8192CE
#define LOW_QUEUE				BE_QUEUE
#define NORMAL_QUEUE				VO_QUEUE
#else
#define LOW_QUEUE                              BE_QUEUE
#define NORMAL_QUEUE                           MGNT_QUEUE
#endif

#ifdef _RTL8192_EXT_PATCH_
//
//For MESH
//
#ifndef IW_MODE_MESH 
#define IW_MODE_MESH			7	
#endif
#define WIFI_MESH_TYPE	RTLLIB_FTYPE_DATA

#define WLAN_MESH_HDR_6ADDR_LEN 18			//by amy 090720// 16
#define WLAN_MESH_HDR_4ADDR_LEN 6			//by amy 090720// 4
#define MAX_MP 16
#define IEEE_MESH_MAC_HASH_SIZE 31
#define MAX_HOST_NAME_LENGTH			32
#endif
#define AMSDU_SUBHEADER_LEN 14
#define SWRF_TIMEOUT				50

//added by amy for LEAP related
#define IE_CISCO_FLAG_POSITION		0x08	// Flag byte: byte 8, numbered from 0.
#define SUPPORT_CKIP_MIC			0x08	// bit3
#define SUPPORT_CKIP_PK			0x10	// bit4
//added by amy for ps
// RF Off Level for IPS or HW/SW radio off
#define	RT_RF_OFF_LEVL_ASPM			BIT0	// PCI ASPM
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT1	// PCI clock request
#define	RT_RF_OFF_LEVL_PCI_D3			BIT2	// PCI D3 mode
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT3	// NIC halt, re-initialize hw parameters
#define	RT_RF_OFF_LEVL_FREE_FW		BIT4	// FW free, re-download the FW
#define	RT_RF_OFF_LEVL_FW_32K		BIT5	// FW in 32k
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT6	// Always enable ASPM and Clock Req in initialization.
#define	RT_RF_LPS_DISALBE_2R			BIT30	// When LPS is on, disable 2R if no packet is received or transmittd.
#define	RT_RF_LPS_LEVEL_ASPM			BIT31	// LPS with ASPM
#define	RT_IN_PS_LEVEL(pPSC, _PS_FLAG)	((pPSC->CurPsLevel & _PS_FLAG) ? true : false)
#define	RT_CLEAR_PS_LEVEL(pPSC, _PS_FLAG)	(pPSC->CurPsLevel &= (~(_PS_FLAG)))
#define	RT_SET_PS_LEVEL(pPSC, _PS_FLAG)	(pPSC->CurPsLevel |= _PS_FLAG)
/* defined for skb cb field */
/* At most 28 byte */
typedef struct cb_desc {
	/* Tx Desc Related flags (8-9) */
	u8 bLastIniPkt:1;
	u8 bCmdOrInit:1;
	u8 bFirstSeg:1;
	u8 bLastSeg:1;
	u8 bEncrypt:1;
	u8 bTxDisableRateFallBack:1;
	u8 bTxUseDriverAssingedRate:1;
	u8 bHwSec:1; //indicate whether use Hw security. WB

	u8 nStuckCount; // stuck count AMY 090414

	/* Tx Firmware Relaged flags (10-11)*/
	u8 bCTSEnable:1;
	u8 bRTSEnable:1;
	u8 bUseShortGI:1;
	u8 bUseShortPreamble:1;
	u8 bTxEnableFwCalcDur:1;
	u8 bAMPDUEnable:1;
	u8 bRTSSTBC:1;
	u8 RTSSC:1;

	u8 bRTSBW:1;
	u8 bPacketBW:1;
	u8 bRTSUseShortPreamble:1;
	u8 bRTSUseShortGI:1;
	u8 bMulticast:1;
	u8 bBroadcast:1;
	u8 drv_agg_enable:1;
#ifdef _RTL8192_EXT_PATCH_
	u8 mesh_pkt:1;
#else
	u8 reserved2:1;
#endif

	/* Tx Desc related element(12-19) */
	u8 rata_index;
	u8 queue_index;
	u16 txbuf_size;
	u8 RATRIndex;
#ifdef _RTL8192_EXT_PATCH_
	u8 mesh_type:2;
	u8 bFromAggrQ:1;
	u8 bAMSDU:1;
	u8 brelay_pkt:1;
	u8 badhoc:1;
	u8 bretry_pkt:1;
	u8 bFromRx:1;
#else
	u8 bAMSDU:1;
	u8 bFromAggrQ:1;
	u8 reserved6:6;
#endif
	u8 macId;
	u8 priority;

	/* Tx firmware related element(20-27) */
	u8 data_rate;
	u8 rts_rate;
	u8 ampdu_factor;
	u8 ampdu_density;
	u8 DrvAggrNum;
	u8 bdhcp;
	u16 pkt_size;
}cb_desc, *pcb_desc;
                        
/*--------------------------Define -------------------------------------------*/
#define MGN_1M                  0x02
#define MGN_2M                  0x04
#define MGN_5_5M                0x0b
#define MGN_11M                 0x16

#define MGN_6M                  0x0c
#define MGN_9M                  0x12
#define MGN_12M                 0x18
#define MGN_18M                 0x24
#define MGN_24M                 0x30
#define MGN_36M                 0x48
#define MGN_48M                 0x60
#define MGN_54M                 0x6c

#define MGN_MCS0                0x80
#define MGN_MCS1                0x81
#define MGN_MCS2                0x82
#define MGN_MCS3                0x83
#define MGN_MCS4                0x84
#define MGN_MCS5                0x85
#define MGN_MCS6                0x86
#define MGN_MCS7                0x87
#define MGN_MCS8                0x88
#define MGN_MCS9                0x89
#define MGN_MCS10               0x8a
#define MGN_MCS11               0x8b
#define MGN_MCS12               0x8c
#define MGN_MCS13               0x8d
#define MGN_MCS14               0x8e
#define MGN_MCS15               0x8f
#define	MGN_MCS0_SG			0x90
#define	MGN_MCS1_SG			0x91
#define	MGN_MCS2_SG			0x92
#define	MGN_MCS3_SG			0x93
#define	MGN_MCS4_SG			0x94
#define	MGN_MCS5_SG			0x95
#define	MGN_MCS6_SG			0x96
#define	MGN_MCS7_SG			0x97
#define	MGN_MCS8_SG			0x98
#define	MGN_MCS9_SG			0x99
#define	MGN_MCS10_SG		0x9a
#define	MGN_MCS11_SG		0x9b
#define	MGN_MCS12_SG		0x9c
#define	MGN_MCS13_SG		0x9d
#define	MGN_MCS14_SG		0x9e
#define	MGN_MCS15_SG		0x9f


//----------------------------------------------------------------------------
//		802.11 Management frame Reason Code field
//----------------------------------------------------------------------------
enum	_ReasonCode{
	unspec_reason	= 0x1,
	auth_not_valid	= 0x2,
	deauth_lv_ss	= 0x3, 
	inactivity		= 0x4,
	ap_overload 	= 0x5, 
	class2_err		= 0x6,
	class3_err		= 0x7, 
	disas_lv_ss 	= 0x8,
	asoc_not_auth	= 0x9,

	//----MIC_CHECK
	mic_failure 	= 0xe,
	//----END MIC_CHECK

	// Reason code defined in 802.11i D10.0 p.28.
	invalid_IE		= 0x0d,
	four_way_tmout	= 0x0f,
	two_way_tmout	= 0x10,
	IE_dismatch 	= 0x11,
	invalid_Gcipher = 0x12,
	invalid_Pcipher = 0x13,
	invalid_AKMP	= 0x14,
	unsup_RSNIEver = 0x15,
	invalid_RSNIE	= 0x16,
	auth_802_1x_fail= 0x17,
	ciper_reject		= 0x18,

	// Reason code defined in 7.3.1.7, 802.1e D13.0, p.42. Added by Annie, 2005-11-15.
	QoS_unspec		= 0x20, // 32
	QAP_bandwidth	= 0x21, // 33
	poor_condition	= 0x22, // 34
	no_facility 	= 0x23, // 35
							// Where is 36???
	req_declined	= 0x25, // 37
	invalid_param	= 0x26, // 38
	req_not_honored= 0x27,	// 39
	TS_not_created	= 0x2F, // 47
	DL_not_allowed	= 0x30, // 48
	dest_not_exist	= 0x31, // 49
	dest_not_QSTA	= 0x32, // 50
};

//added by amy 090330
typedef enum _HW_VARIABLES{
	HW_VAR_ETHER_ADDR,
	HW_VAR_MULTICAST_REG,		
	HW_VAR_BASIC_RATE,
	HW_VAR_BSSID,
	HW_VAR_MEDIA_STATUS,
	HW_VAR_SECURITY_CONF,
	HW_VAR_BEACON_INTERVAL,
	HW_VAR_ATIM_WINDOW,	
	HW_VAR_LISTEN_INTERVAL,
	HW_VAR_CS_COUNTER,
	HW_VAR_DEFAULTKEY0,
	HW_VAR_DEFAULTKEY1,
	HW_VAR_DEFAULTKEY2,
	HW_VAR_DEFAULTKEY3,
	HW_VAR_SIFS,
	HW_VAR_DIFS,
	HW_VAR_EIFS,
	HW_VAR_SLOT_TIME,
	HW_VAR_ACK_PREAMBLE,
	HW_VAR_CW_CONFIG,
	HW_VAR_CW_VALUES,
	HW_VAR_RATE_FALLBACK_CONTROL,
	HW_VAR_CONTENTION_WINDOW,
	HW_VAR_RETRY_COUNT,
	HW_VAR_TR_SWITCH,
	HW_VAR_COMMAND,			// For Command Register, Annie, 2006-04-07.
	HW_VAR_WPA_CONFIG,		//2004/08/23, kcwu, for 8187 Security config
	HW_VAR_AMPDU_MIN_SPACE,	// The spacing between sub-frame. Roger, 2008.07.04.
	HW_VAR_SHORTGI_DENSITY,	// The density for shortGI. Roger, 2008.07.04.
	HW_VAR_AMPDU_FACTOR,
	HW_VAR_MCS_RATE_AVAILABLE,
	HW_VAR_AC_PARAM,			// For AC Parameters, 2005.12.01, by rcnjko.
	HW_VAR_ACM_CTRL,			// For ACM Control, Annie, 2005-12-13.
	HW_VAR_DIS_Req_Qsize,		// For DIS_Reg_Qsize, Joseph
	HW_VAR_CCX_CHNL_LOAD,		// For CCX 2 channel load request, 2006.05.04.
	HW_VAR_CCX_NOISE_HISTOGRAM,	// For CCX 2 noise histogram request, 2006.05.04.
	HW_VAR_CCX_CLM_NHM,			// For CCX 2 parallel channel load request and noise histogram request, 2006.05.12.
	HW_VAR_TxOPLimit,				// For turbo mode related settings, added by Roger, 2006.12.07
	HW_VAR_TURBO_MODE,			// For turbo mode related settings, added by Roger, 2006.12.15.
	HW_VAR_RF_STATE, 			// For change or query RF power state, 061214, rcnjko.
	HW_VAR_RF_OFF_BY_HW,		// For UI to query if external HW signal disable RF, 061229, rcnjko. 
	HW_VAR_BUS_SPEED, 		// In unit of bps. 2006.07.03, by rcnjko.
        HW_VAR_SET_DEV_POWER,	// Set to low power, added by LanHsin, 2007.

	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//1Attention Please!!!<11n or 8190 specific code should be put below this line>
	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
	HW_VAR_RCR,				//for RCR, David 2006,05,11
	HW_VAR_RATR_0,
	HW_VAR_RRSR,
	HW_VAR_CPU_RST,
	HW_VAR_CECHK_BSSID,
        HW_VAR_LBK_MODE,			// Set lookback mode, 2008.06.11. added by Roger.	
	// Set HW related setting for 11N AES bug.
	HW_VAR_AES_11N_FIX,
	// Set Usb Rx Aggregation
	HW_VAR_USB_RX_AGGR,
	HW_VAR_USER_CONTROL_TURBO_MODE,
	HW_VAR_RETRY_LIMIT,
#ifndef _RTL8192_EXT_PATCH_
	HW_VAR_INIT_TX_RATE,  //Get Current Tx rate register. 2008.12.10. Added by tynli
#endif
	HW_VAR_TX_RATE_REG,  //Get Current Tx rate register. 2008.12.10. Added by tynli
	HW_VAR_EFUSE_USAGE, //Get current EFUSE utilization. 2008.12.19. Added by Roger.
	HW_VAR_EFUSE_BYTES,
	HW_VAR_AUTOLOAD_STATUS, //Get current autoload status, 0: autoload success, 1: autoload fail. 2008.12.19. Added by Roger.
	HW_VAR_RF_2R_DISABLE, // 2R disable
	HW_VAR_SET_RPWM,
	HW_VAR_H2C_FW_PWRMODE, // For setting FW related H2C cmd structure. by tynli. 2009.2.18
	HW_VAR_H2C_FW_JOINBSSRPT, // For setting FW related H2C cmd structure. by tynli. 2009.2.18
	HW_VAR_1X1_RECV_COMBINE,	// For 1T2R but only 1SS, Add by hpfan 2009.04.16 hpfan
}HW_VARIABLES;

//added by amy 090330 end

#define aSifsTime	 (((priv->rtllib->current_network.mode == IEEE_A)||(priv->rtllib->current_network.mode == IEEE_N_24G)||(priv->rtllib->current_network.mode == IEEE_N_5G))? 16 : 10)

#define MGMT_QUEUE_NUM 5

#define IEEE_CMD_SET_WPA_PARAM			1
#define	IEEE_CMD_SET_WPA_IE			2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME				4

#define IEEE_PARAM_WPA_ENABLED			1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED		3
#define IEEE_PARAM_PRIVACY_INVOKED		4
#define IEEE_PARAM_AUTH_ALGS			5
#define IEEE_PARAM_IEEE_802_1X			6
//It should consistent with the driver_XXX.c
//   David, 2006.9.26
#define IEEE_PARAM_WPAX_SELECT			7
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_PROTO_WPA				1	
#define IEEE_PROTO_RSN				2
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_WPAX_USEGROUP			0
#define IEEE_WPAX_WEP40				1
#define IEEE_WPAX_TKIP				2
#define IEEE_WPAX_WRAP   			3
#define IEEE_WPAX_CCMP				4
#define IEEE_WPAX_WEP104			5

#define IEEE_KEY_MGMT_IEEE8021X			1
#define IEEE_KEY_MGMT_PSK			2

#define IEEE_MLME_STA_DEAUTH			1
#define IEEE_MLME_STA_DISASSOC			2


#define IEEE_CRYPT_ERR_UNKNOWN_ALG		2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR		3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED	4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED		5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED	6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7


#define	IEEE_CRYPT_ALG_NAME_LEN			16

#define MAX_IE_LEN  0xff

// added for kernel conflict 
#define rtllib_crypt_deinit_entries 	rtllib_crypt_deinit_entries_rsl
#define rtllib_crypt_deinit_handler 	rtllib_crypt_deinit_handler_rsl
#define rtllib_crypt_delayed_deinit 	rtllib_crypt_delayed_deinit_rsl
#define rtllib_register_crypto_ops  	rtllib_register_crypto_ops_rsl
#define rtllib_unregister_crypto_ops rtllib_unregister_crypto_ops_rsl
#define rtllib_get_crypto_ops 	rtllib_get_crypto_ops_rsl

#define rtllib_ccmp_null		rtllib_ccmp_null_rsl

#define rtllib_tkip_null		rtllib_tkip_null_rsl

#define rtllib_wep_null		rtllib_wep_null_rsl

#define free_rtllib          	free_rtllib_rsl
#define alloc_rtllib        		alloc_rtllib_rsl

#define rtllib_rx 			rtllib_rx_rsl
#define rtllib_rx_mgt		rtllib_rx_mgt_rsl
#define rtllib_rx_probe_rq           rtllib_rx_probe_rq_rsl

#define rtllib_get_beacon		rtllib_get_beacon_rsl
#define rtllib_wake_queue		rtllib_wake_queue_rsl
#define rtllib_stop_queue		rtllib_stop_queue_rsl
#define rtllib_reset_queue		rtllib_reset_queue_rsl
#define rtllib_softmac_stop_protocol	rtllib_softmac_stop_protocol_rsl
#define rtllib_softmac_start_protocol rtllib_softmac_start_protocol_rsl
#define rtllib_is_shortslot		rtllib_is_shortslot_rsl
#define rtllib_is_54g		rtllib_is_54g_rsl
#define rtllib_wpa_supplicant_ioctl	rtllib_wpa_supplicant_ioctl_rsl
#define rtllib_ps_tx_ack		rtllib_ps_tx_ack_rsl
#define rtllib_softmac_xmit		rtllib_softmac_xmit_rsl
#define rtllib_stop_send_beacons	rtllib_stop_send_beacons_rsl
#define notify_wx_assoc_event		notify_wx_assoc_event_rsl
#define SendDisassociation		SendDisassociation_rsl
#define rtllib_disassociate		rtllib_disassociate_rsl
#define rtllib_start_send_beacons	rtllib_start_send_beacons_rsl
#define rtllib_stop_scan		rtllib_stop_scan_rsl
#define rtllib_send_probe_requests	rtllib_send_probe_requests_rsl
#define rtllib_softmac_scan_syncro	rtllib_softmac_scan_syncro_rsl
#define rtllib_start_scan_syncro	rtllib_start_scan_syncro_rsl
#define rtllib_start_scan		rtllib_start_scan_rsl

#define rtllib_wx_get_essid		rtllib_wx_get_essid_rsl
#define rtllib_wx_set_essid		rtllib_wx_set_essid_rsl
#define rtllib_wx_set_rate		rtllib_wx_set_rate_rsl
#define rtllib_wx_get_rate		rtllib_wx_get_rate_rsl
#define rtllib_wx_set_wap		rtllib_wx_set_wap_rsl
#define rtllib_wx_get_wap		rtllib_wx_get_wap_rsl
#define rtllib_wx_set_mode		rtllib_wx_set_mode_rsl 
#define rtllib_wx_get_mode		rtllib_wx_get_mode_rsl
#define rtllib_wx_set_scan		rtllib_wx_set_scan_rsl
#define rtllib_wx_get_freq		rtllib_wx_get_freq_rsl
#define rtllib_wx_set_freq		rtllib_wx_set_freq_rsl
#define rtllib_wx_set_rawtx		rtllib_wx_set_rawtx_rsl
#define rtllib_wx_get_name		rtllib_wx_get_name_rsl
#define rtllib_wx_set_power		rtllib_wx_set_power_rsl
#define rtllib_wx_get_power		rtllib_wx_get_power_rsl
#define rtllib_wlan_frequencies	rtllib_wlan_frequencies_rsl
#define rtllib_wx_set_rts		rtllib_wx_set_rts_rsl
#define rtllib_wx_get_rts		rtllib_wx_get_rts_rsl

#define rtllib_txb_free		rtllib_txb_free_rsl

#define rtllib_wx_set_gen_ie		rtllib_wx_set_gen_ie_rsl
#define rtllib_wx_get_scan		rtllib_wx_get_scan_rsl
#define rtllib_wx_set_encode		rtllib_wx_set_encode_rsl
#define rtllib_wx_get_encode		rtllib_wx_get_encode_rsl
#if WIRELESS_EXT >= 18
#define rtllib_wx_set_mlme		rtllib_wx_set_mlme_rsl
#define rtllib_wx_set_auth		rtllib_wx_set_auth_rsl
#define rtllib_wx_set_encode_ext	rtllib_wx_set_encode_ext_rsl
#define rtllib_wx_get_encode_ext	rtllib_wx_get_encode_ext_rsl
#endif

#ifdef _RTL8192_EXT_PATCH_
#define ENABLE_NULL_PT_DEBUG
#ifdef ENABLE_NULL_PT_DEBUG
#define RT_ASSERT_RET(_Exp)								\
		if(!(_Exp))									\
		{											\
			printk("Rtl819x: ");					\
                	printk( "Assertion failed! %s,%s,line=%d\n", \
                	#_Exp,__FUNCTION__,__LINE__);          \
			return;						\
		}
#define RT_ASSERT_RET_VALUE(_Exp,Ret)								\
		if(!(_Exp))									\
		{											\
			printk("Rtl819x: ");					\
                	printk( "Assertion failed! %s,%s,line=%d\n", \
                	#_Exp,__FUNCTION__,__LINE__);          \
			return (Ret);						\
		}
#else
#define RT_ASSERT_RET(_Exp) do {} while(0)
#define RT_ASSERT_RET_VALUE(_Exp,Ret) do {} while(0)
#endif
#endif

typedef struct ieee_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
        union {
		struct {
			u8 name;
			u32 value;
		} wpa_param;
		struct {
			u32 len;
			u8 reserved[32];
			u8 data[0];
		} wpa_ie;
	        struct{
			int command;
    			int reason_code;
		} mlme;
		struct {
			u8 alg[IEEE_CRYPT_ALG_NAME_LEN];
			u8 set_tx;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[0];
		} crypt;
	} u;
}ieee_param;


#if WIRELESS_EXT < 17
#define IW_QUAL_QUAL_INVALID   0x10
#define IW_QUAL_LEVEL_INVALID  0x20
#define IW_QUAL_NOISE_INVALID  0x40
#define IW_QUAL_QUAL_UPDATED   0x1
#define IW_QUAL_LEVEL_UPDATED  0x2
#define IW_QUAL_NOISE_UPDATED  0x4
#endif

// linux under 2.6.9 release may not support it, so modify it for common use
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9))
//#define MSECS(t)	(1000 * ((t) / HZ) + 1000 * ((t) % HZ) / HZ)
#define MSECS(t)	(HZ * ((t) / 1000) + (HZ * ((t) % 1000)) / 1000)
static inline unsigned long msleep_interruptible_rsl(unsigned int msecs)
{
         unsigned long timeout = MSECS(msecs) + 1;
 
         while (timeout) {
                 set_current_state(TASK_INTERRUPTIBLE);
                 timeout = schedule_timeout(timeout);
         }
         return timeout;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,31))
static inline void msleep(unsigned int msecs)
{
         unsigned long timeout = MSECS(msecs) + 1;
 
         while (timeout) {
                 set_current_state(TASK_UNINTERRUPTIBLE);
                 timeout = schedule_timeout(timeout);
         }
}
#endif
#else
#define MSECS(t) msecs_to_jiffies(t)
#define msleep_interruptible_rsl  msleep_interruptible
#endif

#define RTLLIB_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */
#define RTLLIB_1ADDR_LEN 10
#define RTLLIB_2ADDR_LEN 16
#define RTLLIB_3ADDR_LEN 24
#define RTLLIB_4ADDR_LEN 30
#define RTLLIB_FCS_LEN    4
#define RTLLIB_HLEN                  (RTLLIB_4ADDR_LEN)
#define RTLLIB_FRAME_LEN             (RTLLIB_DATA_LEN + RTLLIB_HLEN)
#define RTLLIB_MGMT_HDR_LEN 24
#define RTLLIB_DATA_HDR3_LEN 24
#define RTLLIB_DATA_HDR4_LEN 30

#define MIN_FRAG_THRESHOLD     256U
#define MAX_FRAG_THRESHOLD     2346U
#define MAX_HT_DATA_FRAG_THRESHOLD 0x2000

#define HT_AMSDU_SIZE_4K 3839
#define HT_AMSDU_SIZE_8K 7935

/* Frame control field constants */
#define RTLLIB_FCTL_VERS		0x0003
#define RTLLIB_FCTL_FTYPE		0x000c
#define RTLLIB_FCTL_STYPE		0x00f0
#define RTLLIB_FCTL_FRAMETYPE	0x00fc
#define RTLLIB_FCTL_TODS		0x0100
#define RTLLIB_FCTL_FROMDS		0x0200
#define RTLLIB_FCTL_DSTODS		0x0300 //added by david
#define RTLLIB_FCTL_MOREFRAGS	0x0400
#define RTLLIB_FCTL_RETRY		0x0800
#define RTLLIB_FCTL_PM		0x1000
#define RTLLIB_FCTL_MOREDATA		0x2000
#define RTLLIB_FCTL_WEP		0x4000
#define RTLLIB_FCTL_ORDER		0x8000

#define RTLLIB_FTYPE_MGMT		0x0000
#define RTLLIB_FTYPE_CTL		0x0004
#define RTLLIB_FTYPE_DATA		0x0008

/* management */
#define RTLLIB_STYPE_ASSOC_REQ	0x0000
#define RTLLIB_STYPE_ASSOC_RESP 	0x0010
#define RTLLIB_STYPE_REASSOC_REQ	0x0020
#define RTLLIB_STYPE_REASSOC_RESP	0x0030
#define RTLLIB_STYPE_PROBE_REQ	0x0040
#define RTLLIB_STYPE_PROBE_RESP	0x0050
#define RTLLIB_STYPE_BEACON		0x0080
#define RTLLIB_STYPE_ATIM		0x0090
#define RTLLIB_STYPE_DISASSOC	0x00A0
#define RTLLIB_STYPE_AUTH		0x00B0
#define RTLLIB_STYPE_DEAUTH		0x00C0
#define RTLLIB_STYPE_MANAGE_ACT	0x00D0
#ifdef _RTL8192_EXT_PATCH_
/* added for mesh action frame type */
#define RTLLIB_STYPE_MESH_ACT        0x00F0
#endif

/* control */
#define RTLLIB_STYPE_PSPOLL		0x00A0
#define RTLLIB_STYPE_RTS		0x00B0
#define RTLLIB_STYPE_CTS		0x00C0
#define RTLLIB_STYPE_ACK		0x00D0
#define RTLLIB_STYPE_CFEND		0x00E0
#define RTLLIB_STYPE_CFENDACK	0x00F0
#define RTLLIB_STYPE_BLOCKACK   0x0094

/* data */
#define RTLLIB_STYPE_DATA		0x0000
#define RTLLIB_STYPE_DATA_CFACK	0x0010
#define RTLLIB_STYPE_DATA_CFPOLL	0x0020
#define RTLLIB_STYPE_DATA_CFACKPOLL	0x0030
#define RTLLIB_STYPE_NULLFUNC	0x0040
#define RTLLIB_STYPE_CFACK		0x0050
#define RTLLIB_STYPE_CFPOLL		0x0060
#define RTLLIB_STYPE_CFACKPOLL	0x0070
#define RTLLIB_STYPE_QOS_DATA	0x0080 //added for WMM 2006/8/2
#define RTLLIB_STYPE_QOS_NULL	0x00C0

#define RTLLIB_SCTL_FRAG		0x000F
#define RTLLIB_SCTL_SEQ		0xFFF0

/* QOS control */
#define RTLLIB_QCTL_TID              0x000F

#define	FC_QOS_BIT					BIT7
#define IsDataFrame(pdu)			( ((pdu[0] & 0x0C)==0x08) ? true : false )
#define	IsLegacyDataFrame(pdu)	(IsDataFrame(pdu) && (!(pdu[0]&FC_QOS_BIT)) )	
//added by wb. Is this right?
#define IsQoSDataFrame(pframe)  ((*(u16*)pframe&(RTLLIB_STYPE_QOS_DATA|RTLLIB_FTYPE_DATA)) == (RTLLIB_STYPE_QOS_DATA|RTLLIB_FTYPE_DATA))
#define Frame_Order(pframe)     (*(u16*)pframe&RTLLIB_FCTL_ORDER)
#define SN_LESS(a, b)		(((a-b)&0x800)!=0)
#define SN_EQUAL(a, b)	(a == b)
#if defined(RTL8192SU) || defined(RTL8192U)
typedef struct rtl8192_tx_info {
        struct urb *urb;
        struct urb *zero;
        //u32 out_pipe;
}rtl8192_tx_info ;
#define MAX_DEV_ADDR_SIZE sizeof(struct rtl8192_tx_info)
#else
#define MAX_DEV_ADDR_SIZE 8
#endif

typedef enum _ACT_CATEGORY{
        ACT_CAT_QOS = 1,
        ACT_CAT_DLS = 2,
        ACT_CAT_BA  = 3,
        ACT_CAT_HT  = 7,
        ACT_CAT_WMM = 17,
#ifdef _RTL8192_EXT_PATCH_
        ACT_CAT_MESH_PEERLINK_MGNT = 30,
        ACT_CAT_MESH_LINK_METRIC = 31,
        ACT_CAT_MESH_PATH_SELECT = 32,
        ACT_CAT_MESH_INTERWORKING = 33,
        ACT_CAT_MESH_SECURITY_INFO = 35,
#endif
} ACT_CATEGORY, *PACT_CATEGORY;

typedef enum _TS_ACTION{
        ACT_ADDTSREQ = 0,
        ACT_ADDTSRSP = 1,
        ACT_DELTS    = 2,
        ACT_SCHEDULE = 3,
} TS_ACTION, *PTS_ACTION;

typedef enum _BA_ACTION{
        ACT_ADDBAREQ = 0,
        ACT_ADDBARSP = 1,
        ACT_DELBA    = 2,
} BA_ACTION, *PBA_ACTION;
#ifdef _RTL8192_EXT_PATCH_
typedef enum _PEERLINK_ACTION{
	ACT_PEERLINK_OPEN = 0,
	ACT_PEERLINK_CONFIRM = 1,
	ACT_PEERLINK_CLOSE =2,
} PEERLINK_ACTION, *PPEERLINK_ACTION;

typedef enum _LINK_METRIC_ACTION{
	ACT_LINKMETRIC_REQ = 0,
	ACT_LINKMETRIC_RSP = 1,
} LINK_METRIC_ACTION, *PLINK_METRIC_ACTION;

typedef enum _PATH_SELECT_ACTION{
	ACT_PATH_REQ = 0,
	ACT_PATH_REPLY = 1,
	ACT_PATH_ERR =2,
	ACT_RANN =3,
} PATH_SELECT_ACTION, *PPATH_SELECT_ACTION;

typedef enum _MESH_PEERING_PROTOCOL_VERSION_VALUE{
	Mesh_Peering_Management_Protocol_V =42,
	Abbreviated_Handshake_Protocol_V = 43,
}MESH_PEERING_PROTOCOL_VERSION_VALUE;
#endif

typedef enum _InitialGainOpType{
	IG_Backup=0,
	IG_Restore,
	IG_Max
}InitialGainOpType;
//added by amy for LED 090319
//================================================================================
// LED customization.
//================================================================================
typedef enum _LED_CTL_MODE{
        LED_CTL_POWER_ON = 1,
        LED_CTL_LINK = 2,
        LED_CTL_NO_LINK = 3,
        LED_CTL_TX = 4,
        LED_CTL_RX = 5,
        LED_CTL_SITE_SURVEY = 6,
        LED_CTL_POWER_OFF = 7,
        LED_CTL_START_TO_LINK = 8,
        LED_CTL_START_WPS = 9,
        LED_CTL_STOP_WPS = 10,
        LED_CTL_START_WPS_BOTTON = 11, //added for runtop
        LED_CTL_STOP_WPS_FAIL = 12, //added for ALPHA	
	 LED_CTL_STOP_WPS_FAIL_OVERLAP = 13, //added for BELKIN
}LED_CTL_MODE;

typedef enum _RT_RF_TYPE_DEF
{
	RF_1T2R = 0,
	RF_2T4R,
	RF_2T2R,
	RF_1T1R,
	RF_2T2R_GREEN,
	RF_819X_MAX_TYPE
}RT_RF_TYPE_DEF;

typedef enum _WIRELESS_MODE {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
	WIRELESS_MODE_N_24G = 0x10,
	WIRELESS_MODE_N_5G = 0x20
} WIRELESS_MODE;

// This definition is used for FW CMD (FW_RA_UPDATE_MASK) to distinguish network type
typedef enum _NETWORK_TYPE{
	WIRELESS_11B = 1,
	WIRELESS_11G = 2,
	WIRELESS_11A = 4,
	WIRELESS_11N = 8
} WIRELESS_NETWORK_TYPE;

//added by amy for adhoc 090402
#define OUI_SUBTYPE_WMM_INFO		0
#define OUI_SUBTYPE_WMM_PARAM	1
#define OUI_SUBTYPE_QOS_CAPABI	5
//added by amy for adhoc 090402 end

/* debug macros */
#define CONFIG_RTLLIB_DEBUG
#ifdef CONFIG_RTLLIB_DEBUG
extern u32 rtllib_debug_level;
#define RTLLIB_DEBUG(level, fmt, args...) \
do { if (rtllib_debug_level & (level)) \
  printk(KERN_DEBUG "rtllib: " fmt, ## args); } while (0)
//wb added to debug out data buf
//if you want print DATA buffer related BA, please set rtllib_debug_level to DATA|BA
#define RTLLIB_DEBUG_DATA(level, data, datalen)	\
	do{ if ((rtllib_debug_level & (level)) == (level))	\
		{ 	\
			int i;					\
			u8* pdata = (u8*) data;			\
			printk(KERN_DEBUG "rtllib: %s()\n", __FUNCTION__);	\
			for(i=0; i<(int)(datalen); i++)			\
			{						\
				printk("%2x ", pdata[i]);		\
				if ((i+1)%16 == 0) printk("\n");	\
			}				\
			printk("\n");			\
		}					\
	} while (0)		
#else
#define RTLLIB_DEBUG(level, fmt, args...) do {} while (0)
#define RTLLIB_DEBUG_DATA(level, data, datalen) do {} while(0)
#endif	/* CONFIG_RTLLIB_DEBUG */

/* debug macros not dependent on CONFIG_RTLLIB_DEBUG */

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARG(x) ((u8*)(x))[0],((u8*)(x))[1],((u8*)(x))[2],((u8*)(x))[3],((u8*)(x))[4],((u8*)(x))[5]

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define RTLLIB_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a RTLLIB_xxxx_DEBUG() macro definition for your
 * classification, or use RTLLIB_DEBUG(RTLLIB_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ipw/debug_level
 *
 * you simply need to add your entry to the ipw_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/ipw then you do not have
 * CONFIG_RTLLIB_DEBUG defined in your kernel configuration
 *
 */

#define RTLLIB_DL_INFO          (1<<0)
#define RTLLIB_DL_WX            (1<<1)
#define RTLLIB_DL_SCAN          (1<<2)
#define RTLLIB_DL_STATE         (1<<3)
#define RTLLIB_DL_MGMT          (1<<4)
#define RTLLIB_DL_FRAG          (1<<5)
#define RTLLIB_DL_EAP           (1<<6)
#define RTLLIB_DL_DROP          (1<<7)

#define RTLLIB_DL_TX            (1<<8)
#define RTLLIB_DL_RX            (1<<9)

#define RTLLIB_DL_HT		   (1<<10)  //HT
#define RTLLIB_DL_BA		   (1<<11)  //ba
#define RTLLIB_DL_TS		   (1<<12)  //TS
#define RTLLIB_DL_QOS           (1<<13)
#define RTLLIB_DL_REORDER	   (1<<14)
#define RTLLIB_DL_IOT	   (1<<15)
#define RTLLIB_DL_IPS	   (1<<16)
#define RTLLIB_DL_TRACE	   (1<<29)  //trace function, need to user net_ratelimit() together in order not to print too much to the screen
#define RTLLIB_DL_DATA	   (1<<30)   //use this flag to control whether print data buf out.
#define RTLLIB_DL_ERR	   (1<<31)   //always open
#define RTLLIB_ERROR(f, a...) printk(KERN_ERR "rtllib: " f, ## a)
#define RTLLIB_WARNING(f, a...) printk(KERN_WARNING "rtllib: " f, ## a)
#define RTLLIB_DEBUG_INFO(f, a...)   RTLLIB_DEBUG(RTLLIB_DL_INFO, f, ## a)

#define RTLLIB_DEBUG_WX(f, a...)     RTLLIB_DEBUG(RTLLIB_DL_WX, f, ## a)
#define RTLLIB_DEBUG_SCAN(f, a...)   RTLLIB_DEBUG(RTLLIB_DL_SCAN, f, ## a)
#define RTLLIB_DEBUG_STATE(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_STATE, f, ## a)
#define RTLLIB_DEBUG_MGMT(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_MGMT, f, ## a)
#define RTLLIB_DEBUG_FRAG(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_FRAG, f, ## a)
#define RTLLIB_DEBUG_EAP(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_EAP, f, ## a)
#define RTLLIB_DEBUG_DROP(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_DROP, f, ## a)
#define RTLLIB_DEBUG_TX(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_TX, f, ## a)
#define RTLLIB_DEBUG_RX(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_RX, f, ## a)
#define RTLLIB_DEBUG_QOS(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_QOS, f, ## a)

#ifdef CONFIG_RTLLIB_DEBUG
/* Added by Annie, 2005-11-22. */
#define MAX_STR_LEN     64
/* I want to see ASCII 33 to 126 only. Otherwise, I print '?'. Annie, 2005-11-22.*/
#define PRINTABLE(_ch)  (_ch>'!' && _ch<'~')    
#define RTLLIB_PRINT_STR(_Comp, _TitleString, _Ptr, _Len)                            	\
                        if((_Comp) & level)   							\
                        {                                                                       \
                                int             __i;                                            \
                                u8  buffer[MAX_STR_LEN];                                    	\
                                int length = (_Len<MAX_STR_LEN)? _Len : (MAX_STR_LEN-1) ;  	\
                                memset(buffer, 0, MAX_STR_LEN);                      		\
                                memcpy(buffer, (u8 *)_Ptr, length );            		\
                                for( __i=0; __i<MAX_STR_LEN; __i++ )                            \
                                {                                                               \
                                     if( !PRINTABLE(buffer[__i]) )   buffer[__i] = '?';     	\
                                }                                                               \
                                buffer[length] = '\0';                                          \
                                printk("Rtl819x: ");                                         	\
                                printk(_TitleString);                                         \
                                printk(": %d, <%s>\n", _Len, buffer);                         \
                        }
#else
#define RTLLIB_PRINT_STR(_Comp, _TitleString, _Ptr, _Len)  do {} while (0)
#endif

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
#define ETH_P_ARP	0x0806		/* Address Resolution packet	*/
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct rtllib_snap_hdr {

        u8    dsap;   /* always 0xAA */
        u8    ssap;   /* always 0xAA */
        u8    ctrl;   /* always 0x03 */
        u8    oui[P80211_OUI_LEN];    /* organizational universal id */

} __attribute__ ((packed));

#define SNAP_SIZE sizeof(struct rtllib_snap_hdr)

#define WLAN_FC_GET_VERS(fc) ((fc) & RTLLIB_FCTL_VERS)
#define WLAN_FC_GET_TYPE(fc) ((fc) & RTLLIB_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & RTLLIB_FCTL_STYPE)
#define WLAN_FC_MORE_DATA(fc) ((fc) & RTLLIB_FCTL_MOREDATA)

#define WLAN_FC_GET_FRAMETYPE(fc) ((fc) & RTLLIB_FCTL_FRAMETYPE)
#define WLAN_GET_SEQ_FRAG(seq) ((seq) & RTLLIB_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  (((seq) & RTLLIB_SCTL_SEQ) >> 4)

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1
//#define WLAN_AUTH_LEAP 2
#define WLAN_AUTH_LEAP 128

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_ESS (1<<0)
#define WLAN_CAPABILITY_IBSS (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST (1<<3)
#define WLAN_CAPABILITY_PRIVACY (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE (1<<5)
#define WLAN_CAPABILITY_PBCC (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY (1<<7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT (1<<8)
#define WLAN_CAPABILITY_QOS (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME (1<<10)
#define WLAN_CAPABILITY_DSSS_OFDM (1<<13)

/* 802.11g ERP information element */
#define WLAN_ERP_NON_ERP_PRESENT (1<<0)
#define WLAN_ERP_USE_PROTECTION (1<<1)
#define WLAN_ERP_BARKER_PREAMBLE (1<<2)

/* Status codes */
enum rtllib_statuscode {
        WLAN_STATUS_SUCCESS = 0,
        WLAN_STATUS_UNSPECIFIED_FAILURE = 1,
        WLAN_STATUS_CAPS_UNSUPPORTED = 10,
        WLAN_STATUS_REASSOC_NO_ASSOC = 11,
        WLAN_STATUS_ASSOC_DENIED_UNSPEC = 12,
        WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG = 13,
        WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION = 14,
        WLAN_STATUS_CHALLENGE_FAIL = 15,
        WLAN_STATUS_AUTH_TIMEOUT = 16,
        WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA = 17,
        WLAN_STATUS_ASSOC_DENIED_RATES = 18,
        /* 802.11b */
        WLAN_STATUS_ASSOC_DENIED_NOSHORTPREAMBLE = 19,
        WLAN_STATUS_ASSOC_DENIED_NOPBCC = 20,
        WLAN_STATUS_ASSOC_DENIED_NOAGILITY = 21,
        /* 802.11h */
        WLAN_STATUS_ASSOC_DENIED_NOSPECTRUM = 22,
        WLAN_STATUS_ASSOC_REJECTED_BAD_POWER = 23,
        WLAN_STATUS_ASSOC_REJECTED_BAD_SUPP_CHAN = 24,
        /* 802.11g */
        WLAN_STATUS_ASSOC_DENIED_NOSHORTTIME = 25,
        WLAN_STATUS_ASSOC_DENIED_NODSSSOFDM = 26,
        /* 802.11i */
        WLAN_STATUS_INVALID_IE = 40,
        WLAN_STATUS_INVALID_GROUP_CIPHER = 41,
        WLAN_STATUS_INVALID_PAIRWISE_CIPHER = 42,
        WLAN_STATUS_INVALID_AKMP = 43,
        WLAN_STATUS_UNSUPP_RSN_VERSION = 44,
        WLAN_STATUS_INVALID_RSN_IE_CAP = 45,
        WLAN_STATUS_CIPHER_SUITE_REJECTED = 46,
};

/* Reason codes */
enum rtllib_reasoncode {
        WLAN_REASON_UNSPECIFIED = 1,
        WLAN_REASON_PREV_AUTH_NOT_VALID = 2,
        WLAN_REASON_DEAUTH_LEAVING = 3,
        WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY = 4,
        WLAN_REASON_DISASSOC_AP_BUSY = 5,
        WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA = 6,
        WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA = 7,
        WLAN_REASON_DISASSOC_STA_HAS_LEFT = 8,
        WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH = 9,
        /* 802.11h */
        WLAN_REASON_DISASSOC_BAD_POWER = 10,
        WLAN_REASON_DISASSOC_BAD_SUPP_CHAN = 11,
        /* 802.11i */
        WLAN_REASON_INVALID_IE = 13,
        WLAN_REASON_MIC_FAILURE = 14,
        WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
        WLAN_REASON_GROUP_KEY_HANDSHAKE_TIMEOUT = 16,
        WLAN_REASON_IE_DIFFERENT = 17,
        WLAN_REASON_INVALID_GROUP_CIPHER = 18,
        WLAN_REASON_INVALID_PAIRWISE_CIPHER = 19,
        WLAN_REASON_INVALID_AKMP = 20,
        WLAN_REASON_UNSUPP_RSN_VERSION = 21,
        WLAN_REASON_INVALID_RSN_IE_CAP = 22,
        WLAN_REASON_IEEE8021X_FAILED = 23,
        WLAN_REASON_CIPHER_SUITE_REJECTED = 24,
};

#define RTLLIB_STATMASK_SIGNAL (1<<0)
#define RTLLIB_STATMASK_RSSI (1<<1)
#define RTLLIB_STATMASK_NOISE (1<<2)
#define RTLLIB_STATMASK_RATE (1<<3)
#define RTLLIB_STATMASK_WEMASK 0x7

#define RTLLIB_CCK_MODULATION    (1<<0)
#define RTLLIB_OFDM_MODULATION   (1<<1)

#define RTLLIB_24GHZ_BAND     (1<<0)
#define RTLLIB_52GHZ_BAND     (1<<1)

#define RTLLIB_CCK_RATE_LEN  		4
#define RTLLIB_CCK_RATE_1MB		        0x02
#define RTLLIB_CCK_RATE_2MB		        0x04
#define RTLLIB_CCK_RATE_5MB		        0x0B
#define RTLLIB_CCK_RATE_11MB		        0x16
#define RTLLIB_OFDM_RATE_LEN 		8
#define RTLLIB_OFDM_RATE_6MB		        0x0C
#define RTLLIB_OFDM_RATE_9MB		        0x12
#define RTLLIB_OFDM_RATE_12MB		0x18
#define RTLLIB_OFDM_RATE_18MB		0x24
#define RTLLIB_OFDM_RATE_24MB		0x30
#define RTLLIB_OFDM_RATE_36MB		0x48
#define RTLLIB_OFDM_RATE_48MB		0x60
#define RTLLIB_OFDM_RATE_54MB		0x6C
#define RTLLIB_BASIC_RATE_MASK		0x80

#define RTLLIB_CCK_RATE_1MB_MASK		(1<<0)
#define RTLLIB_CCK_RATE_2MB_MASK		(1<<1)
#define RTLLIB_CCK_RATE_5MB_MASK		(1<<2)
#define RTLLIB_CCK_RATE_11MB_MASK		(1<<3)
#define RTLLIB_OFDM_RATE_6MB_MASK		(1<<4)
#define RTLLIB_OFDM_RATE_9MB_MASK		(1<<5)
#define RTLLIB_OFDM_RATE_12MB_MASK		(1<<6)
#define RTLLIB_OFDM_RATE_18MB_MASK		(1<<7)
#define RTLLIB_OFDM_RATE_24MB_MASK		(1<<8)
#define RTLLIB_OFDM_RATE_36MB_MASK		(1<<9)
#define RTLLIB_OFDM_RATE_48MB_MASK		(1<<10)
#define RTLLIB_OFDM_RATE_54MB_MASK		(1<<11)

#define RTLLIB_CCK_RATES_MASK	        0x0000000F
#define RTLLIB_CCK_BASIC_RATES_MASK	(RTLLIB_CCK_RATE_1MB_MASK | \
	RTLLIB_CCK_RATE_2MB_MASK)
#define RTLLIB_CCK_DEFAULT_RATES_MASK	(RTLLIB_CCK_BASIC_RATES_MASK | \
        RTLLIB_CCK_RATE_5MB_MASK | \
        RTLLIB_CCK_RATE_11MB_MASK)

#define RTLLIB_OFDM_RATES_MASK		0x00000FF0
#define RTLLIB_OFDM_BASIC_RATES_MASK	(RTLLIB_OFDM_RATE_6MB_MASK | \
	RTLLIB_OFDM_RATE_12MB_MASK | \
	RTLLIB_OFDM_RATE_24MB_MASK)
#define RTLLIB_OFDM_DEFAULT_RATES_MASK	(RTLLIB_OFDM_BASIC_RATES_MASK | \
	RTLLIB_OFDM_RATE_9MB_MASK  | \
	RTLLIB_OFDM_RATE_18MB_MASK | \
	RTLLIB_OFDM_RATE_36MB_MASK | \
	RTLLIB_OFDM_RATE_48MB_MASK | \
	RTLLIB_OFDM_RATE_54MB_MASK)
#define RTLLIB_DEFAULT_RATES_MASK (RTLLIB_OFDM_DEFAULT_RATES_MASK | \
                                RTLLIB_CCK_DEFAULT_RATES_MASK)

#define RTLLIB_NUM_OFDM_RATES	    8
#define RTLLIB_NUM_CCK_RATES	            4
#define RTLLIB_OFDM_SHIFT_MASK_A         4


/* this is stolen and modified from the madwifi driver*/
#define RTLLIB_FC0_TYPE_MASK		0x0c
#define RTLLIB_FC0_TYPE_DATA		0x08
#define RTLLIB_FC0_SUBTYPE_MASK	0xB0
#define RTLLIB_FC0_SUBTYPE_QOS	0x80

#define RTLLIB_QOS_HAS_SEQ(fc) \
	(((fc) & (RTLLIB_FC0_TYPE_MASK | RTLLIB_FC0_SUBTYPE_MASK)) == \
	 (RTLLIB_FC0_TYPE_DATA | RTLLIB_FC0_SUBTYPE_QOS))
	
/* this is stolen from ipw2200 driver */
#define IEEE_IBSS_MAC_HASH_SIZE 31
struct ieee_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num[17];
	u16 frag_num[17];
	unsigned long packet_time[17];
	struct list_head list;
};

#ifdef _RTL8192_EXT_PATCH_
struct ieee_mesh_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num[17];
	u16 frag_num[17];
	unsigned long packet_time[17];
	struct list_head list;
};
#endif
/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.  Not setting these will not cause
 *       any adverse affects. */
struct rtllib_rx_stats {
#if 1
	u32 mac_time[2];
	s8 rssi;
	u8 signal;
	u8 noise;
	u16 rate; /* in 100 kbps */
	u8 received_channel;
	u8 control;
	u8 mask;
	u8 freq;
	u16 len;
	u64 tsf;
	u32 beacon_time;
	u8 nic_type;
	u16       Length;
	//      u8        DataRate;      // In 0.5 Mbps
	u8        SignalQuality; // in 0-100 index. 
	s32       RecvSignalPower; // Real power in dBm for this packet, no beautification and aggregation.
	s8        RxPower; // in dBm Translate from PWdB
	u8        SignalStrength; // in 0-100 index.
	u16       bHwError:1;
	u16       bCRC:1;
	u16       bICV:1;
	u16       bShortPreamble:1;
	u16       Antenna:1;      //for rtl8185
	u16       Decrypted:1;    //for rtl8185, rtl8187
	u16       Wakeup:1;       //for rtl8185
	u16       Reserved0:1;    //for rtl8185
	u8        AGC;
	u32       TimeStampLow;
	u32       TimeStampHigh;
	bool      bShift;
	bool      bIsQosData;             // Added by Annie, 2005-12-22.
	u8        UserPriority;

	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//1Attention Please!!!<11n or 8190 specific code should be put below this line>
	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!

	u8        RxDrvInfoSize;
	u8        RxBufShift;
	bool      bIsAMPDU;
	bool      bFirstMPDU;
	bool      bContainHTC;
	bool      RxIs40MHzPacket;
	u32       RxPWDBAll;
	u8        RxMIMOSignalStrength[4];        // in 0~100 index
	s8        RxMIMOSignalQuality[2];
	bool      bPacketMatchBSSID;
	bool      bIsCCK;
	bool      bPacketToSelf;
	//added by amy
	u8*       virtual_address;
	u16          packetlength;              // Total packet length: Must equal to sum of all FragLength
	u16          fraglength;                        // FragLength should equal to PacketLength in non-fragment case
	u16          fragoffset;                        // Data offset for this fragment
	u16          ntotalfrag;
	bool      	  bisrxaggrsubframe;
	bool		  bPacketBeacon;	//cosa add for rssi
	bool		  bToSelfBA;		//cosa add for rssi
	char 	  cck_adc_pwdb[4];	//cosa add for rx path selection
	u16		  Seq_Num;
	u8		nTotalAggPkt;		// Number of aggregated packets.
#ifdef TCP_CSUM_OFFLOAD_RX
	u8	tcp_csum_valid;
#endif
#endif

};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define RTLLIB_FRAG_CACHE_LEN 4

struct rtllib_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct rtllib_stats {
	unsigned int tx_unicast_frames;
	unsigned int tx_multicast_frames;
	unsigned int tx_fragments;
	unsigned int tx_unicast_octets;
	unsigned int tx_multicast_octets;
	unsigned int tx_deferred_transmissions;
	unsigned int tx_single_retry_frames;
	unsigned int tx_multiple_retry_frames;
	unsigned int tx_retry_limit_exceeded;
	unsigned int tx_discards;
	unsigned int rx_unicast_frames;
	unsigned int rx_multicast_frames;
	unsigned int rx_fragments;
	unsigned int rx_unicast_octets;
	unsigned int rx_multicast_octets;
	unsigned int rx_fcs_errors;
	unsigned int rx_discards_no_buffer;
	unsigned int tx_discards_wrong_sa;
	unsigned int rx_discards_undecryptable;
	unsigned int rx_message_in_msg_fragments;
	unsigned int rx_message_in_bad_msg_fragments;
};

struct rtllib_device;

#include "rtllib_crypt.h"

#define SEC_KEY_1         (1<<0)
#define SEC_KEY_2         (1<<1)
#define SEC_KEY_3         (1<<2)
#define SEC_KEY_4         (1<<3)
#define SEC_ACTIVE_KEY    (1<<4)
#define SEC_AUTH_MODE     (1<<5)
#define SEC_UNICAST_GROUP (1<<6)
#define SEC_LEVEL         (1<<7)
#define SEC_ENABLED       (1<<8)
#define SEC_ENCRYPT       (1<<9)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define SEC_ALG_NONE            0
#define SEC_ALG_WEP             1
#define SEC_ALG_TKIP            2
#define SEC_ALG_CCMP            4

#define WEP_KEYS 		4
#define WEP_KEY_LEN		13
#define SCM_KEY_LEN             32
#define SCM_TEMPORAL_KEY_LENGTH 16

struct rtllib_security {
	u16 active_key:2,
            enabled:1,
	    auth_mode:2,
            auth_algo:4,
            unicast_uses_group:1,
	    encrypt:1;
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][SCM_KEY_LEN];
	u8 level;
	u16 flags;
} __attribute__ ((packed));


/*
 802.11 data frame from AP
      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `-------------------------------------------------------------------'
Total: 28-2340 bytes
*/

/* Management Frame Information Element Types */
enum rtllib_mfie {
        MFIE_TYPE_SSID = 0,
        MFIE_TYPE_RATES = 1,
        MFIE_TYPE_FH_SET = 2,
        MFIE_TYPE_DS_SET = 3,
        MFIE_TYPE_CF_SET = 4,
        MFIE_TYPE_TIM = 5,
        MFIE_TYPE_IBSS_SET = 6,
        MFIE_TYPE_COUNTRY = 7,
        MFIE_TYPE_HOP_PARAMS = 8,
        MFIE_TYPE_HOP_TABLE = 9,
        MFIE_TYPE_REQUEST = 10,
        MFIE_TYPE_CHALLENGE = 16,
        MFIE_TYPE_POWER_CONSTRAINT = 32,
        MFIE_TYPE_POWER_CAPABILITY = 33,
        MFIE_TYPE_TPC_REQUEST = 34,
        MFIE_TYPE_TPC_REPORT = 35,
        MFIE_TYPE_SUPP_CHANNELS = 36,
        MFIE_TYPE_CSA = 37,
        MFIE_TYPE_MEASURE_REQUEST = 38,
        MFIE_TYPE_MEASURE_REPORT = 39,
        MFIE_TYPE_QUIET = 40,
        MFIE_TYPE_IBSS_DFS = 41,
        MFIE_TYPE_ERP = 42,
	MFIE_TYPE_HT_CAP= 45,
	MFIE_TYPE_RSN = 48,
	MFIE_TYPE_RATES_EX = 50,
#ifdef _RTL8192_EXT_PATCH_
	MFIE_TYPE_MESH_PANN = 51,
	MFIE_TYPE_MESH_RANN = 52,
	MFIE_TYPE_MESH_PATH_REQ = 53,
	MFIE_TYPE_MESH_PATH_REP = 54,
	MFIE_TYPE_MESH_PATH_ERR = 55,
#endif
	MFIE_TYPE_HT_INFO= 61,
	MFIE_TYPE_AIRONET=133,
#ifdef _RTL8192_EXT_PATCH_
	MFIE_TYPE_MESH_CONFIGURATION = 136,   	///<802.11s> 1.07
	MFIE_TYPE_MESH_ID = 137,     ///< 802.11s>1.07 MESH ID infoemation element
	MFIE_TYPE_MESH_LOCAL_LINK_STATE_ANNOU = 138,   ///<802.11s>1.07 
	MFIE_TYPE_MESH_ABBR_HANDSHAKE_IE_	=139,  ///<802.11s>3.0
	MFIE_TYPE_MESH_PEER_LINK_MGNT = 140,   ///< 802.11s>1.07
	MFIE_TYPE_MESH_NEIGHBOR_LIST = 142,     ///< 802.11s >1.07neighbor list IE
	MFIE_TYPE_MESH_TIM = 143,     ///< 802.11s>1.07MESH DTIM IE
	MFIE_TYPE_MESH_ATIM_WINDOW = 144,     ///<802.11s>1.07
	MFIE_TYPE_MESH_BEACON_TIMING = 145,     ///< 802.11s>1.07
	MFIE_TYPE_MESH_MDAOP_ADVERTISMENTS = 148,   ///< 802.11s>1.07
	MFIE_TYPE_MESH_MSC = 158,   ///<802.11s>1.07
	MFIE_TYPE_MESH_MSA = 159,   ///<802.11s>1.07
	MFIE_TYPE_MESH_MOAOP_SET_TEARDOWN = 212,   ///< 802.11s>1.07 
#endif
	MFIE_TYPE_GENERIC = 221,
        MFIE_TYPE_QOS_PARAMETER = 222,
};

/* Minimal header; can be used for passing 802.11 frames with sufficient
 * information to determine what type of underlying data type is actually
 * stored in the data. */
struct rtllib_pspoll_hdr {
        __le16 frame_ctl;
        __le16 aid;
	u8 bssid[ETH_ALEN];
        u8 ta[ETH_ALEN];
        //u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr {
        __le16 frame_ctl;
        __le16 duration_id;
        u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr_1addr {
        __le16 frame_ctl;
        __le16 duration_id;
        u8 addr1[ETH_ALEN];
        u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr_2addr {
        __le16 frame_ctl;
        __le16 duration_id;
        u8 addr1[ETH_ALEN];
        u8 addr2[ETH_ALEN];
        u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
        u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr_4addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
        u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr_3addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	__le16 qos_ctl;
	u8 payload[0];
} __attribute__ ((packed));

struct rtllib_hdr_4addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	__le16 qos_ctl;
	u8 payload[0];
} __attribute__ ((packed));

struct rtllib_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __attribute__ ((packed));

#ifdef _RTL8192_EXT_PATCH_

struct mesh_peering_protocol_version_t{
    u8 OUI[3]; // defined by owen:  {000FAC};	???
    u8 value;
} __attribute__ ((packed));
struct rtllib_plink_open {
	struct rtllib_hdr_3addr header;
	u8       category;
	u8       action;
	struct mesh_peering_protocol_version_t mesh_peering_protocol_version;
	__le16 capability;
	/* SSID, supported rates, RSN */
        struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_plink_confirm {
	struct rtllib_hdr_3addr header;
	u8       category;
	u8       action;
	struct mesh_peering_protocol_version_t mesh_peering_protocol_version;
	__le16 capability;
	__le16 aid;
	/* SSID, supported rates, RSN */
        struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_plink_close {
	struct rtllib_hdr_3addr header;
	u8       category;
	u8       action;
	struct mesh_peering_protocol_version_t mesh_peering_protocol_version;
	/* SSID, supported rates, RSN */
	struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_linkmetric_report {
	struct rtllib_hdr_3addr header;
	u8       category;
	u8       action;
	struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_linkmetric_req {
	struct rtllib_hdr_3addr header;
	u8       category;
	u8       action;
} __attribute__ ((packed));
#endif

struct rtllib_authentication {
	struct rtllib_hdr_3addr header;
	__le16 algorithm;
	__le16 transaction;
	__le16 status;
	/*challenge*/
	struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_disauth {
        struct rtllib_hdr_3addr header;
        __le16 reason;
} __attribute__ ((packed));

struct rtllib_disassoc {
        struct rtllib_hdr_3addr header;
        __le16 reason;
} __attribute__ ((packed));

struct rtllib_probe_request {
	struct rtllib_hdr_3addr header;
	/* SSID, supported rates */
        struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_probe_response {
	struct rtllib_hdr_3addr header;
	u32 time_stamp[2];
	__le16 beacon_interval;
	__le16 capability;
        /* SSID, supported rates, FH params, DS params,
         * CF params, IBSS params, TIM (if beacon), RSN */
        struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

/* Alias beacon for probe_response */
#define rtllib_beacon rtllib_probe_response

struct rtllib_assoc_request_frame {
	struct rtllib_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	/* SSID, supported rates, RSN */
        struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_reassoc_request_frame {
	struct rtllib_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	u8 current_ap[ETH_ALEN];
	/* SSID, supported rates, RSN */
        struct rtllib_info_element info_element[0];
} __attribute__ ((packed));

struct rtllib_assoc_response_frame {
	struct rtllib_hdr_3addr header;
	__le16 capability;
	__le16 status;
	__le16 aid;
	struct rtllib_info_element info_element[0]; /* supported rates */
} __attribute__ ((packed));

struct rtllib_txb {
	u8 nr_frags;
	u8 encrypted;
	u8 queue_index;
	u8 rts_included;
	u16 reserved;
	__le16 frag_size;
	__le16 payload_size;
	struct sk_buff *fragments[0];
};

#define MAX_TX_AGG_COUNT		  16
struct rtllib_drv_agg_txb {
	u8 nr_drv_agg_frames;
	struct sk_buff *tx_agg_frames[MAX_TX_AGG_COUNT];
}__attribute__((packed));

#define MAX_SUBFRAME_COUNT 		  64
struct rtllib_rxb {
	u8 nr_subframes;
	struct sk_buff *subframes[MAX_SUBFRAME_COUNT];
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
#ifdef TCP_CSUM_OFFLOAD_RX
	u8 tcp_csum_valid;
#endif

}__attribute__((packed));

typedef union _frameqos {
	u16 shortdata;
	u8  chardata[2];
	struct {
		u16 tid:4;
		u16 eosp:1;
		u16 ack_policy:2;
		u16 reserved:1;
		u16 txop:8;
	}field;
}frameqos,*pframeqos;

/* SWEEP TABLE ENTRIES NUMBER*/
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH                  ((u8)12)
#define MAX_RATES_EX_LENGTH               ((u8)16)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#define MAX_NETWORK_COUNT                  16 // Fix "Failed to read scan data : Argument list too long" issue in kernel 2.4.20
#else
//#define MAX_NETWORK_COUNT                  128 // WARNING: "__you_cannot_kmalloc_that_much" undefined!
#define MAX_NETWORK_COUNT                  96 // Fix "__you_cannot_kmalloc_that_much" issue in kernel 2.6.18
#endif			

#define MAX_CHANNEL_NUMBER                 161
#define RTLLIB_SOFTMAC_SCAN_TIME	   100
//(HZ / 2)
#define RTLLIB_SOFTMAC_ASSOC_RETRY_TIME (HZ * 2)

#define CRC_LENGTH                 4U

#define MAX_WPA_IE_LEN 64
#define MAX_WZC_IE_LEN 256

#define NETWORK_EMPTY_ESSID (1<<0)
#define NETWORK_HAS_OFDM    (1<<1)
#define NETWORK_HAS_CCK     (1<<2)

/* QoS structure */
#define NETWORK_HAS_QOS_PARAMETERS      (1<<3)
#define NETWORK_HAS_QOS_INFORMATION     (1<<4)
#define NETWORK_HAS_QOS_MASK            (NETWORK_HAS_QOS_PARAMETERS | \
                                         NETWORK_HAS_QOS_INFORMATION)
/* 802.11h */
#define NETWORK_HAS_POWER_CONSTRAINT    (1<<5)
#define NETWORK_HAS_CSA                 (1<<6)
#define NETWORK_HAS_QUIET               (1<<7)
#define NETWORK_HAS_IBSS_DFS            (1<<8)
#define NETWORK_HAS_TPC_REPORT          (1<<9)

#define NETWORK_HAS_ERP_VALUE           (1<<10)

#define QOS_QUEUE_NUM                   4
#define QOS_OUI_LEN                     3
#define QOS_OUI_TYPE                    2
#define QOS_ELEMENT_ID                  221
#define QOS_OUI_INFO_SUB_TYPE           0
#define QOS_OUI_PARAM_SUB_TYPE          1
#define QOS_VERSION_1                   1
#define QOS_AIFSN_MIN_VALUE             2
#if 1
struct rtllib_qos_information_element {
        u8 elementID;
        u8 length;
        u8 qui[QOS_OUI_LEN];
        u8 qui_type;
        u8 qui_subtype;
        u8 version;
        u8 ac_info;
} __attribute__ ((packed));

struct rtllib_qos_ac_parameter {
        u8 aci_aifsn;
        u8 ecw_min_max;
        __le16 tx_op_limit;
} __attribute__ ((packed));

struct rtllib_qos_parameter_info {
        struct rtllib_qos_information_element info_element;
        u8 reserved;
        struct rtllib_qos_ac_parameter ac_params_record[QOS_QUEUE_NUM];
} __attribute__ ((packed));

struct rtllib_qos_parameters {
        __le16 cw_min[QOS_QUEUE_NUM];
        __le16 cw_max[QOS_QUEUE_NUM];
        u8 aifs[QOS_QUEUE_NUM];
        u8 flag[QOS_QUEUE_NUM];
        __le16 tx_op_limit[QOS_QUEUE_NUM];
} __attribute__ ((packed));

struct rtllib_qos_data {
        struct rtllib_qos_parameters parameters;
	unsigned int wmm_acm;
        int active;
        int supported;
        u8 param_count;
        u8 old_param_count;
};

struct rtllib_tim_parameters {
        u8 tim_count;
        u8 tim_period;
} __attribute__ ((packed));

//#else
struct rtllib_wmm_ac_param {
	u8 ac_aci_acm_aifsn;
	u8 ac_ecwmin_ecwmax;
	u16 ac_txop_limit;
};

struct rtllib_wmm_ts_info {
	u8 ac_dir_tid;
	u8 ac_up_psb;
	u8 reserved;
} __attribute__ ((packed));

struct rtllib_wmm_tspec_elem {
	struct rtllib_wmm_ts_info ts_info;
	u16 norm_msdu_size;
	u16 max_msdu_size;
	u32 min_serv_inter;
	u32 max_serv_inter;
	u32 inact_inter;
	u32 suspen_inter;
	u32 serv_start_time;
	u32 min_data_rate;
	u32 mean_data_rate;
	u32 peak_data_rate;
	u32 max_burst_size;
	u32 delay_bound;
	u32 min_phy_rate;
	u16 surp_band_allow;
	u16 medium_time;
}__attribute__((packed));
#endif
enum eap_type {
	EAP_PACKET = 0,
	EAPOL_START,
	EAPOL_LOGOFF,
	EAPOL_KEY,
	EAPOL_ENCAP_ASF_ALERT
};

static const char *eap_types[] = {
	[EAP_PACKET]		= "EAP-Packet",
	[EAPOL_START]		= "EAPOL-Start",
	[EAPOL_LOGOFF]		= "EAPOL-Logoff",
	[EAPOL_KEY]		= "EAPOL-Key",
	[EAPOL_ENCAP_ASF_ALERT]	= "EAPOL-Encap-ASF-Alert"
};

static inline const char *eap_get_type(int type)
{
	return ((u32)type >= ARRAY_SIZE(eap_types)) ? "Unknown" : eap_types[type];
}
//added by amy for reorder
static inline u8 Frame_QoSTID(u8* buf)
{
	struct rtllib_hdr_3addr *hdr;
	u16 fc;
	hdr = (struct rtllib_hdr_3addr *)buf;
	fc = le16_to_cpu(hdr->frame_ctl);
	return (u8)((frameqos*)(buf + (((fc & RTLLIB_FCTL_TODS)&&(fc & RTLLIB_FCTL_FROMDS))? 30 : 24)))->field.tid;
}

//added by amy for reorder

struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 type;
	u16 length;
} __attribute__ ((packed));

struct rtllib_softmac_stats{
	unsigned int rx_ass_ok;
	unsigned int rx_ass_err;
	unsigned int rx_probe_rq;
	unsigned int tx_probe_rs;
	unsigned int tx_beacons;
	unsigned int rx_auth_rq;
	unsigned int rx_auth_rs_ok;
	unsigned int rx_auth_rs_err;
	unsigned int tx_auth_rq;
	unsigned int no_auth_rs;
	unsigned int no_ass_rs;
	unsigned int tx_ass_rq;
	unsigned int rx_ass_rq;
	unsigned int tx_probe_rq;
	unsigned int reassoc;
	unsigned int swtxstop;
	unsigned int swtxawake;
	unsigned char CurrentShowTxate;
	unsigned char last_packet_rate;
	unsigned int txretrycount;
};

#define BEACON_PROBE_SSID_ID_POSITION 12

struct rtllib_info_element_hdr {
	u8 id;
	u8 len;
} __attribute__ ((packed));

/*
 * These are the data types that can make up management packets
 *
	u16 auth_algorithm;
	u16 auth_sequence;
	u16 beacon_interval;
	u16 capability;
	u8 current_ap[ETH_ALEN];
	u16 listen_interval;
	struct {
		u16 association_id:14, reserved:2;
	} __attribute__ ((packed));
	u32 time_stamp[2];
	u16 reason;
	u16 status;
*/

#define RTLLIB_DEFAULT_TX_ESSID "Penguin"
#define RTLLIB_DEFAULT_BASIC_RATE 2 //1Mbps
#ifdef _RTL8192_EXT_PATCH_
#define RTLLIB_DEFAULT_MESHID "CMPC-mesh"
#define RTLLIB_DEFAULT_MESH_CHAN 7 
#endif

enum {WMM_all_frame, WMM_two_frame, WMM_four_frame, WMM_six_frame};
#define MAX_SP_Len  (WMM_all_frame << 4)
#define RTLLIB_QOS_TID 0x0f
#define QOS_CTL_NOTCONTAIN_ACK (0x01 << 5)
#ifdef ENABLE_AMSDU
#define QOS_CTL_AMSDU_PRESENT (0x01 << 7)
#endif

#define RTLLIB_DTIM_MBCAST 4
#define RTLLIB_DTIM_UCAST 2
#define RTLLIB_DTIM_VALID 1
#define RTLLIB_DTIM_INVALID 0

#define RTLLIB_PS_DISABLED 0
#define RTLLIB_PS_UNICAST RTLLIB_DTIM_UCAST
#define RTLLIB_PS_MBCAST RTLLIB_DTIM_MBCAST

//added by David for QoS 2006/6/30
//#define WMM_Hang_8187
#ifdef WMM_Hang_8187
#undef WMM_Hang_8187
#endif

#define WME_AC_BK   0x00
#define WME_AC_BE   0x01
#define WME_AC_VI   0x02
#define WME_AC_VO   0x03
#define WME_ACI_MASK 0x03
#define WME_AIFSN_MASK 0x03
#define WME_AC_PRAM_LEN 16

#define MAX_RECEIVE_BUFFER_SIZE 9100

//UP Mapping to AC, using in MgntQuery_SequenceNumber() and maybe for DSCP
//#define UP2AC(up)	((up<3) ? ((up==0)?1:0) : (up>>1)) 
#if 1
#define UP2AC(up) (		   \
	((up) < 1) ? WME_AC_BE : \
	((up) < 3) ? WME_AC_BK : \
	((up) < 4) ? WME_AC_BE : \
	((up) < 6) ? WME_AC_VI : \
	WME_AC_VO)	
#endif
//AC Mapping to UP, using in Tx part for selecting the corresponding TX queue
#define AC2UP(_ac)	(       \
	((_ac) == WME_AC_VO) ? 6 : \
	((_ac) == WME_AC_VI) ? 5 : \
	((_ac) == WME_AC_BK) ? 1 : \
	0)

#define	ETHER_ADDR_LEN		6	/* length of an Ethernet address */
#define ETHERNET_HEADER_SIZE    14      /* length of two Ethernet address plus ether type*/

struct	ether_header {
	u8 ether_dhost[ETHER_ADDR_LEN];
	u8 ether_shost[ETHER_ADDR_LEN];
	u16 ether_type;
} __attribute__((packed)); 

#ifndef ETHERTYPE_PAE
#define	ETHERTYPE_PAE	0x888e		/* EAPOL PAE/802.1x */
#endif
#ifndef ETHERTYPE_IP
#define	ETHERTYPE_IP	0x0800		/* IP protocol */
#endif


typedef enum _erp_t{
	ERP_NonERPpresent	= 0x01,
	ERP_UseProtection	= 0x02,
	ERP_BarkerPreambleMode = 0x04,
} erp_t;

#ifdef _RTL8192_EXT_PATCH_
typedef struct _mesh_config_t {
	u8 path_proto_id[4];
	u8 path_metric_id[4];
	u8 congest_ctl_mode[4];
	u8 mesh_capability[2];
} mesh_config_t;
#define MAX_MESH_ID_LEN 32
#define MESH_CONF_TOTAL_LEN 19
#endif

struct rtllib_network {
	/* These entries are used to identify a unique network */
	u8 bssid[ETH_ALEN];
	u8 channel;
	/* Ensure null-terminated for any debug msgs */
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;
	//YJ,add,090701
	u8 hidden_ssid[IW_ESSID_MAX_SIZE + 1];
	u8 hidden_ssid_len;
	struct rtllib_qos_data qos_data;

	//added by amy for LEAP
	bool	bWithAironetIE;
	bool	bCkipSupported;
	bool	bCcxRmEnable;
	u16 	CcxRmState[2];
	// CCXv4 S59, MBSSID.
	bool	bMBssidValid;
	u8	MBssidMask;
	u8	MBssid[6];
	// CCX 2 S38, WLAN Device Version Number element. Annie, 2006-08-20.
	bool	bWithCcxVerNum;
	u8	BssCcxVerNumber;
	/* These are network statistics */
	struct rtllib_rx_stats stats;
	u16 capability;
	u8  rates[MAX_RATES_LENGTH];
	u8  rates_len;
	u8  rates_ex[MAX_RATES_EX_LENGTH];
	u8  rates_ex_len;
	unsigned long last_scanned;
	u8  mode;
	u32 flags;
	u32 last_associate;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 listen_interval;
	u16 atim_window;
	u8  erp_value;
	u8  wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8  rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;
//rtllib_network too big, can't malloc sucssess in 2.4 kernel
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)	
	u8  wzc_ie[MAX_WZC_IE_LEN];
	size_t wzc_ie_len;
#endif

        struct rtllib_tim_parameters tim;
	u8  dtim_period;
	u8  dtim_data;
	u32 last_dtim_sta_time[2];

        //appeded for QoS
        u8 wmm_info;
        struct rtllib_wmm_ac_param wmm_param[4];
        u8 QoS_Enable;
#ifdef THOMAS_TURBO
	u8 Turbo_Enable;//enable turbo mode, added by thomas
#endif
#ifdef ENABLE_DOT11D
	u16 CountryIeLen;
	u8 CountryIeBuf[MAX_IE_LEN];
#endif
        // HT Related, by amy, 2008.04.29
	BSS_HT	bssht;
	// Add to handle broadcom AP management frame CCK rate.
	bool broadcom_cap_exist;
	bool realtek_cap_exit;
	bool marvell_cap_exist;
	bool ralink_cap_exist;
	bool atheros_cap_exist;
	bool cisco_cap_exist;
	bool unknown_cap_exist;
//	u8	berp_info;
	bool	berp_info_valid;
	bool buseprotection;
	// Dirty Fix 1x1 IOT
	bool bIsNetgear854T;
	u8 SignalStrength;
	u8 RSSI; // 0-100.
	//put at the end of the structure.
#ifdef _RTL8192_EXT_PATCH_
	u8 hostname[MAX_HOST_NAME_LENGTH];
	u8 hostname_len;
	u8 mesh_id[MAX_MESH_ID_LEN];
	u8 mesh_id_len;
	mesh_config_t mesh_config;
	u8 mesh_config_len;
	void *ext_entry;
#endif

	struct list_head list;
};

#if 1
enum rtllib_state {

	/* the card is not linked at all */
	RTLLIB_NOLINK = 0,
	
	/* RTLLIB_ASSOCIATING* are for BSS client mode
	 * the driver shall not perform RX filtering unless
	 * the state is LINKED.
	 * The driver shall just check for the state LINKED and
	 * defaults to NOLINK for ALL the other states (including
	 * LINKED_SCANNING)
	 */
	
	/* the association procedure will start (wq scheduling)*/
	RTLLIB_ASSOCIATING,
	RTLLIB_ASSOCIATING_RETRY,
	
	/* the association procedure is sending AUTH request*/
	RTLLIB_ASSOCIATING_AUTHENTICATING,
	
	/* the association procedure has successfully authentcated
	 * and is sending association request
	 */
	RTLLIB_ASSOCIATING_AUTHENTICATED,
	
	/* the link is ok. the card associated to a BSS or linked
	 * to a ibss cell or acting as an AP and creating the bss
	 */
	RTLLIB_LINKED,
	
	/* same as LINKED, but the driver shall apply RX filter
	 * rules as we are in NO_LINK mode. As the card is still
	 * logically linked, but it is doing a syncro site survey
	 * then it will be back to LINKED state.
	 */
	RTLLIB_LINKED_SCANNING,
#ifdef _RTL8192_EXT_PATCH_
	RTLLIB_MESH_SCANNING,
	RTLLIB_MESH_LINKED,
#endif
};
#else
enum rtllib_state {
        RTLLIB_UNINITIALIZED = 0,
        RTLLIB_INITIALIZED,
        RTLLIB_ASSOCIATING,
        RTLLIB_ASSOCIATED,
        RTLLIB_AUTHENTICATING,
        RTLLIB_AUTHENTICATED,
        RTLLIB_SHUTDOWN
};
#endif

#define DEFAULT_MAX_SCAN_AGE (15 * HZ)
#define DEFAULT_FTS 2346

#define CFG_RTLLIB_RESERVE_FCS (1<<0)
#define CFG_RTLLIB_COMPUTE_FCS (1<<1)
#define CFG_RTLLIB_RTS (1<<2)

#define RTLLIB_24GHZ_MIN_CHANNEL 1
#define RTLLIB_24GHZ_MAX_CHANNEL 14
#define RTLLIB_24GHZ_CHANNELS (RTLLIB_24GHZ_MAX_CHANNEL - \
                                  RTLLIB_24GHZ_MIN_CHANNEL + 1)

#define RTLLIB_52GHZ_MIN_CHANNEL 34
#define RTLLIB_52GHZ_MAX_CHANNEL 165
#define RTLLIB_52GHZ_CHANNELS (RTLLIB_52GHZ_MAX_CHANNEL - \
                                  RTLLIB_52GHZ_MIN_CHANNEL + 1)

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,11))
extern inline int is_multicast_ether_addr(const u8 *addr)
{
        return ((addr[0] != 0xff) && (0x01 & addr[0]));
}
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,13))
extern inline int is_broadcast_ether_addr(const u8 *addr)
{
	return ((addr[0] == 0xff) && (addr[1] == 0xff) && (addr[2] == 0xff) &&   \
		(addr[3] == 0xff) && (addr[4] == 0xff) && (addr[5] == 0xff));
}
#endif
#ifndef eqMacAddr
#define eqMacAddr(a,b)		( ((a)[0]==(b)[0] && (a)[1]==(b)[1] && (a)[2]==(b)[2] && (a)[3]==(b)[3] && (a)[4]==(b)[4] && (a)[5]==(b)[5]) ? 1:0 )
#endif
typedef struct tx_pending_t{
	int frag;
	struct rtllib_txb *txb;
}tx_pending_t;

typedef struct _bandwidth_autoswitch
{
	long threshold_20Mhzto40Mhz;
	long	threshold_40Mhzto20Mhz;	
	bool bforced_tx20Mhz;
	bool bautoswitch_enable;	
}bandwidth_autoswitch,*pbandwidth_autoswitch;


//added by amy for order

#define REORDER_WIN_SIZE	128
#define REORDER_ENTRY_NUM	128
typedef struct _RX_REORDER_ENTRY
{
	struct list_head	List;
	u16			SeqNum;
	struct rtllib_rxb* prxb;
} RX_REORDER_ENTRY, *PRX_REORDER_ENTRY;
//added by amy for order
typedef enum _Fsync_State{
	Default_Fsync,
	HW_Fsync,
	SW_Fsync
}Fsync_State;	

// Power save mode configured. 
typedef	enum _RT_PS_MODE	
{
	eActive,	// Active/Continuous access.
	eMaxPs,		// Max power save mode.
	eFastPs,		// Fast power save mode.
	eAutoPs,	// Auto power save mode.
}RT_PS_MODE;

typedef enum _IPS_CALLBACK_FUNCION
{
	IPS_CALLBACK_NONE = 0,
	IPS_CALLBACK_MGNT_LINK_REQUEST = 1,
	IPS_CALLBACK_JOIN_REQUEST = 2,
}IPS_CALLBACK_FUNCION;

typedef enum _RT_JOIN_ACTION{
	RT_JOIN_INFRA   = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
}RT_JOIN_ACTION;

typedef struct _IbssParms{
	u16   atimWin;
}IbssParms, *PIbssParms;
#define MAX_NUM_RATES	264 // Max num of support rates element: 8,  Max num of ext. support rate: 255. 061122, by rcnjko.

// RF state.
typedef	enum _RT_RF_POWER_STATE
{
	eRfOn,
	eRfSleep,
	eRfOff
}RT_RF_POWER_STATE;

typedef struct _RT_POWER_SAVE_CONTROL
{

	//
	// Inactive Power Save(IPS) : Disable RF when disconnected
	//
	bool				bInactivePs;
	bool				bIPSModeBackup;
	bool				bHaltAdapterClkRQ;
	bool				bSwRfProcessing;
	RT_RF_POWER_STATE	eInactivePowerState;
	work_struct_rsl 	InactivePsWorkItem;
	struct timer_list	InactivePsTimer;

	// Return point for join action
	IPS_CALLBACK_FUNCION	ReturnPoint;
	
	// Recored Parameters for rescheduled JoinRequest
	bool				bTmpBssDesc;
	RT_JOIN_ACTION		tmpJoinAction;
	struct rtllib_network tmpBssDesc;

	// Recored Parameters for rescheduled MgntLinkRequest
	bool				bTmpScanOnly;
	bool				bTmpActiveScan;
	bool				bTmpFilterHiddenAP;
	bool				bTmpUpdateParms;
	u8				tmpSsidBuf[33];
	OCTET_STRING			tmpSsid2Scan;
	bool				bTmpSsid2Scan;
	u8				tmpNetworkType;
	u8				tmpChannelNumber;
	u16				tmpBcnPeriod;
	u8				tmpDtimPeriod;
	u16				tmpmCap;
	OCTET_STRING			tmpSuppRateSet;
	u8				tmpSuppRateBuf[MAX_NUM_RATES];
	bool				bTmpSuppRate;
	IbssParms				tmpIbpm;
	bool				bTmpIbpm;

	//
	// Leisre Poswer Save : Disable RF if connected but traffic is not busy
	//		
	bool				bLeisurePs;
	u32				PowerProfile;
	u8				LpsIdleCount;
	u8				RegMaxLPSAwakeIntvl;
	u8				LPSAwakeIntvl;

	//RF OFF Level
	u32				CurPsLevel;
	u32				RegRfPsLevel;

	//Fw Control LPS
	bool				bFwCtrlLPS;
	u8				FWCtrlPSMode;

	//2009.01.01 added by tynli
	// Record if there is a link request in IPS RF off progress.
	bool				LinkReqInIPSRFOffPgs;
	// To make sure that connect info should be executed, so we set the bit to filter the link info which comes after the connect info.
	bool				BufConnectinfoBefore;
	
}RT_POWER_SAVE_CONTROL,*PRT_POWER_SAVE_CONTROL;

typedef u32 RT_RF_CHANGE_SOURCE;
#define RF_CHANGE_BY_SW BIT31
#define RF_CHANGE_BY_HW BIT30
#define RF_CHANGE_BY_PS BIT29
#define RF_CHANGE_BY_IPS BIT28
#define RF_CHANGE_BY_INIT	0	// Do not change the RFOff reason. Defined by Bruce, 2008-01-17.

#ifdef ENABLE_DOT11D
typedef enum 
{
	COUNTRY_CODE_FCC = 0,
	COUNTRY_CODE_IC = 1,
	COUNTRY_CODE_ETSI = 2,
	COUNTRY_CODE_SPAIN = 3,
	COUNTRY_CODE_FRANCE = 4,
	COUNTRY_CODE_MKK = 5,
	COUNTRY_CODE_MKK1 = 6,
	COUNTRY_CODE_ISRAEL = 7,
	COUNTRY_CODE_TELEC = 8,
	COUNTRY_CODE_MIC = 9,
	COUNTRY_CODE_GLOBAL_DOMAIN = 10,
	COUNTRY_CODE_WORLD_WIDE_13 = 11,
	COUNTRY_CODE_TELEC_NETGEAR = 12,
	// add new channel plan above this line
	COUNTRY_CODE_MAX
}country_code_type_t;	
#endif

typedef enum _SCAN_OPERATION_BACKUP_OPT{
	SCAN_OPT_BACKUP=0,
	SCAN_OPT_RESTORE,
	SCAN_OPT_MAX
}SCAN_OPERATION_BACKUP_OPT;

//
// Firmware realted CMD IO. Added by Roger. 2008.12.03.
//
typedef	enum _FW_CMD_IO_TYPE{
	FW_CMD_DIG_ENABLE = 0, // For DIG DM
	FW_CMD_DIG_DISABLE = 1,
	FW_CMD_DIG_HALT = 2,
	FW_CMD_DIG_RESUME = 3,
	FW_CMD_HIGH_PWR_ENABLE = 4, // For High Power DM
	FW_CMD_HIGH_PWR_DISABLE = 5,
	FW_CMD_RA_RESET = 6, // For Rate adaptive DM
	FW_CMD_RA_ACTIVE= 7,
	FW_CMD_RA_REFRESH_N= 8,
	FW_CMD_RA_REFRESH_BG= 9,
	FW_CMD_RA_INIT= 10,
	FW_CMD_IQK_ENABLE = 11, // For FW supported IQK
	FW_CMD_TXPWR_TRACK_ENABLE = 12,	// Tx power tracking switch, MP driver only
	FW_CMD_TXPWR_TRACK_DISABLE = 13,	// Tx power tracking switch, MP driver only
	FW_CMD_TXPWR_TRACK_THERMAL = 14, // Tx power tracking with thermal indication, for Normal driver
	FW_CMD_PAUSE_DM_BY_SCAN = 15,
	FW_CMD_RESUME_DM_BY_SCAN = 16,
	FW_CMD_RA_REFRESH_N_COMB = 17,
	FW_CMD_RA_REFRESH_BG_COMB = 18,
	FW_CMD_ANTENNA_SW_ENABLE = 19,
	FW_CMD_ANTENNA_SW_DISABLE = 20,
	FW_CMD_TX_FEEDBACK_CCX_ENABLE = 21, // Tx Status report for CCX from FW
	FW_CMD_LPS_ENTER = 22,			// Indifate firmware that driver enters LPS, For PS-Poll hardware bug
	FW_CMD_LPS_LEAVE = 23,			// Indicate firmware that driver leave LPS, 2009/1/4, by Emily
	FW_CMD_DIG_MODE_SS = 24, 			// Set DIG mode to signal strength
	FW_CMD_DIG_MODE_FA = 25,			// Set DIG mode to false alarm.
	FW_CMD_ADD_A2_ENTRY = 26,
	FW_CMD_CTRL_DM_BY_DRIVER = 27,
	FW_CMD_CTRL_DM_BY_DRIVER_NEW = 28,
	FW_CMD_PAPE_CONTROL = 29,
}FW_CMD_IO_TYPE,*PFW_CMD_IO_TYPE;

#define RT_MAX_LD_SLOT_NUM	10
typedef struct _RT_LINK_DETECT_T{

	u32				NumRecvBcnInPeriod;
	u32				NumRecvDataInPeriod;

	u32				RxBcnNum[RT_MAX_LD_SLOT_NUM];	// number of Rx beacon / CheckForHang_period  to determine link status
	u32				RxDataNum[RT_MAX_LD_SLOT_NUM];	// number of Rx data / CheckForHang_period  to determine link status
	u16				SlotNum;	// number of CheckForHang period to determine link status
	u16				SlotIndex;

	u32				NumTxOkInPeriod;
	u32				NumRxOkInPeriod;
	u32				NumRxUnicastOkInPeriod;
	bool				bBusyTraffic;

#ifdef _RTL8192_EXT_PATCH_
	//
	// For keep-alive mechanism. 
	//
	u8				IdleCount;
	u32				LastNumTxUnicast;
	u32				LastNumRxUnicast;
#endif	
}RT_LINK_DETECT_T, *PRT_LINK_DETECT_T;

#ifdef _RTL8192_EXT_PATCH_
struct rtllib_crypt_data_list{
	u8 used;
	u8 mac_addr[ETH_ALEN];  //record mac_add
	struct rtllib_crypt_data *crypt[WEP_KEYS];
}__attribute__((packed));

typedef struct _SW_RATR_TABLE{
	bool bused;
	u32  ratr_value;
}SW_RATR_TABLE, *PSW_RATR_TABLE;
#endif

typedef struct _SW_CAM_TABLE{

	u8				macaddr[6];
	bool				bused;
	u8				key_buf[16];
	u16 				key_type;
	u8				useDK;
	u8				key_index;				// CAM entry in CAM
	
}SW_CAM_TABLE,*PSW_CAM_TABLE;
#define   TOTAL_CAM_ENTRY				32
//added by amy 090402 for adhoc
typedef struct _rate_adaptive
{
	u8				rate_adaptive_disabled;
	u8				ratr_state;
	u16				reserve;	
	
	u32				high_rssi_thresh_for_ra;
	u32				high2low_rssi_thresh_for_ra;
	u8				low2high_rssi_thresh_for_ra40M;
	u32				low_rssi_thresh_for_ra40M;
	u8				low2high_rssi_thresh_for_ra20M;
	u32				low_rssi_thresh_for_ra20M;
	u32				upper_rssi_threshold_ratr;
	u32				middle_rssi_threshold_ratr;
	u32				low_rssi_threshold_ratr;
	u32				low_rssi_threshold_ratr_40M;
	u32				low_rssi_threshold_ratr_20M;
	u8				ping_rssi_enable;	//cosa add for test
	u32				ping_rssi_ratr;	//cosa add for test
	u32				ping_rssi_thresh_for_ra;//cosa add for test
	u32				last_ratr;
	u8				PreRATRState;
	
} rate_adaptive, *prate_adaptive;
#if defined(RTL8192U) || defined(RTL8192SU) || defined(RTL8192SE) || defined RTL8192CE
#define PEER_MAX_ASSOC 10
struct sta_info {
	u8 macaddr[ETH_ALEN];
	u16 aid;
	u8 authentication;
	u8 encryption;
	u16 capability;
	u8 ratr_index;
	int wireless_mode;
	RT_HTINFO_STA_ENTRY htinfo;
	u8 wme_enable;
	u32 CurDataRate;
	rate_adaptive     rate_adaptive;
	u8 bPowerSave;		// true if the STA is in power save mode, false is for active one.
	struct sk_buff_head PsQueue;		// Packet queued for this STA if it is in power save mode.
	u32	LastActiveTime; // Last active time in ms. 
	//Isaiah for data-adaptation 2006-05-05
	u16			StaDataRate;  // sta transmit rate 
	u32			StaSS; // recv Data's SignalStregth
	u16			RetryFrameCnt;
	u16			LastRetryCnt;
	u16			NoRetryFrameCnt;
	u16			LastNoRetryCnt;
	int			AvgRetryRate;
	int			LastRetryRate;
	u8			txRateIndex;
	u16			APDataRate;
	u16			ForcedDataRate;	
} __attribute__ ((packed));
#endif
//added by amy 090402 for adhoc end
#ifdef _RTL8192_EXT_PATCH_
//MP HT ralated//
typedef struct _RT_PEERMP_HTINFO{
	u8				bEnableHT;
	u8				bCurrentHTSupport;

	u8				bRegBW40MHz;				// Tx 40MHz channel capablity
	u8				bCurBW40MHz;				// Tx 40MHz channel capability

	u8				bRegShortGI40MHz;			// Tx Short GI for 40Mhz
	u8				bCurShortGI40MHz;			// Tx Short GI for 40MHz

	u8				bRegShortGI20MHz;			// Tx Short GI for 20MHz
	u8				bCurShortGI20MHz;			// Tx Short GI for 20MHz

	u8				bCurSuppCCK;				// Tx CCK rate capability

	// 802.11n spec version for "peer"
	//HT_SPEC_VER			ePeerHTSpecVer;
	

	// HT related information for "Self"
	//HT_CAPABILITY_ELE	SelfHTCap;		// This is HT cap element sent to peer STA, which also indicate HT Rx capabilities.
	//HT_INFORMATION_ELE	SelfHTInfo;		// This is HT info element sent to peer STA, which also indicate HT Rx capabilities.

	// HT related information for "Peer"
	u8				PeerHTCapBuf[32];
	u8				PeerHTInfoBuf[32];


	// A-MSDU related
	u8				bAMSDU_Support;			// This indicates Tx A-MSDU capability
	u16				nAMSDU_MaxSize;			// This indicates Tx A-MSDU capability
	u8				bCurrent_Mesh_AMSDU_Support;	// This indicates Tx A-MSDU capability
	

	// AMPDU  related <2006.08.10 Emily>
	u8				bAMPDUEnable;				// This indicate Tx A-MPDU capability
	u8				bCurrentMeshAMPDUEnable;		// This indicate Tx A-MPDU capability		
	u8				AMPDU_Factor;				// This indicate Tx A-MPDU capability
	u8				CurrentAMPDUFactor;		// This indicate Tx A-MPDU capability
	u8				MPDU_Density;				// This indicate Tx A-MPDU capability
	u8				CurrentMPDUDensity;			// This indicate Tx A-MPDU capability

	// Forced A-MPDU enable
	//HT_AGGRE_MODE_E	ForcedAMPDUMode;
	u8				ForcedAMPDUFactor;
	u8				ForcedMPDUDensity;

	// Forced A-MSDU enable
	HT_AGGRE_MODE_E	ForcedAMSDUMode;
	u16				ForcedAMSDUMaxSize;

	u8				bForcedShortGI;

	u8				CurrentOpMode;

	// MIMO PS related
	u8				MimoPs;
	
	// 40MHz Channel Offset settings.
	HT_EXTCHNL_OFFSET	CurSTAExtChnlOffset;
	u8				bCurTxBW40MHz;	// If we use 40 MHz to Tx
	u8				PeerBandwidth;

	// For Bandwidth Switching
	u8				bSwBwInProgress;
	CHNLOP				ChnlOp; // software switching channel in progress. By Bruce, 2008-02-15.
	u8				SwBwStep;
	//struct timer_list		SwBwTimer;  //moved to rtllib_device. as timer_list need include some header file here.

	// For Realtek proprietary A-MPDU factor for aggregation
	u8				bRegRT2RTAggregation;
	u8				bCurrentRT2RTAggregation;
	u8				bCurrentRT2RTLongSlotTime;
	u8				szRT2RTAggBuffer[10];

	// Rx Reorder control
	u8				bRegRxReorderEnable;
	u8				bCurRxReorderEnable;
	u8				RxReorderWinSize;
	u8				RxReorderPendingTime;
	u16				RxReorderDropCounter;

#ifdef USB_TX_DRIVER_AGGREGATION_ENABLE
	u8				UsbTxAggrNum;
#endif
#ifdef USB_RX_AGGREGATION_SUPPORT
	u8				UsbRxFwAggrEn;
	u8				UsbRxFwAggrPageNum;
	u8				UsbRxFwAggrPacketNum;
	u8				UsbRxFwAggrTimeout;
#endif

	// Add for Broadcom(Linksys) IOT. Joseph
	u8				bIsPeerBcm;

	u8				McsRateSet[16];
	u8				HTHighestOperaRate;
	// For IOT issue.
	u8				IOTPeer;
	u32				IOTAction;
} __attribute__ ((packed)) peerMP_htinfo, *ppeerMP_htinfo;
#endif

#define	NUM_PMKID_CACHE		16

typedef struct _RT_PMKID_LIST
{
	u8						bUsed;
	u8 						Bssid[6];
	u8						PMKID[16];
	u8						SsidBuf[33];
	u8*						ssid_octet;
	u16 					ssid_length;
} RT_PMKID_LIST, *PRT_PMKID_LIST;

/*************** DRIVER STATUS   *****/
#define STATUS_SCANNING			0
#define STATUS_SCAN_HW			1
#define STATUS_SCAN_ABORTING	2
#define STATUS_SETTING_CHAN		3
/*************** DRIVER STATUS   *****/

struct rtllib_device {
	struct net_device *dev;
        struct rtllib_security sec;

	unsigned long status;

	u8 VersionID;
	/* The last AssocReq/Resp IEs */
	u8 *assocreq_ies, *assocresp_ies;
	size_t assocreq_ies_len, assocresp_ies_len;
	
	bool b_customer_lenovo_id;
	bool	bForcedShowRxRate;
	bool	bForcedShowRateStill;
	u8	SystemQueryDataRateCount;
	bool	bForcedBgMode;
//#ifdef _RTL8192_EXT_PATCH_
	bool bUseRAMask;//added by amy 090709 for mesh rate adaptive
//#endif
	//for 92se 1x1 IOT issue
	bool b1x1RecvCombine;
	u8 RF_Type; //0 means 1T2R, 1 means 2T4R
	bool b1SSSupport;
	//for 92se 1x1 IOT issue
	
	//hw security related
//	u8 hwsec_support; //support?
	u8 hwsec_active;  //hw security active.
	bool is_silent_reset;
      bool force_mic_error;
	bool is_roaming;
	bool ieee_up;	
	//added by amy
	bool bSupportRemoteWakeUp;
	RT_PS_MODE	dot11PowerSaveMode; // Power save mode configured.
	bool actscanning;
	//added by amy 090313
	bool be_scan_inprogress;
	bool beinretry;
	RT_RF_POWER_STATE		eRFPowerState;
	RT_RF_CHANGE_SOURCE	RfOffReason;
	bool is_set_key;
	bool wx_set_enc; //for wep cat't get IP issue when use UI.
	//11n spec related I wonder if These info structure need to be moved out of rtllib_device
#if defined(RTL8192U) || defined(RTL8192SU) || defined(RTL8192SE)
	struct sta_info *peer_assoc_list[PEER_MAX_ASSOC];//added by amy for adhoc 090402
	//added by thomas for adhoc 090805
	atomic_t	AsocEntryNum;
	u64	CurrTsf;
	u64	TargetTsf;
	bool	Peer_bCurBW40M;
#endif
	//11n HT below
	PRT_HIGH_THROUGHPUT	pHTInfo;
	//struct timer_list		SwBwTimer;
//	spinlock_t chnlop_spinlock;
	spinlock_t bw_spinlock;

	spinlock_t reorder_spinlock;
	// for HT operation rate set.  we use this one for HT data rate to seperate different descriptors
	//the way fill this is the same as in the IE
	u8	Regdot11HTOperationalRateSet[16];		//use RATR format
	u8	Regdot11TxHTOperationalRateSet[16];		//use RATR format
	u8	dot11HTOperationalRateSet[16];		//use RATR format
	u8	RegHTSuppRateSet[16];
	u8				HTCurrentOperaRate;
	u8				HTHighestOperaRate;
	//added by amy 090330
	// For Min spacing configurations, Added by Roger, 2008.08.30.
       u8		MinSpaceCfg;
	u8		MaxMssDensity;
	//added by amy 090330 end
	//wb added for rate operation mode to firmware
	u8	bTxDisableRateFallBack;
	u8 	bTxUseDriverAssingedRate;
	u8	bTxEnableFwCalcDur;
	atomic_t	atm_chnlop;
	atomic_t	atm_swbw;
//	u8	HTHighestOperaRate;
//	u8 	HTCurrentOperaRate;

	// 802.11e and WMM Traffic Stream Info (TX)
	struct list_head		Tx_TS_Admit_List;
	struct list_head		Tx_TS_Pending_List;
	struct list_head		Tx_TS_Unused_List;
	TX_TS_RECORD		TxTsRecord[TOTAL_TS_NUM];
	// 802.11e and WMM Traffic Stream Info (RX)
	struct list_head		Rx_TS_Admit_List;
	struct list_head		Rx_TS_Pending_List;
	struct list_head		Rx_TS_Unused_List;
	RX_TS_RECORD		RxTsRecord[TOTAL_TS_NUM];
	RX_REORDER_ENTRY	RxReorderEntry[128];
	struct list_head		RxReorder_Unused_List;
	// Qos related. Added by Annie, 2005-11-01.
//	PSTA_QOS			pStaQos;
	u8				ForcedPriority;		// Force per-packet priority 1~7. (default: 0, not to force it.)
	

	/* Bookkeeping structures */
	struct net_device_stats stats;
	struct rtllib_stats ieee_stats;
	struct rtllib_softmac_stats softmac_stats;
	
	/* Probe / Beacon management */
	struct list_head network_free_list;
	struct list_head network_list;
	//added by amy for adhoc 090403
#ifdef RTL8192U
	struct list_head network_bssid_free_list;
	struct list_head network_bssid_list;
#endif
	//added by amy for adhoc 090403 end
	struct rtllib_network *networks;
	int scans;
	int scan_age;

	int iw_mode; /* operating mode (IW_MODE_*) */
	struct iw_spy_data spy_data; 

	spinlock_t lock;
	spinlock_t wpax_suitlist_lock;
	
	int tx_headroom; /* Set to size of any additional room needed at front
			  * of allocated Tx SKBs */
	u32 config;

	/* WEP and other encryption related settings at the device level */
	int open_wep; /* Set to 1 to allow unencrypted frames */
	int auth_mode;
	int reset_on_keychange; /* Set to 1 if the HW needs to be reset on
				 * WEP key changes */

	/* If the host performs {en,de}cryption, then set to 1 */
	int host_encrypt;
	int host_encrypt_msdu;
	int host_decrypt;
        /* host performs multicast decryption */
        int host_mc_decrypt;

        /* host should strip IV and ICV from protected frames */
        /* meaningful only when hardware decryption is being used */
        int host_strip_iv_icv;

        int host_open_frag;
        int host_build_iv;
	int ieee802_1x; /* is IEEE 802.1X used */

	/* WPA data */
	bool bHalfNMode;
	bool bHalfWirelessN24GMode;
	int wpa_enabled;
	int drop_unencrypted;
	int tkip_countermeasures;
	int privacy_invoked;
	size_t wpa_ie_len;
	u8 *wpa_ie;
	size_t wps_ie_len;//for wps
	u8 *wps_ie;
	u8 ap_mac_addr[6];
	u16 pairwise_key_type;
	u16 group_key_type;
	struct list_head crypt_deinit_list;
#ifdef _RTL8192_EXT_PATCH_
	struct rtllib_crypt_data_list* cryptlist[MAX_MP];
	struct rtllib_crypt_data *sta_crypt[WEP_KEYS];
#else
	struct rtllib_crypt_data *crypt[WEP_KEYS];
#endif

	int tx_keyidx; /* default TX key index (crypt[tx_keyidx]) */
	SW_CAM_TABLE swcamtable[TOTAL_CAM_ENTRY];//added by amy 090415
	struct timer_list crypt_deinit_timer;
        int crypt_quiesced;

	int bcrx_sta_key; /* use individual keys to override default keys even
			   * with RX of broad/multicast frames */
	
	RT_PMKID_LIST		PMKIDList[NUM_PMKID_CACHE];

	/* Fragmentation structures */
	// each streaming contain a entry
	struct rtllib_frag_entry frag_cache[17][RTLLIB_FRAG_CACHE_LEN];
	unsigned int frag_next_idx[17];
	u16 fts; /* Fragmentation Threshold */
#define DEFAULT_RTS_THRESHOLD 2346U
#define MIN_RTS_THRESHOLD 1
#define MAX_RTS_THRESHOLD 2346U
        u16 rts; /* RTS threshold */

        /* Association info */
        u8 bssid[ETH_ALEN];

	/* This stores infos for the current network.
	 * Either the network we are associated in INFRASTRUCTURE
	 * or the network that we are creating in MASTER mode.
	 * ad-hoc is a mixture ;-).
	 * Note that in infrastructure mode, even when not associated,
	 * fields bssid and essid may be valid (if wpa_set and essid_set
	 * are true) as thy carry the value set by the user via iwconfig  
	 */
	struct rtllib_network current_network;

	enum rtllib_state state;

	int short_slot;
	int reg_mode;
	int mode;       /* A, B, G */
	int modulation; /* CCK, OFDM */
	int freq_band;  /* 2.4Ghz, 5.2Ghz, Mixed */
	int abg_true;   /* ABG flag              */
	
	/* used for forcing the ibss workqueue to terminate 
	 * without wait for the syncro scan to terminate
	 */
	short sync_scan_hurryup; 
	u16 scan_watch_dog;
        int perfect_rssi;
        int worst_rssi;

        u16 prev_seq_ctl;       /* used to drop duplicate frames */

	/* map of allowed channels. 0 is dummy */
	// FIXME: remeber to default to a basic channel plan depending of the PHY type
#ifdef ENABLE_DOT11D
	void* pDot11dInfo;
	bool bGlobalDomain;

	u8   IbssStartChnl;
	u8   ibss_maxjoin_chal;
#else
	int channel_map[MAX_CHANNEL_NUMBER+1];
#endif	
#ifdef _RTL8192_EXT_PATCH_
	u8 backup_channel;
	bool bUseProtection;
#endif	
	int rate;       /* current rate */
	int basic_rate;
	u32	currentRate;//added by amy 090401
	
	//FIXME: pleace callback, see if redundant with softmac_features
	short active_scan;
	
	/* this contains flags for selectively enable softmac support */
	u16 softmac_features;
	
	/* if the sequence control field is not filled by HW */
	u16 seq_ctrl[5];
	
	/* association procedure transaction sequence number */
	u16 associate_seq;
	
	/* AID for RTXed association responses */
	u16 assoc_id;
	
	/* power save mode related*/
	u8 ack_tx_to_ieee;
	short ps;
	short sta_sleep;
	int ps_timeout;
	int ps_period;
	struct tasklet_struct ps_task;
	u32 ps_th;
	u32 ps_tl;
	bool polling;
	
	short raw_tx;
	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	short queue_stop;
	short scanning_continue ;
	short proto_started;
	short proto_stoppping;
	
	struct semaphore wx_sem;
	struct semaphore scan_sem;
	struct semaphore ips_sem;//added by amy for IPS 090331	
	
	spinlock_t mgmt_tx_lock;	
	spinlock_t beacon_lock;

	short beacon_txing;

	short wap_set;
	short ssid_set;
	
	/* set on initialization */
	u8  qos_support;
	unsigned int wmm_acm;
	
	/* for discarding duplicated packets in IBSS */
	struct list_head ibss_mac_hash[IEEE_IBSS_MAC_HASH_SIZE];
	
	/* for discarding duplicated packets in BSS */
	u16 last_rxseq_num[17]; /* rx seq previous per-tid */
	u16 last_rxfrag_num[17];/* tx frag previous per-tid */
	unsigned long last_packet_time[17];
	
	/* for PS mode */
	unsigned long last_rx_ps_time;
	bool			bAwakePktSent; // true if we had sent a packet with PwrMgt bit == 0 since mPss becom eAwake.
	// For leisure power save in connection state, 2007-12-17 by Isaiah
	u8			LPSDelayCnt;
	
	/* used if IEEE_SOFTMAC_SINGLE_QUEUE is set */
	struct sk_buff *mgmt_queue_ring[MGMT_QUEUE_NUM];
	int mgmt_queue_head;
	int mgmt_queue_tail;
//{ added for rtl819x
#define RTLLIB_QUEUE_LIMIT 128
	u8 AsocRetryCount;
	unsigned int hw_header;
	struct sk_buff_head skb_waitQ[MAX_QUEUE_SIZE];
	struct sk_buff_head  skb_aggQ[MAX_QUEUE_SIZE]; 
	struct sk_buff_head  skb_drv_aggQ[MAX_QUEUE_SIZE]; 
	u32	sta_edca_param[4];
	bool aggregation;
	// Enable/Disable Rx immediate BA capability.
	bool enable_rx_imm_BA;
	bool bibsscoordinator;

	//+by amy for DM ,080515
	//Dynamic Tx power for near/far range enable/Disable  , by amy , 2008-05-15
	bool	bdynamic_txpower_enable; 

	bool bCTSToSelfEnable;
	u8 	CTSToSelfTH;
	
	u32 	fsync_time_interval;
	u32	fsync_rate_bitmap;
	u8	fsync_rssi_threshold;
	bool	bfsync_enable;

	u8	fsync_multiple_timeinterval;		// FsyncMultipleTimeInterval * FsyncTimeInterval	 
	u32	fsync_firstdiff_ratethreshold;		// low threshold
	u32	fsync_seconddiff_ratethreshold;	 // decrease threshold
	Fsync_State			fsync_state;
	bool		bis_any_nonbepkts;
	//20Mhz 40Mhz AutoSwitch Threshold
	bandwidth_autoswitch bandwidth_auto_switch;
	//for txpower tracking
	bool FwRWRF;

	//added by amy for AP roaming
	RT_LINK_DETECT_T	LinkDetectInfo;
	bool bIsAggregateFrame;//added by amy for Leisure PS
        //added by amy for ps
	RT_POWER_SAVE_CONTROL	PowerSaveControl;
//}
	//YJ,add,090409
	u8 amsdu_in_process;

	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	struct  tx_pending_t tx_pending;
	
	/* used if IEEE_SOFTMAC_ASSOCIATE is set */
	struct timer_list associate_timer;

	/* used if IEEE_SOFTMAC_BEACONS is set */
	struct timer_list beacon_timer;
#if defined(RTL8192U) || defined(RTL8192SU) || defined(RTL8192SE)
	struct timer_list ibss_wait_timer;
#endif
#ifdef _RTL8192_EXT_PATCH_
	u8 need_sw_enc;
	struct net_device *meshdev;
	struct net_device_stats *meshstats;
	u8 only_mesh;  // 1:only mesh, 0: msta
				   //Callobration Mode: (iw_mode == IW_MODE_INFRA && only_mesh == 0)	
				   //Playgroud Mode & Client Mode: (iw_mode == IW_MODE_MESH && only_mesh == 1)	
				   //Gateway Mode: (iw_mode == IW_MODE_MESH && only_mesh == 0)	
	u8 hostname[MAX_HOST_NAME_LENGTH];
	u8 hostname_len;
	struct list_head mesh_network_free_list;
	struct list_head mesh_network_list;
	struct rtllib_network *mesh_networks;
	struct rtllib_network current_mesh_network;
	enum rtllib_state mesh_state;
	short mesh_started;
	int mesh_security_setting;
	int mesh_sec_type;
	struct list_head mesh_mac_hash[IEEE_MESH_MAC_HASH_SIZE];
	u32 LinkingPeerBitMap;
	u8 LinkingPeerAddr[TOTAL_CAM_ENTRY][ETH_ALEN]; 
	u32 peer_AID_bitmap;
	u8 peer_AID_Addr[30][ETH_ALEN];
	int mesh_txkeyidx;//AMY added for software scrypt wep with mesh0 and WPA with wlan0 for MSTA mode.
	u16 mesh_pairwise_key_type;//AMY added for mesh 
	u16 mesh_group_key_type;
	u32 HwSecCamBitMap; 
	u8 HwSecCamStaAddr[TOTAL_CAM_ENTRY][ETH_ALEN]; 
	SW_CAM_TABLE swmeshcamtable[TOTAL_CAM_ENTRY];
	SW_RATR_TABLE swmeshratrtable[8];
	short meshid_set;
	u8 p2pmode;
	u8 is_server_eth0;  //YJ,add,090217
	u8 serverExtChlOffset;
	u8 APExtChlOffset;
	short meshScanMode;
	bool meshscanning;
	struct sk_buff_head  skb_meshaggQ[MAX_QUEUE_SIZE]; //added by amy 090323
	u8 mesh_amsdu_in_process;//YJ,add,090416
#endif
	work_struct_rsl associate_complete_wq;
#ifdef ENABLE_IPS
	work_struct_rsl ips_leave_wq;//added by amy for IPS 090331
#endif    
#if defined(RTL8192U) || defined(RTL8192SU) || defined(RTL8192SE)
	delayed_work_struct_rsl check_tsf_wq;//added by thomas for adhoc 090805
	delayed_work_struct_rsl update_assoc_sta_info_wq;//added by amy for adhoc 090402
#endif
	delayed_work_struct_rsl associate_procedure_wq;
	delayed_work_struct_rsl softmac_scan_wq;
	delayed_work_struct_rsl associate_retry_wq;
	delayed_work_struct_rsl start_ibss_wq;
#ifndef RTL8190P
	delayed_work_struct_rsl hw_wakeup_wq;
	delayed_work_struct_rsl hw_sleep_wq;
#endif
	delayed_work_struct_rsl link_change_wq;
	work_struct_rsl wx_sync_scan_wq;
#ifdef _RTL8192_EXT_PATCH_
	work_struct_rsl ext_stop_scan_wq;
	work_struct_rsl ext_send_beacon_wq;
	work_struct_rsl ext_create_crypt_for_peers_wq;  
	work_struct_rsl ext_path_sel_ops_wq;  
	work_struct_rsl ext_update_extchnloffset_wq;  
	delayed_work_struct_rsl ext_wx_set_key_wq;
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)  
	struct workqueue_struct *wq;
#else
	u32 *wq;
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,40)  
	/* used for periodly scan */
	struct timer_list scan_timer;
#endif
        // Qos related. Added by Annie, 2005-11-01.
        //STA_QOS  StaQos;

        //u32 STA_EDCA_PARAM[4];
	//CHANNEL_ACCESS_SETTING ChannelAccessSetting;
#ifdef _RTL8192_EXT_PATCH_
	int (*set_mesh_key)(struct net_device *dev,
                        struct iw_point *encoding, struct iw_encode_ext *ext, u8 *addr);
#endif
	/* Callback functions */
	void (*set_security)(struct net_device *dev,
			     struct rtllib_security *sec);
	
	/* Used to TX data frame by using txb structs.
	 * this is not used if in the softmac_features
	 * is set the flag IEEE_SOFTMAC_TX_QUEUE
	 */
	int (*hard_start_xmit)(struct rtllib_txb *txb,
			       struct net_device *dev);
	
	int (*reset_port)(struct net_device *dev);
        int (*is_queue_full) (struct net_device * dev, int pri);

        int (*handle_management) (struct net_device * dev,
                                  struct rtllib_network * network, u16 type);
        int (*is_qos_active) (struct net_device *dev, struct sk_buff *skb);

	/* Softmac-generated frames (mamagement) are TXed via this 
	 * callback if the flag IEEE_SOFTMAC_SINGLE_QUEUE is 
	 * not set. As some cards may have different HW queues that 
	 * one might want to use for data and management frames
	 * the option to have two callbacks might be useful.
	 * This fucntion can't sleep.
	 */
	int (*softmac_hard_start_xmit)(struct sk_buff *skb,
			       struct net_device *dev);
	
	/* used instead of hard_start_xmit (not softmac_hard_start_xmit)
	 * if the IEEE_SOFTMAC_TX_QUEUE feature is used to TX data
	 * frames. I the option IEEE_SOFTMAC_SINGLE_QUEUE is also set
	 * then also management frames are sent via this callback.
	 * This function can't sleep.
	 */    
	void (*softmac_data_hard_start_xmit)(struct sk_buff *skb,
			       struct net_device *dev,int rate);

	/* stops the HW queue for DATA frames. Useful to avoid
	 * waste time to TX data frame when we are reassociating
	 * This function can sleep.
	 */	 
	void (*data_hard_stop)(struct net_device *dev);
	
	/* OK this is complementar to data_poll_hard_stop */
	void (*data_hard_resume)(struct net_device *dev);
	
	/* ask to the driver to retune the radio .
	 * This function can sleep. the driver should ensure
	 * the radio has been swithced before return.
	 */
	void (*set_chan)(struct net_device *dev,short ch);
	
	/* These are not used if the ieee stack takes care of
	 * scanning (IEEE_SOFTMAC_SCAN feature set). 
	 * In this case only the set_chan is used.
	 *
	 * The syncro version is similar to the start_scan but
	 * does not return until all channels has been scanned.
	 * this is called in user context and should sleep, 
	 * it is called in a work_queue when swithcing to ad-hoc mode
	 * or in behalf of iwlist scan when the card is associated 
	 * and root user ask for a scan. 
	 * the fucntion stop_scan should stop both the syncro and
	 * background scanning and can sleep.
	 * The fucntion start_scan should initiate the background 
	 * scanning and can't sleep.
	 */ 
	void (*scan_syncro)(struct net_device *dev);
	void (*start_scan)(struct net_device *dev);
	void (*stop_scan)(struct net_device *dev);
	
	//add here for start or stop firmware scan
	//firmware will scan all channle's based on 
	//firmware channel plan if not stop it.
	int (*rtllib_start_hw_scan)(struct net_device *dev);
	int (*rtllib_stop_hw_scan)(struct net_device *dev);
	
	/* indicate the driver that the link state is changed
	 * for example it may indicate the card is associated now.
	 * Driver might be interested in this to apply RX filter 
	 * rules or simply light the LINK led 
	 */
	void (*link_change)(struct net_device *dev);
	
	/* these two function indicates to the HW when to start
	 * and stop to send beacons. This is used when the 
	 * IEEE_SOFTMAC_BEACONS is not set. For now the
	 * stop_send_bacons is NOT guaranteed to be called only
	 * after start_send_beacons.
	 */
	void (*start_send_beacons) (struct net_device *dev);
	void (*stop_send_beacons) (struct net_device *dev);
	
	/* power save mode related */
	void (*sta_wake_up) (struct net_device *dev);
//	void (*ps_request_tx_ack) (struct net_device *dev);
	void (*enter_sleep_state) (struct net_device *dev, u32 th, u32 tl);
	short (*ps_is_queue_empty) (struct net_device *dev);
#if 0
	/* Typical STA methods */
        int (*handle_auth) (struct net_device * dev,
                            struct rtllib_auth * auth);
        int (*handle_deauth) (struct net_device * dev,
                              struct rtllib_deauth * auth);
        int (*handle_action) (struct net_device * dev,
                              struct rtllib_action * action,
                              struct rtllib_rx_stats * stats);
        int (*handle_disassoc) (struct net_device * dev,
                                struct rtllib_disassoc * assoc);
#endif
        int (*handle_beacon) (struct net_device * dev, struct rtllib_beacon * beacon, struct rtllib_network * network);
#if 0
        int (*handle_probe_response) (struct net_device * dev,
                                      struct rtllib_probe_response * resp,
                                      struct rtllib_network * network);
        int (*handle_probe_request) (struct net_device * dev,
                                     struct rtllib_probe_request * req,
                                     struct rtllib_rx_stats * stats);
#endif
        int (*handle_assoc_response) (struct net_device * dev, struct rtllib_assoc_response_frame * resp, struct rtllib_network * network);

#if 0
        /* Typical AP methods */
        int (*handle_assoc_request) (struct net_device * dev);
        int (*handle_reassoc_request) (struct net_device * dev,
                                       struct rtllib_reassoc_request * req);
#endif

	/* check whether Tx hw resouce available */
	short (*check_nic_enough_desc)(struct net_device *dev, int queue_index);
	short (*get_nic_desc_num)(struct net_device *dev, int queue_index);
	//added by wb for HT related
//	void (*SwChnlByTimerHandler)(struct net_device *dev, int channel);
	void (*SetBWModeHandler)(struct net_device *dev, HT_CHANNEL_WIDTH Bandwidth, HT_EXTCHNL_OFFSET Offset);
	bool (*GetNmodeSupportBySecCfg)(struct net_device* dev);
	void (*SetWirelessMode)(struct net_device* dev, u8 wireless_mode);
	bool (*GetHalfNmodeSupportByAPsHandler)(struct net_device* dev); 
	u8   (*rtllib_ap_sec_type)(struct rtllib_device *ieee);
	void (*HalUsbRxAggrHandler)(struct net_device *dev, bool Value);
	void (*InitialGainHandler)(struct net_device *dev, u8 Operation);
#ifdef _RTL8192_EXT_PATCH_
	int (*set_key_for_AP)(struct rtllib_device *ieee);
	void (*del_hwsec_cam_entry)(struct rtllib_device *ieee, u8 *addr); //YJ,add,090213,for hw sec
	int (*set_key_for_peer) (struct net_device *dev,u8 *Addr,u8 KeyIndex,u16 KeyType,u32 *KeyContent );//YJ,add,090213,for hw sec
	/// rtllib_softmac.c
	int (*ext_patch_rtllib_start_protocol) (struct rtllib_device *ieee); // start special mode

	short (*ext_patch_rtllib_probe_req_1) (struct rtllib_device *ieee); // return = 0: no more phases,  >0: another phase
	u8* (*ext_patch_rtllib_probe_req_2) (struct rtllib_device *ieee, struct sk_buff *skb, u8 *tag); // return tag

	void (*ext_patch_rtllib_stop_protocol) (struct rtllib_device *ieee,bool is_silent_reset); // stop timer
	
	int (*ext_patch_rtllib_rx_frame_softmac_on_peerlink_open) (struct rtllib_device *ieee, struct sk_buff *skb, struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_rx_frame_softmac_on_peerlink_confirm) (struct rtllib_device *ieee, struct sk_buff *skb);
	int (*ext_patch_rtllib_rx_frame_softmac_on_peerlink_close) (struct rtllib_device *ieee, struct sk_buff *skb);
	void (*ext_patch_rtllib_close_all_peerlink) (struct rtllib_device *ieee, __le16 reason);

	int (*ext_patch_rtllib_rx_frame_softmac_on_linkmetric_report) (struct rtllib_device *ieee, struct sk_buff *skb);
	int (*ext_patch_rtllib_rx_frame_softmac_on_linkmetric_req) (struct rtllib_device *ieee, struct sk_buff *skb);

	int (*ext_patch_rtllib_rx_frame_softmac_on_pathselect_preq) (struct rtllib_device *ieee, struct sk_buff *skb,struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_rx_frame_softmac_on_pathselect_prep) (struct rtllib_device *ieee, struct sk_buff *skb,struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_rx_frame_softmac_on_pathselect_perr) (struct rtllib_device *ieee, struct sk_buff *skb,struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_rx_frame_softmac_on_pathselect_rann) (struct rtllib_device *ieee, struct sk_buff *skb,struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_rx_frame_softmac_on_pathselect_pann) (struct rtllib_device *ieee, struct sk_buff *skb,struct rtllib_rx_stats *rx_stats);

	int (*ext_patch_rtllib_ext_stop_scan_wq_set_channel) (struct rtllib_device *ieee);
	int (*ext_patch_r819x_wx_set_mesh_chan)(struct net_device *dev, unsigned char channel);
	void (*ext_patch_r819x_wx_set_channel) (struct rtllib_device *ieee, int ch);
	
	int (*ext_patch_rtllib_softmac_xmit_get_rate) (struct rtllib_device *ieee, struct sk_buff *skb);
	int (*ext_patch_rtllib_rx_frame_softmac_on_auth)(struct rtllib_device *ieee, struct sk_buff *skb, struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_rx_frame_softmac_on_deauth)(struct rtllib_device *ieee, struct sk_buff *skb, struct rtllib_rx_stats *rx_stats);
	int (*ext_patch_rtllib_tx_data)(struct rtllib_device *ieee, struct rtllib_hdr_4addrqos* header, u8* is_peer_n_mode, u8* is_peer_40M,
		u8* is_peer_shortGI_40M, u8* is_peer_shortGI_20M,u8* peer_highest_opra_rate,u8* peer_ratr_index, u8* AID);
	bool (*ext_patch_rtllib_is_mesh)(struct rtllib_device * ieee,u8 * addr);
	ppeerMP_htinfo (*ext_patch_rtllib_get_peermp_htinfo) (struct rtllib_device *ieee,u8 * daddr);//added by amy 090323
	int (* ext_patch_rtllib_create_crypt_for_peer)(struct rtllib_device * ieee);
	void (*ext_patch_rtllib_start_mesh)(struct rtllib_device *ieee);
	// rtllib_rx.c
	void (*ext_patch_rtllib_rx_mgt_on_probe_req) ( struct rtllib_device *ieee, struct rtllib_probe_request *beacon, struct rtllib_rx_stats *stats);
	unsigned int(*ext_patch_rtllib_process_probe_response_1)(struct rtllib_device *ieee,	struct rtllib_probe_response *beacon,	struct rtllib_rx_stats *stats);
	
	void (*ext_patch_rtllib_rx_mgt_update_expire) ( struct rtllib_device *ieee, struct sk_buff *skb);
	struct sk_buff* (*ext_patch_get_beacon_get_probersp)(struct rtllib_device *ieee, u8 *dest, struct rtllib_network *net);

	// success(return 0) is responsible to free skb
	int (*ext_patch_rtllib_rx_on_rx) (struct rtllib_device *ieee, struct sk_buff *skb, struct rtllib_rx_stats *rx_stats, u16 type, u16 stype);
	
	int (*ext_patch_rtllib_rx_frame_get_hdrlen) (struct rtllib_device *ieee, struct sk_buff *skb);
	
	int (*ext_patch_rtllib_rx_frame_get_mac_hdrlen) (struct rtllib_device *ieee, struct sk_buff *skb);
	
	int (*ext_patch_rtllib_rx_frame_get_mesh_hdrlen_llc) (struct rtllib_device *ieee, struct sk_buff *skb);
	// Check whether or not accept the incoming frame. return 0: not accept, >0: accept
	int (*ext_patch_rtllib_rx_is_valid_framectl) (struct rtllib_device *ieee, u16 fc, u16 type, u16 stype);
	
	// return > 0 is success.  0 when failed
	// success(return >0) is responsible to free skb
	//int (*ext_patch_rtllib_rx_process_dataframe) (struct rtllib_device *ieee, struct sk_buff *skb, struct rtllib_rx_stats *rx_stats, u8 *dst, u8 *src);
	
	/* added by david for setting acl dynamically */
	u8 (*ext_patch_rtllib_acl_query) (struct rtllib_device *ieee, u8 *sa);

	// int (*ext_patch_is_duplicate_packet) (struct rtllib_device *ieee, struct rtllib_hdr *header, u16 type, u16 stype);
	
	void (*ext_patch_rtllib_update_ratr_mask)(struct net_device* dev,u8* MacAddr,u8 rssi_level,u32 *ratr_bitmap,u8* bShortGI);
	struct sk_buff* (*ext_patch_rtllib_send_ath_commit) (struct rtllib_device * ieee,u8 * addr);
	struct sk_buff* (*ext_patch_rtllib_send_ath_confirm) (struct rtllib_device * ieee,u8 * addr);
	int (*ext_patch_rtllib_rx_ath_commit) (struct rtllib_device * ieee,u8 * addr);
	u8 (*ext_patch_rtllib_rx_ath_confirm) (struct rtllib_device *ieee, u8* pframe,u16 strlen,u8 *addr);

#endif // _RTL8187_EXT_PATCH_
	bool (*SetFwCmdHandler)(struct net_device *dev,	FW_CMD_IO_TYPE		FwCmdIO);
#ifdef _RTL8192_EXT_PATCH_
	void (*UpdateHalRAMaskHandler)(struct net_device* dev, bool bMulticast, u8 macId, u8* MacAddr, u8 wirelessMode, 	u8 rssi_level);
#else
	void (*UpdateHalRAMaskHandler)(struct net_device* dev, bool bMulticast, u8 macId, u8* MacAddr, u8 *pEntry, 	u8 rssi_level);
#endif
	u16  (*rtl_11n_user_show_rates)(struct net_device *dev);
	void (*ScanOperationBackupHandler)(struct net_device *dev, u8 Operation);
	void (*LedControlHandler)(struct net_device * dev, LED_CTL_MODE LedAction);
	void (*SetHwRegHandler)(struct net_device *dev,u8 variable,u8* val);//added by amy 090330
	void (*GetHwRegHandler)(struct net_device *dev,u8 variable,u8* val);
#ifdef ENABLE_IPS
        void (*rtllib_ips_leave_wq) (struct net_device *dev);//added by amy 090331
        void (*rtllib_ips_leave)(struct net_device *dev);//added by amy 090423
#endif
#ifdef ENABLE_LPS
	void (*LeisurePSLeave)(struct net_device *dev);//added by amy for Leisure PS 090402
#endif
#if defined(RTL8192U) || defined(RTL8192SU) || defined(RTL8192SE)
	void (*SetBeaconRelatedRegistersHandler)(struct net_device* dev);//added by thomas for adhoc 090805
	bool (*check_ht_cap)(struct net_device* dev, struct sta_info *sta, struct rtllib_network* net);//added by amy for adhoc 090402
	void (*Adhoc_InitRateAdaptive)(struct net_device *dev,struct sta_info  *pEntry);//added by amy for adhoc 090403
#endif
	/* This must be the last item so that it points to the data
	 * allocated beyond this structure by alloc_rtllib */
	u8 priv[0];
};

#define IEEE_A            (1<<0)
#define IEEE_B            (1<<1)
#define IEEE_G            (1<<2)
#define IEEE_N_24G 		  (1<<4)
#define	IEEE_N_5G		  (1<<5)
#define IEEE_MODE_MASK    (IEEE_A|IEEE_B|IEEE_G)

/* Generate a 802.11 header */

/* Uses the channel change callback directly
 * instead of [start/stop] scan callbacks
 */
#define IEEE_SOFTMAC_SCAN (1<<2)

/* Perform authentication and association handshake */
#define IEEE_SOFTMAC_ASSOCIATE (1<<3)

/* Generate probe requests */
#define IEEE_SOFTMAC_PROBERQ (1<<4)

/* Generate respones to probe requests */
#define IEEE_SOFTMAC_PROBERS (1<<5)

/* The ieee802.11 stack will manages the netif queue
 * wake/stop for the driver, taking care of 802.11
 * fragmentation. See softmac.c for details. */
#define IEEE_SOFTMAC_TX_QUEUE (1<<7)

/* Uses only the softmac_data_hard_start_xmit
 * even for TX management frames.
 */
#define IEEE_SOFTMAC_SINGLE_QUEUE (1<<8)

/* Generate beacons.  The stack will enqueue beacons
 * to the card 
 */ 
#define IEEE_SOFTMAC_BEACONS (1<<6)

#ifdef _RTL8192_EXT_PATCH_
static inline int rtllib_find_MP(struct rtllib_device* ieee, const u8* addr, u8 set)
{
	int i=0;
	for (i=1; i<MAX_MP; i++)
	{
		if ((ieee->cryptlist[i]->used == 0)&&set)
		{//entry is empty
			memcpy(ieee->cryptlist[i]->mac_addr, addr, ETH_ALEN);
			ieee->cryptlist[i]->used = 1;
			return i;
		}
		else if (0 == memcmp(ieee->cryptlist[i]->mac_addr, addr, ETH_ALEN)) //find matched entry
		{
			return i;
		}
	}
	return -1;
}
#endif

static inline void *rtllib_priv(struct net_device *dev)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0) 
	return ((struct rtllib_device *)netdev_priv(dev))->priv;
#else
	return ((struct rtllib_device *)dev->priv)->priv;
#endif
}

extern inline int rtllib_is_empty_essid(const char *essid, int essid_len)
{
	/* Single white space is for Linksys APs */
	if (essid_len == 1 && essid[0] == ' ')
		return 1;

	/* Otherwise, if the entire essid is 0, we assume it is hidden */
	while (essid_len) {
		essid_len--;
		if (essid[essid_len] != '\0')
			return 0;
	}

	return 1;
}

extern inline int rtllib_is_valid_mode(struct rtllib_device *ieee, int mode)
{
	/*
	 * It is possible for both access points and our device to support
	 * combinations of modes, so as long as there is one valid combination
	 * of ap/device supported modes, then return success
	 *
	 */
	if ((mode & IEEE_A) &&
	    (ieee->modulation & RTLLIB_OFDM_MODULATION) &&
	    (ieee->freq_band & RTLLIB_52GHZ_BAND))
		return 1;

	if ((mode & IEEE_G) &&
	    (ieee->modulation & RTLLIB_OFDM_MODULATION) &&
	    (ieee->freq_band & RTLLIB_24GHZ_BAND))
		return 1;

	if ((mode & IEEE_B) &&
	    (ieee->modulation & RTLLIB_CCK_MODULATION) &&
	    (ieee->freq_band & RTLLIB_24GHZ_BAND))
		return 1;

	return 0;
}

extern inline int rtllib_get_hdrlen(u16 fc)
{
	int hdrlen = RTLLIB_3ADDR_LEN;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case RTLLIB_FTYPE_DATA:
		if ((fc & RTLLIB_FCTL_FROMDS) && (fc & RTLLIB_FCTL_TODS))
			hdrlen = RTLLIB_4ADDR_LEN; /* Addr4 */
		if(RTLLIB_QOS_HAS_SEQ(fc))
			hdrlen += 2; /* QOS ctrl*/
		break;
	case RTLLIB_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case RTLLIB_STYPE_CTS:
		case RTLLIB_STYPE_ACK:
			hdrlen = RTLLIB_1ADDR_LEN;
			break;
		default:
			hdrlen = RTLLIB_2ADDR_LEN;
			break;
		}
		break;
	}

	return hdrlen;
}

static inline u8 *rtllib_get_payload(struct rtllib_hdr *hdr)
{
        switch (rtllib_get_hdrlen(le16_to_cpu(hdr->frame_ctl))) {
        case RTLLIB_1ADDR_LEN:
                return ((struct rtllib_hdr_1addr *)hdr)->payload;
        case RTLLIB_2ADDR_LEN:
                return ((struct rtllib_hdr_2addr *)hdr)->payload;
        case RTLLIB_3ADDR_LEN:
                return ((struct rtllib_hdr_3addr *)hdr)->payload;
        case RTLLIB_4ADDR_LEN:
                return ((struct rtllib_hdr_4addr *)hdr)->payload;
        }
        return NULL;
}

static inline int rtllib_is_ofdm_rate(u8 rate)
{
        switch (rate & ~RTLLIB_BASIC_RATE_MASK) {
        case RTLLIB_OFDM_RATE_6MB:
        case RTLLIB_OFDM_RATE_9MB:
        case RTLLIB_OFDM_RATE_12MB:
        case RTLLIB_OFDM_RATE_18MB:
        case RTLLIB_OFDM_RATE_24MB:
        case RTLLIB_OFDM_RATE_36MB:
        case RTLLIB_OFDM_RATE_48MB:
        case RTLLIB_OFDM_RATE_54MB:
                return 1;
        }
        return 0;
}

static inline int rtllib_is_cck_rate(u8 rate)
{
        switch (rate & ~RTLLIB_BASIC_RATE_MASK) {
        case RTLLIB_CCK_RATE_1MB:
        case RTLLIB_CCK_RATE_2MB:
        case RTLLIB_CCK_RATE_5MB:
        case RTLLIB_CCK_RATE_11MB:
                return 1;
        }
        return 0;
}


/* rtllib.c */
extern void free_rtllib(struct net_device *dev);
extern struct net_device *alloc_rtllib(int sizeof_priv);

extern int rtllib_set_encryption(struct rtllib_device *ieee);

/* rtllib_tx.c */

#ifdef _RTL8192_EXT_PATCH_
extern int rtllib_encrypt_fragment(
	struct rtllib_device *ieee,
	struct sk_buff *frag,
	int hdr_len, u8 is_mesh);
#else
extern int rtllib_encrypt_fragment(
	struct rtllib_device *ieee,
	struct sk_buff *frag,
	int hdr_len);
#endif

extern int rtllib_xmit(struct sk_buff *skb,  struct net_device *dev);
#ifdef _RTL8192_EXT_PATCH_
extern int rtllib_classify(struct sk_buff *skb, u8 bIsAmsdu);
extern int rtllib_mesh_xmit(struct sk_buff *skb,  struct net_device *dev);
extern inline int rtllib_put_snap(u8 *data, u16 h_proto);
#endif
extern void rtllib_txb_free(struct rtllib_txb *);

#ifdef ENABLE_AMSDU
extern struct sk_buff *AMSDU_Aggregation(struct rtllib_device *ieee, struct sk_buff_head *pSendList);
extern u8 AMSDU_GetAggregatibleList(struct rtllib_device *ieee, struct sk_buff *pCurSkb, struct sk_buff_head	*pSendList, u8 queue_index);
extern struct sk_buff *msh_AMSDU_Aggregation(struct rtllib_device *ieee, struct sk_buff_head *pSendList);
extern u8 msh_AMSDU_GetAggregatibleList(struct rtllib_device *ieee, struct sk_buff *pCurSkb, struct sk_buff_head *pSendList, u8 queue_index);
#endif

/* rtllib_rx.c */
extern int rtllib_rx(struct rtllib_device *ieee, struct sk_buff *skb,
			struct rtllib_rx_stats *rx_stats);
extern void rtllib_rx_mgt(struct rtllib_device *ieee,
			     struct sk_buff *skb,
			     struct rtllib_rx_stats *stats);
extern void rtllib_rx_probe_rq(struct rtllib_device *ieee, 
                           struct sk_buff *skb);

/* rtllib_wx.c */
extern int rtllib_wx_get_scan(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *key);
#ifdef _RTL8192_EXT_PATCH_
extern int rtllib_wx_set_encode(struct rtllib_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key, u8 is_mesh);
extern int rtllib_wx_get_encode(struct rtllib_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key, u8 is_mesh);
extern int rtllib_mesh_set_encode_ext(struct rtllib_device *ieee,
                               struct iw_point *encoding, struct iw_encode_ext *ext, int entry);
#if WIRELESS_EXT >= 18
extern int rtllib_wx_get_encode_ext(struct rtllib_device *ieee,
                            struct iw_request_info *info,
                            union iwreq_data* wrqu, char *extra, u8 is_mesh);
extern int rtllib_wx_set_encode_ext(struct rtllib_device *ieee,
                            struct iw_request_info *info,
                            union iwreq_data* wrqu, char *extra, u8 is_mesh);
#endif
#else
extern int rtllib_wx_set_encode(struct rtllib_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
extern int rtllib_wx_get_encode(struct rtllib_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
#if WIRELESS_EXT >= 18
extern int rtllib_wx_get_encode_ext(struct rtllib_device *ieee,
                            struct iw_request_info *info,
                            union iwreq_data* wrqu, char *extra);
extern int rtllib_wx_set_encode_ext(struct rtllib_device *ieee,
                            struct iw_request_info *info,
                            union iwreq_data* wrqu, char *extra);
#endif
#endif
extern int rtllib_wx_set_auth(struct rtllib_device *ieee,
                               struct iw_request_info *info,
                               struct iw_param *data, char *extra);
extern int rtllib_wx_set_mlme(struct rtllib_device *ieee,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra);
extern int rtllib_wx_set_gen_ie(struct rtllib_device *ieee, u8 *ie, size_t len);

/* rtllib_softmac.c */
extern short rtllib_is_54g(struct rtllib_network *net);
extern short rtllib_is_shortslot(struct rtllib_network net);
extern int rtllib_rx_frame_softmac(struct rtllib_device *ieee, struct sk_buff *skb,
			struct rtllib_rx_stats *rx_stats, u16 type,
			u16 stype);
extern void rtllib_softmac_new_net(struct rtllib_device *ieee, struct rtllib_network *net);

void SendDisassociation(struct rtllib_device *ieee, bool deauth, u16 asRsn);
extern void rtllib_softmac_xmit(struct rtllib_txb *txb, struct rtllib_device *ieee);

extern void rtllib_stop_send_beacons(struct rtllib_device *ieee);
extern void notify_wx_assoc_event(struct rtllib_device *ieee);
extern void rtllib_softmac_check_all_nets(struct rtllib_device *ieee);
extern void rtllib_start_bss(struct rtllib_device *ieee);
extern void rtllib_start_master_bss(struct rtllib_device *ieee);
extern void rtllib_start_ibss(struct rtllib_device *ieee);
extern void rtllib_softmac_init(struct rtllib_device *ieee);
extern void rtllib_softmac_free(struct rtllib_device *ieee);
extern void rtllib_associate_abort(struct rtllib_device *ieee);
extern void rtllib_disassociate(struct rtllib_device *ieee);
extern void rtllib_stop_scan(struct rtllib_device *ieee);
#ifdef _RTL8192_EXT_PATCH_
extern void rtllib_start_scan_syncro(struct rtllib_device *ieee, u8 is_mesh);
#else
extern void rtllib_start_scan_syncro(struct rtllib_device *ieee);
#endif
extern void rtllib_sta_ps_send_null_frame(struct rtllib_device *ieee, short pwr);
extern void rtllib_sta_wakeup(struct rtllib_device *ieee, short nl);
extern void rtllib_sta_ps_send_pspoll_frame(struct rtllib_device *ieee);
extern void rtllib_check_all_nets(struct rtllib_device *ieee);
extern void rtllib_start_protocol(struct rtllib_device *ieee);
extern void rtllib_stop_protocol(struct rtllib_device *ieee, u8 shutdown);//modified by amy for PS 090407
#ifdef _RTL8192_EXT_PATCH_
extern void rtllib_softmac_start_protocol(struct rtllib_device *ieee,u8 mesh_flag);
extern void rtllib_softmac_stop_protocol(struct rtllib_device *ieee, u8 mesh_flag, u8 shutdown);
extern void rtllib_start_mesh_protocol(struct rtllib_device *ieee);
extern void rtllib_stop_mesh_protocol(struct rtllib_device *ieee);
#else
extern void rtllib_softmac_start_protocol(struct rtllib_device *ieee);
extern void rtllib_softmac_stop_protocol(struct rtllib_device *ieee, u8 shutdown);
#endif
extern void rtllib_reset_queue(struct rtllib_device *ieee);
extern void rtllib_wake_queue(struct rtllib_device *ieee);
extern void rtllib_stop_queue(struct rtllib_device *ieee);
extern struct sk_buff *rtllib_get_beacon(struct rtllib_device *ieee);
extern void rtllib_start_send_beacons(struct rtllib_device *ieee);
extern void rtllib_stop_send_beacons(struct rtllib_device *ieee);
#ifdef _RTL8192_EXT_PATCH_
extern int rtllib_wpa_supplicant_ioctl(struct rtllib_device *ieee, struct iw_point *p, u8 is_mesh);
#else
extern int rtllib_wpa_supplicant_ioctl(struct rtllib_device *ieee, struct iw_point *p);
#endif
extern void notify_wx_assoc_event(struct rtllib_device *ieee);
extern void rtllib_ps_tx_ack(struct rtllib_device *ieee, short success);

extern void softmac_mgmt_xmit(struct sk_buff *skb, struct rtllib_device *ieee);
extern u16 rtllib_query_seqnum(struct rtllib_device*ieee, struct sk_buff* skb, u8* dst);
extern u8 rtllib_ap_sec_type(struct rtllib_device *ieee);

#ifdef _RTL8192_EXT_PATCH_
extern void rtllib_rx_auth_rq(struct rtllib_device *ieee, struct sk_buff *skb);
extern void rtllib_resp_to_auth(struct rtllib_device *ieee, int s, u8* dest);
extern void rtllib_associate_step1(struct rtllib_device *ieee,u8 * addr);
extern bool rtllib_ext_prepare_to_joinmesh(struct rtllib_device *ieee, struct rtllib_network *pstat);
extern u8 HTFilterMCSRate( struct rtllib_device* ieee, u8* pSupportMCS, u8* pOperateMCS);
extern void HTUseDefaultSetting(struct rtllib_device* ieee);
extern void rtllib_start_scan(struct rtllib_device *ieee);
extern struct sk_buff* rtllib_ext_probe_resp_by_net(struct rtllib_device *ieee, u8 *dest, struct rtllib_network *net);
extern int rtllib_network_init(struct rtllib_device *ieee, struct rtllib_probe_response *beacon, struct rtllib_network *network, struct rtllib_rx_stats *stats);
extern struct rtllib_txb *rtllib_alloc_txb(int nr_frags, int txb_size, int gfp_mask);
extern void rtllib_ext_send_11s_beacon(struct rtllib_device *ieee);
#endif

/* rtllib_crypt_ccmp&tkip&wep.c */
extern void rtllib_tkip_null(void);
extern void rtllib_wep_null(void);
extern void rtllib_ccmp_null(void);

/* rtllib_softmac_wx.c */

extern int rtllib_wx_get_wap(struct rtllib_device *ieee, 
			    struct iw_request_info *info, 
			    union iwreq_data *wrqu, char *ext);
			    
extern int rtllib_wx_set_wap(struct rtllib_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra);
			 
extern int rtllib_wx_get_essid(struct rtllib_device *ieee, struct iw_request_info *a,union iwreq_data *wrqu,char *b);

extern int rtllib_wx_set_rate(struct rtllib_device *ieee, 
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);
			     
extern int rtllib_wx_get_rate(struct rtllib_device *ieee, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra);
			     
extern int rtllib_wx_set_mode(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);
			     
extern int rtllib_wx_set_scan(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);
			     
extern int rtllib_wx_set_essid(struct rtllib_device *ieee, 
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra);
			      
extern int rtllib_wx_get_mode(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

extern int rtllib_wx_set_freq(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

#ifdef _RTL8192_EXT_PATCH_
extern int rtllib_wx_get_freq(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b, u8 is_mesh);
#else
extern int rtllib_wx_get_freq(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);
#endif
extern void rtllib_wx_sync_scan_wq(void *data);

extern int rtllib_wx_set_rawtx(struct rtllib_device *ieee, 
			       struct iw_request_info *info, 
			       union iwreq_data *wrqu, char *extra);
			       
extern int rtllib_wx_get_name(struct rtllib_device *ieee, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_set_power(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_power(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_set_rts(struct rtllib_device *ieee, 
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_rts(struct rtllib_device *ieee, 
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);
#ifdef _RTL8192_EXT_PATCH_
extern void rtllib_sta_ps_send_null_frame(struct rtllib_device *ieee, short pwr);
#endif
//HT				 			     
#define MAX_RECEIVE_BUFFER_SIZE 9100  //
extern void HTDebugHTCapability(u8* CapIE, u8* TitleString );
extern void HTDebugHTInfo(u8*  InfoIE, u8* TitleString);

void HTSetConnectBwMode(struct rtllib_device* ieee, HT_CHANNEL_WIDTH Bandwidth, HT_EXTCHNL_OFFSET    Offset);
extern void HTUpdateDefaultSetting(struct rtllib_device* ieee);
#ifdef _RTL8192_EXT_PATCH_
extern void HTConstructCapabilityElement(struct rtllib_device* ieee, u8* posHTCap, u8* len, u8 isEncrypt, u8 bIsBcn);
#else
extern void HTConstructCapabilityElement(struct rtllib_device* ieee, u8* posHTCap, u8* len, u8 isEncrypt);
#endif
extern void HTConstructInfoElement(struct rtllib_device* ieee, u8* posHTInfo, u8* len, u8 isEncrypt);
extern void HTConstructRT2RTAggElement(struct rtllib_device* ieee, u8* posRT2RTAgg, u8* len);
extern void HTOnAssocRsp(struct rtllib_device *ieee);
extern void HTInitializeHTInfo(struct rtllib_device* ieee);
extern void HTInitializeBssDesc(PBSS_HT pBssHT);
extern void HTResetSelfAndSavePeerSetting(struct rtllib_device* ieee, struct rtllib_network * pNetwork);
extern void HTUpdateSelfAndPeerSetting(struct rtllib_device* ieee,   struct rtllib_network * pNetwork);
extern u8 HTGetHighestMCSRate(struct rtllib_device* ieee, u8* pMCSRateSet, u8* pMCSFilter);
extern u8 MCS_FILTER_ALL[];
extern u16 MCS_DATA_RATE[2][2][77] ;
extern u8 HTCCheck(struct rtllib_device* ieee, u8*   pFrame);
//extern void HTSetConnectBwModeCallback(unsigned long data);
extern void HTResetIOTSetting(PRT_HIGH_THROUGHPUT  pHTInfo);
extern bool IsHTHalfNmodeAPs(struct rtllib_device* ieee);
extern u16 HTHalfMcsToDataRate(struct rtllib_device* ieee,  u8      nMcsRate);
extern u16 HTMcsToDataRate( struct rtllib_device* ieee, u8 nMcsRate);
extern u16  TxCountToDataRate( struct rtllib_device* ieee, u8 nDataRate);
//function in BAPROC.c
extern int rtllib_rx_ADDBAReq( struct rtllib_device* ieee, struct sk_buff *skb);
extern int rtllib_rx_ADDBARsp( struct rtllib_device* ieee, struct sk_buff *skb);
extern int rtllib_rx_DELBA(struct rtllib_device* ieee,struct sk_buff *skb);
extern void TsInitAddBA( struct rtllib_device* ieee, PTX_TS_RECORD   pTS, u8 Policy, u8 bOverwritePending);
extern void TsInitDelBA( struct rtllib_device* ieee, PTS_COMMON_INFO pTsCommonInfo, TR_SELECT TxRxSelect);
extern void BaSetupTimeOut(unsigned long data);
extern void TxBaInactTimeout(unsigned long data);
extern void RxBaInactTimeout(unsigned long data);
extern void ResetBaEntry( PBA_RECORD pBA);
//function in TS.c
extern bool GetTs(
        struct rtllib_device*        ieee,
        PTS_COMMON_INFO                 *ppTS,
        u8*                             Addr,
        u8                              TID,
        TR_SELECT                       TxRxSelect,  //Rx:1, Tx:0
        bool                            bAddNewTs
        );
extern void TSInitialize(struct rtllib_device *ieee);
extern  void TsStartAddBaProcess(struct rtllib_device* ieee, PTX_TS_RECORD   pTxTS);			 			     
extern void RemovePeerTS(struct rtllib_device* ieee, u8* Addr);
extern void RemoveAllTS(struct rtllib_device* ieee);
#ifdef _RTL8192_EXT_PATCH_
extern void ResetAdmitTRStream(struct rtllib_device *ieee, u8 *Addr);
void rtllib_softmac_scan_syncro(struct rtllib_device *ieee, u8 is_mesh);
#else
void rtllib_softmac_scan_syncro(struct rtllib_device *ieee);
#endif
extern const long rtllib_wlan_frequencies[];

extern inline void rtllib_increment_scans(struct rtllib_device *ieee)
{
	ieee->scans++;
}

extern inline int rtllib_get_scans(struct rtllib_device *ieee)
{
	return ieee->scans;
}

static inline const char *escape_essid(const char *essid, u8 essid_len) {
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = essid;
	char *d = escaped;

	if (rtllib_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	essid_len = min(essid_len, (u8)IW_ESSID_MAX_SIZE);
	while (essid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
	return escaped;
}

#define CONVERT_RATE(_ieee, _MGN_RATE)	(_MGN_RATE<MGN_MCS0)?(_MGN_RATE):(HTMcsToDataRate(_ieee, (u8)_MGN_RATE))

/* For the function is more related to hardware setting, it's better to use the  
 * ieee handler to refer to it.
 */
extern short check_nic_enough_desc(struct net_device *dev, int queue_index);
extern int rtllib_data_xmit(struct sk_buff *skb, struct net_device *dev);
extern int rtllib_parse_info_param(struct rtllib_device *ieee,
		struct rtllib_info_element *info_element, 
		u16 length,
		struct rtllib_network *network,
		struct rtllib_rx_stats *stats);

void rtllib_indicate_packets(struct rtllib_device *ieee, struct rtllib_rxb** prxbIndicateArray,u8  index);
#if defined(RTL8192U) || defined(RTL8192SU) || defined(RTL8192SE)
extern void IbssAgeFunction(struct rtllib_device *ieee);//added by amy for adhoc 090403
extern struct sta_info *GetStaInfo(struct rtllib_device *ieee, u8 *addr);
extern void DelStaInfoList(struct rtllib_device *ieee);
extern void DelStaInfo(struct rtllib_device *ieee, u8 *addr);
#endif
extern u8 HTFilterMCSRate( struct rtllib_device* ieee, u8* pSupportMCS, u8* pOperateMCS);
extern void HTUseDefaultSetting(struct rtllib_device* ieee);
#define RT_ASOC_RETRY_LIMIT	5
u8 MgntQuery_TxRateExcludeCCKRates(struct rtllib_device *ieee);
#ifdef _RTL8192_EXT_PATCH_
extern unsigned int rtllib_MFIE_rate_len(struct rtllib_device *ieee);
extern void rtllib_MFIE_Brate(struct rtllib_device *ieee, u8 **tag_p);
extern void rtllib_MFIE_Grate(struct rtllib_device *ieee, u8 **tag_p);
extern void rtllib_WMM_Info(struct rtllib_device *ieee, u8 **tag_p);
#endif
#ifdef THOMAS_TURBO
extern void rtllib_TURBO_Info(struct rtllib_device *ieee, u8 **tag_p);
#endif
#ifndef ENABLE_LOCK_DEBUG
#define SPIN_LOCK_IEEE(plock) spin_lock_irqsave((plock), flags)	
#define SPIN_UNLOCK_IEEE(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_REORDER(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_REORDER(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_WPAX(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_WPAX(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_MGNTTX(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_MGNTTX(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_BCN(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_BCN(plock) spin_unlock_irqrestore((plock), flags)
//////////////////////////////////MSHCLASS SPINLOCK///////////////////////////////////////////////////////
#define SPIN_LOCK_MSH_STAINFO(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_MSH_STAINFO(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_MSH_PREQ(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_MSH_PREQ(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_MSH_QUEUE(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_MSH_QUEUE(plock) spin_unlock_irqrestore((plock), flags)
///////////////////////////////////PRIV SPINLOCK///////////////////////////////////////////////////////
#define SPIN_LOCK_PRIV_RFPS(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_RFPS(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_IRQTH(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_IRQTH(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_TX(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_TX(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_D3(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_D3(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_RF(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_RF(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_PS(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_PS(plock) spin_unlock_irqrestore((plock), flags)
///////////////////////////////IEEE SEMAPHORE///////////////////////////////////////////////////////
#define SEM_DOWN_IEEE_WX(psem) down(psem)
#define SEM_UP_IEEE_WX(psem) up(psem)
#define SEM_DOWN_IEEE_SCAN(psem) down(psem)
#define SEM_UP_IEEE_SCAN(psem) up(psem)
#define SEM_DOWN_IEEE_IPS(psem) down(psem)
#define SEM_UP_IEEE_IPS(psem) up(psem)
//////////////////////////////PRIV SEMAPHORE///////////////////////////////////////////////////////
#define SEM_DOWN_PRIV_WX(psem) down(psem)
#define SEM_UP_PRIV_WX(psem) up(psem)
#define SEM_DOWN_PRIV_RF(psem) down(psem)
#define SEM_UP_PRIV_RF(psem) up(psem)
///////////////////////////////PRIV MUTEX///////////////////////////////////////////////////////
#define MUTEX_LOCK_PRIV(pmutex) mutex_lock(pmutex)
#define MUTEX_UNLOCK_PRIV(pmutex) mutex_unlock(pmutex)
#endif
static inline void dump_buf(u8 *buf, u32 len)
{
	u32 i;
	printk("-----------------Len %d----------------\n", len);
	for(i=0; i<len; i++)
		printk("%2.2x-", *(buf+i));
	printk("\n");
}
#endif /* RTLLIB_H */
