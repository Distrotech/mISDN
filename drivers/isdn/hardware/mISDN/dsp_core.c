/* $Id: dsp_core.c,v 1.29 2007/03/27 15:06:29 jolly Exp $
 *
 * Author       Andreas Eversberg (jolly@eversberg.eu)
 * Based on source code structure by
 *		Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/mISDN.cert
 *
 * Thanks to    Karsten Keil (great drivers)
 *              Cologne Chip (great chips)
 *
 * This module does:
 *		Real-time tone generation
 *		DTMF detection
 *		Real-time cross-connection and conferrence
 *		Compensate jitter due to system load and hardware fault.
 *		All features are done in kernel space and will be realized
 *		using hardware, if available and supported by chip set.
 *		Blowfish encryption/decryption
 */

/* STRUCTURE:
 *
 * The dsp module provides layer 2 for b-channels (64kbit). It provides
 * transparent audio forwarding with special digital signal processing:
 *
 * - (1) generation of tones
 * - (2) detection of dtmf tones
 * - (3) crossconnecting and conferences
 * - (4) echo generation for delay test
 * - (5) volume control
 * - (6) disable receive data
 * - (7) echo cancelation
 * - (8) encryption/decryption
 *
 * Look:
 *             TX            RX
 *         ------upper layer------
 *             |             ^
 *             |             |(6)
 *             v             |
 *       +-----+-------------+-----+
 *       |(3)(4)                   |
 *       |           CMX           |
 *       |                         |
 *       |           +-------------+
 *       |           |       ^
 *       |           |       |
 *       |+---------+|  +----+----+
 *       ||(1)      ||  |(5)      |
 *       ||         ||  |         |
 *       ||  Tones  ||  |RX Volume|
 *       ||         ||  |         |
 *       ||         ||  |         |
 *       |+----+----+|  +----+----+
 *       +-----+-----+       ^
 *             |             | 
 *             v             |
 *        +----+----+   +----+----+
 *        |(5)      |   |(2)      |
 *        |         |   |         |
 *        |TX Volume|   |  DTMF   |
 *        |         |   |         |
 *        |         |   |         |
 *        +----+----+   +----+----+
 *             |             ^ 
 *             |             |
 *             v             |
 *        +----+-------------+----+
 *        |(7)                    |
 *        |                       |
 *        |   Echo Cancellation   |
 *        |                       |
 *        |                       |
 *        +----+-------------+----+
 *             |             ^ 
 *             |             |
 *             v             |
 *        +----+----+   +----+----+
 *        |(8)      |   |(8)      |
 *        |         |   |         |
 *        | Encrypt |   | Decrypt |
 *        |         |   |         |
 *        |         |   |         |
 *        +----+----+   +----+----+
 *             |             ^ 
 *             |             |
 *             v             |
 *         ------card  layer------
 *             TX            RX
 *
 * Above you can see the logical data flow. If software is used to do the
 * process, it is actually the real data flow. If hardware is used, data
 * may not flow, but hardware commands to the card, to provide the data flow
 * as shown.
 *
 * NOTE: The channel must be activated in order to make dsp work, even if
 * no data flow to the upper layer is intended. Activation can be done
 * after and before controlling the setting using PH_CONTROL requests.
 *
 * DTMF: Will be detected by hardware if possible. It is done before CMX 
 * processing.
 *
 * Tones: Will be generated via software if endless looped audio fifos are
 * not supported by hardware. Tones will override all data from CMX.
 * It is not required to join a conference to use tones at any time.
 *
 * CMX: Is transparent when not used. When it is used, it will do
 * crossconnections and conferences via software if not possible through
 * hardware. If hardware capability is available, hardware is used.
 *
 * Echo: Is generated by CMX and is used to check performane of hard and
 * software CMX.
 *
 * The CMX has special functions for conferences with one, two and more
 * members. It will allow different types of data flow. Receive and transmit
 * data to/form upper layer may be swithed on/off individually without loosing
 * features of CMX, Tones and DTMF.
 *
 * Echo Cancellation: Sometimes we like to cancel echo from the interface.
 * Note that a VoIP call may not have echo caused by the IP phone. The echo
 * is generated by the telephone line connected to it. Because the delay
 * is high, it becomes an echo. RESULT: Echo Cachelation is required if
 * both echo AND delay is applied to an interface.
 * Remember that software CMX always generates a more or less delay.
 *
 * If all used features can be realized in hardware, and if transmit and/or
 * receive data ist disabled, the card may not send/receive any data at all.
 * Not receiving is usefull if only announcements are played. Not sending is
 * usefull if an answering machine records audio. Not sending and receiving is
 * usefull during most states of the call. If supported by hardware, tones
 * will be played without cpu load. Small PBXs and NT-Mode applications will
 * not need expensive hardware when processing calls.
 *
 *
 * LOCKING:
 *
 * When data is received from upper or lower layer (card), the complete dsp
 * module is locked by a global lock.  When data is ready to be transmitted
 * to a different layer, the module is unlocked. It is not allowed to hold a
 * lock outside own layer.
 * Reasons: Multiple threads must not process cmx at the same time, if threads
 * serve instances, that are connected in same conference.
 * PH_CONTROL must not change any settings, join or split conference members
 * during process of data.
 * 
 *
 * TRANSMISSION:
 *

TBD

There are three things that need to receive data from card:
 - software DTMF decoder
 - software cmx (if conference exists)
 - upper layer, if rx-data not disabled

Whenever dtmf decoder is turned on or off, software cmx changes, rx-data is disabled or enabled, or card becomes activated, then rx-data is disabled or enabled using a special command to the card.

There are three things that need to transmit data to card:
 - software tone generation (part of cmx)
 - software cmx
 - upper layer, if tx-data is written to tx-buffer



 
 */

