/*
 * This file is part of hci_h4p bluetooth driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.
 *
 * Contact: Ville Tervo <ville.tervo@nokia.com>
 * Hacked by Gražvydas Ignotas for Pandora handheld console
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/skbuff.h>

#include "hci_h4p.h"

void hci_h4p_brf6150_parse_fw_event(struct hci_h4p_info *info,
				    struct sk_buff *skb)
{
	struct hci_fw_event *ev = NULL;
	int err = 0;

	if (bt_cb(skb)->pkt_type != H4_EVT_PKT) {
		dev_err(info->dev, "Got non event fw packet.\n");
		err = -EPROTO;
		goto ret;
	}

	ev = (struct hci_fw_event *)skb->data;
	if (ev->hev.evt != HCI_EV_CMD_COMPLETE) {
		dev_err(info->dev, "Got non cmd complete fw event\n");
		err = -EPROTO;
		goto ret;
	}

	if (ev->status != 0) {
		dev_err(info->dev, "Got error status from fw command\n");
		err = -EPROTO;
		goto ret;
	}

	NBT_DBG("fw_event, opcode %04x\n", ev->cmd.opcode);
ret:
	if (unlikely(ev != NULL && ev->cmd.opcode == 0xff36)) {
		/* speed change command */
		info->init_error = err;
		complete(&info->init_completion);
	} else {
		info->fw_error = err;
		complete(&info->fw_completion);
	}
	kfree_skb(skb);
}

int hci_h4p_brf6150_send_fw(struct hci_h4p_info *info, struct sk_buff_head *fw_queue)
{
	struct sk_buff *skb;

	info->fw_error = 0;

	while ((skb = skb_dequeue(fw_queue)) != NULL) {
#if 0
		/* We should allways send word aligned data to h4+ devices */
		int err = 0;
		if (skb->len % 2) {
			err = skb_pad(skb, 1);
		}
		if (err)
			return err;
#endif
		init_completion(&info->fw_completion);
		skb_queue_tail(&info->txq, skb);
		tasklet_schedule(&info->tx_task);

		if (!wait_for_completion_timeout(&info->fw_completion, HZ)) {
			dev_err(info->dev, "Timeout while sending fw\n");
			return -ETIMEDOUT;
		}

		if (info->fw_error) {
			dev_err(info->dev, "There was fw_error while sending fw\n");
			return -EPROTO;
		}
	}
	NBT_DBG_FW("Firmware sent\n");

	return 0;
}