const char *dsp_revision = "$Revision: 1.29 $";

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include "core.h"
#include "layer1.h"
#include "helper.h"
#include "debug.h"
#include "dsp.h"

static char DSPName[] = "DSP";
mISDNobject_t dsp_obj;

static int debug = 0;
int dsp_debug;
static int options = 0;
int dsp_options;
static int poll = 0;
int dsp_poll, dsp_tics;

int dtmfthreshold=100L;

#ifdef MODULE
MODULE_AUTHOR("Andreas Eversberg");
#ifdef OLD_MODULE_PARAM
MODULE_PARM(debug, "1i");
MODULE_PARM(options, "1i");
MODULE_PARM(poll, "1i");
MODULE_PARM(dtmfthreshold, "1i");
#else
module_param(debug, uint, S_IRUGO | S_IWUSR);
module_param(options, uint, S_IRUGO | S_IWUSR);
module_param(poll, uint, S_IRUGO | S_IWUSR);
module_param(dtmfthreshold, uint, S_IRUGO | S_IWUSR);
#endif
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#endif


/*
 * special message process for DL_CONTROL | REQUEST
 */
static int
dsp_control_req(dsp_t *dsp, mISDN_head_t *hh, struct sk_buff *skb)
{
	struct		sk_buff *nskb;
	int ret = 0;
	int cont;
	u8 *data;
	int len;

	if (skb->len < sizeof(int)) {
		printk(KERN_ERR "%s: PH_CONTROL message too short\n", __FUNCTION__);
	}
	cont = *((int *)skb->data);
	len = skb->len - sizeof(int);
	data = skb->data + sizeof(int);

	switch (cont) {
		case DTMF_TONE_START: /* turn on DTMF */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: start dtmf\n", __FUNCTION__);
#if 0
			if (len == sizeof(int)) {
				printk(KERN_NOTICE "changing DTMF Threshold to %d\n",*((int*)data));
				dsp->dtmf.treshold=(*(int*)data)*10000;
			}
#endif

			dsp_dtmf_goertzel_init(dsp);
			/* checking for hardware capability */
			if (dsp->features.hfc_dtmf) {
				dsp->dtmf.hardware = 1;
				dsp->dtmf.software = 0;
			} else {
				dsp->dtmf.hardware = 0;
				dsp->dtmf.software = 1;
			}
			break;
		case DTMF_TONE_STOP: /* turn off DTMF */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: stop dtmf\n", __FUNCTION__);
			dsp->dtmf.hardware = 0;
			dsp->dtmf.software = 0;
			break;
		case CMX_CONF_JOIN: /* join / update conference */
			if (len != sizeof(int)) {
				ret = -EINVAL;
				break;
			}
			if (*((u32 *)data) == 0)
				goto conf_split;
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: join conference %d\n", __FUNCTION__, *((u32 *)data));
			ret = dsp_cmx_conf(dsp, *((u32 *)data));
			if (dsp_debug & DEBUG_DSP_CMX)
				dsp_cmx_debug(dsp);
			break;
		case CMX_CONF_SPLIT: /* remove from conference */
			conf_split:
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: release conference\n", __FUNCTION__);
			ret = dsp_cmx_conf(dsp, 0);
			if (dsp_debug & DEBUG_DSP_CMX)
				dsp_cmx_debug(dsp);
			break;
		case TONE_PATT_ON: /* play tone */
			if (len != sizeof(int)) {
				ret = -EINVAL;
				break;
			}
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: turn tone 0x%x on\n", __FUNCTION__, *((int *)skb->data));
			ret = dsp_tone(dsp, *((int *)data));
			if (!ret)
				dsp_cmx_hardware(dsp->conf, dsp);
			if (!dsp->tone.tone)
				goto tone_off;
			break;
		case TONE_PATT_OFF: /* stop tone */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: turn tone off\n", __FUNCTION__);
			dsp_tone(dsp, 0);
			dsp_cmx_hardware(dsp->conf, dsp);
			/* reset tx buffers (user space data) */
			tone_off:
			dsp->tx_R = dsp->tx_W = 0;
			break;
		case VOL_CHANGE_TX: /* change volume */
			if (len != sizeof(int)) {
				ret = -EINVAL;
				break;
			}
			dsp->tx_volume = *((int *)data);
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: change tx volume to %d\n", __FUNCTION__, dsp->tx_volume);
			dsp_cmx_hardware(dsp->conf, dsp);
			break;
		case VOL_CHANGE_RX: /* change volume */
			if (len != sizeof(int)) {
				ret = -EINVAL;
				break;
			}
			dsp->rx_volume = *((int *)data);
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: change rx volume to %d\n", __FUNCTION__, dsp->tx_volume);
			dsp_cmx_hardware(dsp->conf, dsp);
			break;
		case CMX_ECHO_ON: /* enable echo */
			dsp->echo = 1; /* soft echo */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: enable cmx-echo\n", __FUNCTION__);
			dsp_cmx_hardware(dsp->conf, dsp);
			if (dsp_debug & DEBUG_DSP_CMX)
				dsp_cmx_debug(dsp);
			break;
		case CMX_ECHO_OFF: /* disable echo */
			dsp->echo = 0;
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: disable cmx-echo\n", __FUNCTION__);
			dsp_cmx_hardware(dsp->conf, dsp);
			if (dsp_debug & DEBUG_DSP_CMX)
				dsp_cmx_debug(dsp);
			break;
		case CMX_RECEIVE_ON: /* enable receive to user space */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: enable receive to user space\n", __FUNCTION__);
			dsp->rx_disabled = 0;
			dsp_cmx_hardware(dsp->conf, dsp);
			break;
		case CMX_RECEIVE_OFF: /* disable receive to user space */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: disable receive to user space\n", __FUNCTION__);
			dsp->rx_disabled = 1;
			dsp_cmx_hardware(dsp->conf, dsp);
			break;
		case CMX_MIX_ON: /* enable mixing of transmit data with conference members */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: enable mixing of tx-data with conf mebers\n", __FUNCTION__);
			dsp->tx_mix = 1;
			dsp_cmx_hardware(dsp->conf, dsp);
			if (dsp_debug & DEBUG_DSP_CMX)
				dsp_cmx_debug(dsp);
			break;
		case CMX_MIX_OFF: /* disable mixing of transmit data with conference members */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: disable mixing of tx-data with conf mebers\n", __FUNCTION__);
			dsp->tx_mix = 0;
			dsp_cmx_hardware(dsp->conf, dsp);
			if (dsp_debug & DEBUG_DSP_CMX)
				dsp_cmx_debug(dsp);
			break;
		case ECHOCAN_ON: /* turn echo calcellation on */
			if (len<4) {
				ret = -EINVAL;
			} else {
				int ec_arr[2];
				memcpy(&ec_arr,data,sizeof(ec_arr));
				if (dsp_debug & DEBUG_DSP_CORE)
					printk(KERN_DEBUG "%s: turn echo cancelation on (delay=%d attenuation-shift=%d\n",
						__FUNCTION__, ec_arr[0], ec_arr[1]);
			
				ret = dsp_cancel_init(dsp, ec_arr[0], ec_arr[1] ,1);
				dsp_cmx_hardware(dsp->conf, dsp);
			}
			break;
		case ECHOCAN_OFF: /* turn echo calcellation off */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: turn echo cancelation off\n", __FUNCTION__);
			
			ret = dsp_cancel_init(dsp, 0,0,-1);
			dsp_cmx_hardware(dsp->conf, dsp);
			break;
		case BF_ENABLE_KEY: /* turn blowfish on */
			if (len<4 || len>56) {
				ret = -EINVAL;
				break;
			}
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: turn blowfish on (key not shown)\n", __FUNCTION__);
			ret = dsp_bf_init(dsp, (u8*)data, len);
			/* set new cont */
			if (!ret)
				cont = BF_ACCEPT;
			else
				cont = BF_REJECT;
			/* send indication if it worked to set it */
			nskb = create_link_skb(PH_CONTROL | INDICATION, 0, sizeof(int), &cont, 0);
			if (mISDN_queue_up(&dsp->inst, 0, nskb))
				dev_kfree_skb(nskb);
			if (!ret)
				dsp_cmx_hardware(dsp->conf, dsp);
			break;
		case BF_DISABLE: /* turn blowfish off */
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: turn blowfish off\n", __FUNCTION__);
			dsp_bf_cleanup(dsp);
			dsp_cmx_hardware(dsp->conf, dsp);
			break;
		default:
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: ctrl req %x unhandled\n", __FUNCTION__, cont);
			ret = -EINVAL;
	}
	if (!ret)
		dev_kfree_skb(skb);
	return(ret);
}


/*
 * messages from upper layers
 */
static int
dsp_from_up(mISDNinstance_t *inst, struct sk_buff *skb)
{
	dsp_t			*dsp;
	mISDN_head_t		*hh;
	int			ret = 0;
	u_long		flags;

	if (!skb)
		return(-EINVAL);
	dsp = inst->privat;
	if (!dsp) {
		return(-EIO);
	}

	hh = mISDN_HEAD_P(skb);
	switch(hh->prim) {
		case DL_DATA | RESPONSE:
		case PH_DATA | RESPONSE:
			/* ignore response */
			dev_kfree_skb(skb);
			break;
		case DL_DATA | REQUEST:
		case PH_DATA | REQUEST:
			if (skb->len < 1)
				return(-EINVAL);
			
			if (!dsp->conf_id) {
				/* PROCESS TONES/TX-DATA ONLY */
				if (dsp->tone.tone) {
					/* -> copy tone */
					dsp_tone_copy(dsp, skb->data, skb->len);
				}

				if (dsp->tx_volume)
			                dsp_change_volume(skb, dsp->tx_volume);
				/* cancel echo */
				if (dsp->cancel_enable)
					dsp_cancel_tx(dsp, skb->data, skb->len);
				/* crypt */
				if (dsp->bf_enable)
					dsp_bf_encrypt(dsp, skb->data, skb->len);
				/* send packet */
				if (mISDN_queue_down(&dsp->inst, 0, skb)) {
					dev_kfree_skb(skb);
					printk(KERN_ERR "%s: failed to send tx-packet\n", __FUNCTION__);

					return (-EIO);
				}

			} else {
				if (dsp->features.pcm_id>=0) {
					if (dsp_debug)
						printk("Not sending Data to CMX -- > returning because of HW bridge\n");
					dev_kfree_skb(skb);
					break;
				}
				/* send data to tx-buffer (if no tone is played) */
				spin_lock_irqsave(&dsp_obj.lock, flags);
				if (!dsp->tone.tone) {
					dsp_cmx_transmit(dsp, skb);
				} 
				spin_unlock_irqrestore(&dsp_obj.lock, flags);

				dev_kfree_skb(skb);
			}
			break;
		case PH_CONTROL | REQUEST:
			
			spin_lock_irqsave(&dsp_obj.lock, flags);
			ret = dsp_control_req(dsp, hh, skb);
			spin_unlock_irqrestore(&dsp_obj.lock, flags);
			
			break;
		case DL_ESTABLISH | REQUEST:
		case PH_ACTIVATE | REQUEST:
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: activating b_channel %s\n", __FUNCTION__, dsp->inst.name);
			
			if (dsp->dtmf.hardware || dsp->dtmf.software)
				dsp_dtmf_goertzel_init(dsp);
			hh->prim = PH_ACTIVATE | REQUEST;
			ret = mISDN_queue_down(&dsp->inst, 0, skb);
			
			break;
		case DL_RELEASE | REQUEST:
		case PH_DEACTIVATE | REQUEST:
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: releasing b_channel %s\n", __FUNCTION__, dsp->inst.name);
			
			dsp->tone.tone = dsp->tone.hardware = dsp->tone.software = 0;
			if (timer_pending(&dsp->tone.tl))
				del_timer(&dsp->tone.tl);
			hh->prim = PH_DEACTIVATE | REQUEST;
			ret = mISDN_queue_down(&dsp->inst, 0, skb);
			
			break;
		default:
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: msg %x unhandled %s\n", __FUNCTION__, hh->prim, dsp->inst.name);
			ret = -EINVAL;
			break;
	}
	return(ret);
}


/*
 * messages from lower layers
 */
static int
dsp_from_down(mISDNinstance_t *inst,  struct sk_buff *skb)
{
	dsp_t		*dsp;
	mISDN_head_t	*hh;
	int		ret = 0;
	u8		*digits;
	int 		cont;
	struct		sk_buff *nskb;
	u_long		flags;

	if (!skb)
		return(-EINVAL);
	dsp = inst->privat;
	if (!dsp)
		return(-EIO);

	hh = mISDN_HEAD_P(skb);
	switch(hh->prim)
	{
		case PH_DATA | CONFIRM:
		case DL_DATA | CONFIRM:
			/* flush response, because no relation to upper layer */
			dev_kfree_skb(skb);
			break;
		case PH_DATA | INDICATION:
		case DL_DATA | INDICATION:
			if (skb->len < 1)
				return(-EINVAL);

			
			
			/* decrypt if enabled */
			if (dsp->bf_enable)
				dsp_bf_decrypt(dsp, skb->data, skb->len);
			/* if echo cancellation is enabled */
			if (dsp->cancel_enable)
				dsp_cancel_rx(dsp, skb->data, skb->len);
			/* check if dtmf soft decoding is turned on */
			if (dsp->dtmf.software) {
				digits = dsp_dtmf_goertzel_decode(dsp, skb->data, skb->len, (dsp_options&DSP_OPT_ULAW)?1:0);
				if (digits) while(*digits) {
					if (dsp_debug & DEBUG_DSP_DTMF)
						printk(KERN_DEBUG "%s: sending software decoded digit(%c) to upper layer %s\n", __FUNCTION__, *digits, dsp->inst.name);
					cont = DTMF_TONE_VAL | *digits;
					nskb = create_link_skb(PH_CONTROL | INDICATION, 0, sizeof(int), &cont, 0);
					if (mISDN_queue_up(&dsp->inst, 0, nskb))
						dev_kfree_skb(nskb);
					digits++;
				}
			}
			/* change volume if requested */
			if (dsp->rx_volume)
				dsp_change_volume(skb, dsp->rx_volume);

			if (dsp->conf_id) {
				/* we need to process receive data if software */
				spin_lock_irqsave(&dsp_obj.lock, flags);
				if (dsp->pcm_slot_tx<0 && dsp->pcm_slot_rx<0) {
					/* process data from card at cmx */
					dsp_cmx_receive(dsp, skb);
				}
				spin_unlock_irqrestore(&dsp_obj.lock, flags);
			}

			if (dsp->rx_disabled) {
				/* if receive is not allowed */
				dev_kfree_skb(skb);
				
				break;
			}
			hh->prim = DL_DATA | INDICATION;
			ret = mISDN_queue_up(&dsp->inst, 0, skb);
			
			break;
		case PH_CONTROL | INDICATION:
			
			if (dsp_debug & DEBUG_DSP_DTMFCOEFF)
				printk(KERN_DEBUG "%s: PH_CONTROL received: %x (len %d) %s\n", __FUNCTION__, hh->dinfo, skb->len, dsp->inst.name);
			switch (hh->dinfo) {
				case HW_HFC_COEFF: /* getting coefficients */
				if (!dsp->dtmf.hardware) {
					if (dsp_debug & DEBUG_DSP_DTMFCOEFF)
						printk(KERN_DEBUG "%s: ignoring DTMF coefficients from HFC\n", __FUNCTION__);
					dev_kfree_skb(skb);
					break;
				}
				digits = dsp_dtmf_goertzel_decode(dsp, skb->data, skb->len, 2);
				if (digits) while(*digits) {
					int k;
					struct sk_buff *nskb;
					if (dsp_debug & DEBUG_DSP_DTMF)
						printk(KERN_DEBUG "%s: now sending software decoded digit(%c) to upper layer %s\n", __FUNCTION__, *digits, dsp->inst.name);
					k = *digits | DTMF_TONE_VAL;
					nskb = create_link_skb(PH_CONTROL | INDICATION, 0, sizeof(int), &k, 0);
					if (mISDN_queue_up(&dsp->inst, 0, nskb))
						dev_kfree_skb(nskb);
					digits++;
				}
				dev_kfree_skb(skb);
				break;

				case VOL_CHANGE_TX: /* change volume */
				if (skb->len != sizeof(int)) {
					ret = -EINVAL;
					break;
				}
				dsp->tx_volume = *((int *)skb->data);
				if (dsp_debug & DEBUG_DSP_CORE)
					printk(KERN_DEBUG "%s: change tx volume to %d\n", __FUNCTION__, dsp->tx_volume);
				printk(KERN_DEBUG "%s: change tx volume to %d\n", __FUNCTION__, dsp->tx_volume);
				dsp_cmx_hardware(dsp->conf, dsp);
				break;
				default:
				if (dsp_debug & DEBUG_DSP_CORE)
					printk(KERN_DEBUG "%s: ctrl ind %x unhandled %s\n", __FUNCTION__, hh->dinfo, dsp->inst.name);
				ret = -EINVAL;
			}
			
			break;
		case PH_ACTIVATE | CONFIRM:
			
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: b_channel is now active %s\n", __FUNCTION__, dsp->inst.name);
			/* bchannel now active */
			spin_lock_irqsave(&dsp_obj.lock, flags);
			dsp->b_active = 1;
			dsp->tx_W = dsp->tx_R = 0; /* clear TX buffer */
			dsp->rx_W = dsp->rx_R = -1; /* reset RX buffer */
			memset(dsp->rx_buff, 0, sizeof(dsp->rx_buff));
			dsp_cmx_hardware(dsp->conf, dsp);
			spin_unlock_irqrestore(&dsp_obj.lock, flags);
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: done with activation, sending confirm to user space. %s\n", __FUNCTION__, dsp->inst.name);
			/* send activation to upper layer */
			hh->prim = DL_ESTABLISH | CONFIRM;
			ret = mISDN_queue_up(&dsp->inst, 0, skb);
			
			break;
		case PH_DEACTIVATE | CONFIRM:
			
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: b_channel is now inactive %s\n", __FUNCTION__, dsp->inst.name);
			/* bchannel now inactive */
			spin_lock_irqsave(&dsp_obj.lock, flags);
			dsp->b_active = 0;
			dsp_cmx_hardware(dsp->conf, dsp);
			spin_unlock_irqrestore(&dsp_obj.lock, flags);
			hh->prim = DL_RELEASE | CONFIRM;
			ret = mISDN_queue_up(&dsp->inst, 0, skb);
			
			break;
		default:
			if (dsp_debug & DEBUG_DSP_CORE)
				printk(KERN_DEBUG "%s: msg %x unhandled %s\n", __FUNCTION__, hh->prim, dsp->inst.name);
			ret = -EINVAL;
	}
	return(ret);
}


/*
 * messages from queue
 */
static int
dsp_function(mISDNinstance_t *inst, struct sk_buff *skb)
{
	mISDN_head_t *hh;
	int ret = -EINVAL;

	hh = mISDN_HEAD_P(skb);
	switch (hh->addr & MSG_DIR_MASK) {
		case FLG_MSG_DOWN:
			ret = dsp_from_up(inst, skb);
			break;
		case FLG_MSG_UP:
			ret = dsp_from_down(inst, skb);
			break;
	}

	return(ret);
}


/*
 * desroy DSP instances
 */
static void
release_dsp(dsp_t *dsp)
{
	mISDNinstance_t	*inst = &dsp->inst;
	conference_t	*conf;
	u_long		flags;

	spin_lock_irqsave(&dsp_obj.lock, flags);
	if (timer_pending(&dsp->feature_tl))
		del_timer(&dsp->feature_tl);
	if (timer_pending(&dsp->tone.tl))
		del_timer(&dsp->tone.tl);
	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: removing conferences %s\n", __FUNCTION__, dsp->inst.name);
	conf = dsp->conf;
	if (conf) {
		dsp_cmx_del_conf_member(dsp);
		if (!list_empty(&conf->mlist)) {
			dsp_cmx_del_conf(conf);
		}
	}

	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: remove & destroy object %s\n", __FUNCTION__, dsp->inst.name);
	list_del(&dsp->list);
	spin_unlock_irqrestore(&dsp_obj.lock, flags);
	mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	vfree(dsp);

	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: dsp instance released\n", __FUNCTION__);
}


/*
 * ask for hardware features
 */
static void
dsp_feat(void *arg)
{
	dsp_t *dsp = arg;
	struct sk_buff *nskb;
	void *feat;
	

	switch (dsp->feature_state) {
		case FEAT_STATE_INIT:
			feat = &dsp->features;
			nskb = create_link_skb(PH_CONTROL | REQUEST, HW_FEATURES, sizeof(feat), &feat, 0);
			if (!nskb)
				break;
			if (mISDN_queue_down(&dsp->inst, 0, nskb)) {
				dev_kfree_skb(nskb);
				break;
			}
			if (dsp_debug & DEBUG_DSP_MGR)
				printk(KERN_DEBUG "%s: features will be quered now for instance %s\n", __FUNCTION__, dsp->inst.name);
			spin_lock(&dsp->feature_lock);
			dsp->feature_state = FEAT_STATE_WAIT;
			spin_unlock(&dsp->feature_lock);
			init_timer(&dsp->feature_tl);
			dsp->feature_tl.expires = jiffies + (HZ / 100);
			add_timer(&dsp->feature_tl);
			break;
		case FEAT_STATE_WAIT:
			if (dsp_debug & DEBUG_DSP_MGR)
				printk(KERN_DEBUG "%s: features of %s are: hfc_id=%d hfc_dtmf=%d hfc_loops=%d hfc_echocanhw:%d pcm_id=%d pcm_slots=%d pcm_banks=%d\n",
				 __FUNCTION__, dsp->inst.name,
				 dsp->features.hfc_id,
				 dsp->features.hfc_dtmf,
				 dsp->features.hfc_loops,
				 dsp->features.hfc_echocanhw,
				 dsp->features.pcm_id,
				 dsp->features.pcm_slots,
				 dsp->features.pcm_banks);

			spin_lock(&dsp->feature_lock);
			dsp->feature_state = FEAT_STATE_RECEIVED;
			spin_unlock(&dsp->feature_lock);

			if (dsp->queue_conf_id) {
				/*work on queued conf id*/
				dsp_cmx_conf(dsp, dsp->queue_conf_id );
				if (dsp_debug & DEBUG_DSP_CMX)
					dsp_cmx_debug(dsp);
			}

			if (dsp->queue_cancel[2]) {
				dsp_cancel_init(dsp, 
						dsp->queue_cancel[0],
						dsp->queue_cancel[1],
						dsp->queue_cancel[2]
					       );
						
			}
			break;
	}

}


/*
 * create new DSP instances
 */
static int
new_dsp(mISDNstack_t *st, mISDN_pid_t *pid) 
{
	int	err = 0;
	dsp_t	*ndsp;
	u_long	flags;

	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: creating new dsp instance\n", __FUNCTION__);

	if (!st || !pid)
		return(-EINVAL);
	if (!(ndsp = vmalloc(sizeof(dsp_t)))) {
		printk(KERN_ERR "%s: vmalloc dsp_t failed\n", __FUNCTION__);
		return(-ENOMEM);
	}
	memset(ndsp, 0, sizeof(dsp_t));
	memcpy(&ndsp->inst.pid, pid, sizeof(mISDN_pid_t));
	mISDN_init_instance(&ndsp->inst, &dsp_obj, ndsp, dsp_function);
	if (!mISDN_SetHandledPID(&dsp_obj, &ndsp->inst.pid)) {
		int_error();
		err = -ENOPROTOOPT;
		free_mem:
		vfree(ndsp);
		return(err);
	}
	sprintf(ndsp->inst.name, "DSP_S%x/C%x",
		(st->id&0xff00)>>8, (st->id&0xff0000)>>16);
	/* set frame size to start */
	ndsp->features.hfc_id = -1; /* current PCM id */
	ndsp->features.pcm_id = -1; /* current PCM id */
	ndsp->pcm_slot_rx = -1; /* current CPM slot */
	ndsp->pcm_slot_tx = -1;
	ndsp->pcm_bank_rx = -1;
	ndsp->pcm_bank_tx = -1;
	ndsp->hfc_conf = -1; /* current conference number */
	/* set tone timer */
	ndsp->tone.tl.function = (void *)dsp_tone_timeout;
	ndsp->tone.tl.data = (long) ndsp;
	init_timer(&ndsp->tone.tl);
	/* set dsp feture timer */
	ndsp->feature_tl.function = (void *)dsp_feat;
	ndsp->feature_tl.data = (long) ndsp;
	ndsp->feature_state = FEAT_STATE_INIT;

	if (dtmfthreshold < 20 || dtmfthreshold> 500) {
		dtmfthreshold=200;
	}
	ndsp->dtmf.treshold=dtmfthreshold*10000;

	spin_lock_init(&ndsp->feature_lock);
	init_timer(&ndsp->feature_tl);
	if (!(dsp_options & DSP_OPT_NOHARDWARE)) {
		ndsp->feature_tl.expires = jiffies + (HZ / 100);
		add_timer(&ndsp->feature_tl);
	}
	spin_lock_irqsave(&dsp_obj.lock, flags);
	/* append and register */
	list_add_tail(&ndsp->list, &dsp_obj.ilist);
	spin_unlock_irqrestore(&dsp_obj.lock, flags);
	err = mISDN_ctrl(st, MGR_REGLAYER | INDICATION, &ndsp->inst);
	if (err) {
		printk(KERN_ERR "%s: failed to register layer %s\n", __FUNCTION__, ndsp->inst.name);
		spin_lock_irqsave(&dsp_obj.lock, flags);
		list_del(&ndsp->list);
		spin_unlock_irqrestore(&dsp_obj.lock, flags);
		goto free_mem;
	}
	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: dsp instance created %s\n", __FUNCTION__, ndsp->inst.name);
	return(err);
}


/*
 * manager for DSP instances
 */
static int
dsp_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t	*inst = data;
	dsp_t		*dspl;
	int		ret = -EINVAL;
	u_long		flags;

	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: data:%p prim:%x arg:%p\n", __FUNCTION__, data, prim, arg);
	if (!data)
		return(ret);
	spin_lock_irqsave(&dsp_obj.lock, flags);
	list_for_each_entry(dspl, &dsp_obj.ilist, list) {
		if (&dspl->inst == inst) {
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&dsp_obj.lock, flags);
	if (ret && (prim != (MGR_NEWLAYER | REQUEST))) {
		printk(KERN_WARNING "%s: given instance(%p) not in ilist.\n", __FUNCTION__, data);
		return(ret);
	}

	switch(prim) {
	    case MGR_NEWLAYER | REQUEST:
		ret = new_dsp(data, arg);
		break;
	    case MGR_SETSTACK | INDICATION:
		break;
#ifdef OBSOLETE
	    case MGR_CONNECT | REQUEST:
		ret = mISDN_ConnectIF(inst, arg);
		break;
	    case MGR_SETIF | REQUEST:
	    case MGR_SETIF | INDICATION:
		ret = mISDN_SetIF(inst, arg, prim, dsp_from_up, dsp_from_down, dspl);
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		ret = mISDN_DisConnectIF(inst, arg);
		break;
#endif
	    case MGR_UNREGLAYER | REQUEST:
	    case MGR_RELEASE | INDICATION:
		if (dsp_debug & DEBUG_DSP_MGR)
			printk(KERN_DEBUG "%s: release_dsp id %x\n", __FUNCTION__, dspl->inst.st->id);

	    	release_dsp(dspl);
	    	break;
	    default:
		printk(KERN_WARNING "%s: prim %x not handled\n", __FUNCTION__, prim);
		ret = -EINVAL;
		break;
	}
	return(ret);
}


/*
 * initialize DSP object
 */
static int dsp_init(void)
{
	int err;

	/* copy variables */
	dsp_options = options;
	dsp_debug = debug;

	/* display revision */
	printk(KERN_INFO "mISDN_dsp: Audio DSP  Rev. %s (debug=0x%x) EchoCancellor %s dtmfthreshold(%d)\n", mISDN_getrev(dsp_revision), debug, EC_TYPE, dtmfthreshold);

	/* set packet size */
	if (poll == 0) {
		if (HZ == 100)
			poll = 80;
		else
			poll = 64;
	}

	if (poll > MAX_POLL) {
		printk(KERN_ERR "%s: Wrong poll value (%d), using %d.\n", __FUNCTION__, poll, MAX_POLL);
		poll = MAX_POLL;
	}
	if (poll < 8) {
		printk(KERN_ERR "%s: Wrong poll value (%d), using 8.\n", __FUNCTION__, poll);
		poll = 8;
	}
	dsp_poll = poll;
	dsp_tics = poll * HZ / 8000;
	if (dsp_tics * 8000 == poll * HZ) 
		printk(KERN_INFO "mISDN_dsp: DSP clocks every %d samples. This equals %d jiffies.\n", poll, dsp_tics);
	else {
		printk(KERN_INFO "mISDN_dsp: Cannot clock ever %d samples. Use a multiple of %d (samples)\n", poll, 8000 / HZ);
		err = -EINVAL;
		return(err);
	}

	/* fill mISDN object (dsp_obj) */
	memset(&dsp_obj, 0, sizeof(dsp_obj));
#ifdef MODULE 
#ifdef SET_MODULE_OWNER
	SET_MODULE_OWNER(&dsp_obj);
#endif
#endif
	spin_lock_init(&dsp_obj.lock);
	dsp_obj.name = DSPName;
	dsp_obj.BPROTO.protocol[3] = ISDN_PID_L3_B_DSP;
	dsp_obj.own_ctrl = dsp_manager;
	INIT_LIST_HEAD(&dsp_obj.ilist);

	/* initialize audio tables */
	dsp_audio_generate_law_tables();
	dsp_silence = (dsp_options&DSP_OPT_ULAW)?0xff:0x2a;
	dsp_audio_law_to_s32 = (dsp_options&DSP_OPT_ULAW)?dsp_audio_ulaw_to_s32:dsp_audio_alaw_to_s32;
	dsp_audio_generate_s2law_table();
	dsp_audio_generate_seven();
	dsp_audio_generate_mix_table();
	if (dsp_options & DSP_OPT_ULAW)
		dsp_audio_generate_ulaw_samples();
	dsp_audio_generate_volume_changes();

	/* register object */
	if ((err = mISDN_register(&dsp_obj))) {
		printk(KERN_ERR "mISDN_dsp: Can't register %s error(%d)\n", DSPName, err);
		return(err);
	}

	/* set sample timer */
	dsp_spl_tl.function = (void *)dsp_cmx_send;
	dsp_spl_tl.data = 0;
	init_timer(&dsp_spl_tl);
	dsp_spl_tl.expires = jiffies + dsp_tics + 1; /* safer */
	dsp_spl_jiffies = dsp_spl_tl.expires;
	add_timer(&dsp_spl_tl);
	
	mISDN_module_register(THIS_MODULE);
	
	return(0);
}


/*
 * cleanup DSP object during module removal
 */
static void dsp_cleanup(void)
{
	dsp_t	*dspl, *nd;	
	int	err;

	mISDN_module_unregister(THIS_MODULE);

	if (timer_pending(&dsp_spl_tl))
		del_timer(&dsp_spl_tl);

	if (dsp_debug & DEBUG_DSP_MGR)
		printk(KERN_DEBUG "%s: removing module\n", __FUNCTION__);

	if ((err = mISDN_unregister(&dsp_obj))) {
		printk(KERN_ERR "mISDN_dsp: Can't unregister Audio DSP error(%d)\n", 
			err);
	}
	if (!list_empty(&dsp_obj.ilist)) {
		printk(KERN_WARNING "mISDN_dsp: Audio DSP object inst list not empty.\n");
		list_for_each_entry_safe(dspl, nd, &dsp_obj.ilist, list)
			release_dsp(dspl);
	}
	if (!list_empty(&Conf_list)) {
		printk(KERN_ERR "mISDN_dsp: Conference list not empty. Not all memory freed.\n");
	}
}

#ifdef MODULE
module_init(dsp_init);
module_exit(dsp_cleanup);
#endif
