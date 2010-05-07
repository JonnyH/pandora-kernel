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

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/serial_reg.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/irqs.h>
#include <mach/pm.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#include "hci_h4p.h"

#define PM_TIMEOUT 200
#define BAUD_RATE  3686400

/* This should be used in function that cannot release clocks */
static void hci_h4p_set_clk(struct hci_h4p_info *info, int *clock, int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&info->clocks_lock, flags);
	if (enable && !*clock) {
		NBT_DBG_POWER("Enabling %p\n", clock);
		clk_enable(info->uart_fclk);
		if (cpu_class_is_omap2()) {
			clk_enable(info->uart_iclk);
			omap2_block_sleep();
		}
	}
	if (!enable && *clock) {
		NBT_DBG_POWER("Disabling %p\n", clock);
		clk_disable(info->uart_fclk);
		if (cpu_class_is_omap2()) {
			clk_disable(info->uart_iclk);
			omap2_allow_sleep();
		}
	}

	*clock = enable;
	spin_unlock_irqrestore(&info->clocks_lock, flags);
}

/* Power management functions */
static void hci_h4p_disable_tx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	NBT_DBG_POWER("\n");
	mod_timer(&info->tx_pm_timer, jiffies + msecs_to_jiffies(PM_TIMEOUT));
}

static void hci_h4p_enable_tx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	NBT_DBG_POWER("\n");
	del_timer_sync(&info->tx_pm_timer);
	if (info->tx_pm_enabled) {
		info->tx_pm_enabled = 0;
		hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
#if 0
		gpio_set_value(info->bt_wakeup_gpio, 1);
#endif
	}
}

static void hci_h4p_tx_pm_timer(unsigned long data)
{
	struct hci_h4p_info *info;

	NBT_DBG_POWER("\n");

	info = (struct hci_h4p_info *)data;

	if (hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT) {
#if 0
		gpio_set_value(info->bt_wakeup_gpio, 0);
#endif
		hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
		info->tx_pm_enabled = 1;
	}
	else {
		mod_timer(&info->tx_pm_timer, jiffies + msecs_to_jiffies(PM_TIMEOUT));
	}
}

static void hci_h4p_disable_rx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	mod_timer(&info->rx_pm_timer, jiffies + msecs_to_jiffies(PM_TIMEOUT));
}

static void hci_h4p_enable_rx(struct hci_h4p_info *info)
{
	unsigned long flags;

	if (!info->pm_enabled)
		return;

	del_timer_sync(&info->rx_pm_timer);
	spin_lock_irqsave(&info->lock, flags);
	if (info->rx_pm_enabled) {
		hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
		hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) | UART_IER_RDI);
		__hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
		info->rx_pm_enabled = 0;
	}
	spin_unlock_irqrestore(&info->lock, flags);
}

static void hci_h4p_rx_pm_timer(unsigned long data)
{
	unsigned long flags;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	spin_lock_irqsave(&info->lock, flags);
	if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_DR)) {
		__hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
		hci_h4p_set_rts(info, 0);
		hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) & ~UART_IER_RDI);
		hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
		info->rx_pm_enabled = 1;
	}
	else {
		mod_timer(&info->rx_pm_timer, jiffies + msecs_to_jiffies(PM_TIMEOUT));
	}
	spin_unlock_irqrestore(&info->lock, flags);
}

#ifdef CONFIG_LEDS_TRIGGERS
static void hci_h4p_led_blink(unsigned long data)
{
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	if (!time_after(jiffies, info->led_next_change))
		return;

	info->led_next_change = jiffies + msecs_to_jiffies(250);
	info->led_state = !info->led_state;

	led_trigger_event(info->led, info->led_state ? LED_FULL : LED_OFF);
	
	if (!info->led_state)
		mod_timer(&info->led_timer, jiffies + msecs_to_jiffies(300));
}
#else
#define hci_h4p_led_blink(x) do {} while(0)
#endif

/* Negotiation functions */
int hci_h4p_send_alive_packet(struct hci_h4p_info *info)
{
	NBT_DBG("Sending alive packet\n");

	if (!info->alive_cmd_skb)
		return -EINVAL;

	/* Keep reference to buffer so we can reuse it */
	info->alive_cmd_skb = skb_get(info->alive_cmd_skb);

	skb_queue_tail(&info->txq, info->alive_cmd_skb);
	tasklet_schedule(&info->tx_task);

	NBT_DBG("Alive packet sent\n");

	return 0;
}

static void hci_h4p_alive_packet(struct hci_h4p_info *info, struct sk_buff *skb)
{
	NBT_DBG("Received alive packet\n");
	if (skb->data[1] == 0xCC) {
		complete(&info->init_completion);
	}

	kfree_skb(skb);
}

struct speed_change_cmd_t {
	__u8 uart_prefix;
	struct hci_command_hdr hci_hdr;
	__le32 speed;
} __attribute__((packed));

static int hci_h4p_send_speed(struct hci_h4p_info *info, int speed)
{
	struct speed_change_cmd_t cmd;
	struct sk_buff *skb;
	int i;

	NBT_DBG("Sending speed change command..\n");

	info->init_error = 0;
	init_completion(&info->init_completion);

	cmd.uart_prefix = HCI_COMMAND_PKT;
	cmd.hci_hdr.opcode = 0xff36;
	cmd.hci_hdr.plen = sizeof(cmd.speed);
	cmd.speed = speed;

	for (i = 0; i < 3; i++) {
		skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
		if (skb == NULL)
			return -ENOMEM;

		memcpy(skb_put(skb, sizeof(cmd)), &cmd, sizeof(cmd));

		skb_queue_tail(&info->txq, skb);
		tasklet_schedule(&info->tx_task);

		if (wait_for_completion_interruptible_timeout(
			    &info->init_completion, msecs_to_jiffies(100))) {
			NBT_DBG("Got speed change response after "
				"%d try(-ies)\n", i+1);
			return info->init_error;
		}
	}

	return -ETIMEDOUT;
}

/* H4 packet handling functions */
static int hci_h4p_get_hdr_len(struct hci_h4p_info *info, u8 pkt_type)
{
	long retval;

	switch (pkt_type) {
	case H4_EVT_PKT:
		retval = HCI_EVENT_HDR_SIZE;
		break;
	case H4_ACL_PKT:
		retval = HCI_ACL_HDR_SIZE;
		break;
	case H4_SCO_PKT:
		retval = HCI_SCO_HDR_SIZE;
		break;
	case H4_NEG_PKT:
		retval = 11;
		break;
	case H4_ALIVE_PKT:
		retval = 3;
		break;
	default:
		dev_err(info->dev, "Unknown H4 packet type 0x%.2x\n", pkt_type);
		retval = -1;
		break;
	}

	return retval;
}

static unsigned int hci_h4p_get_data_len(struct hci_h4p_info *info,
					 struct sk_buff *skb)
{
	long retval = -1;
	struct hci_event_hdr *evt_hdr;
	struct hci_acl_hdr *acl_hdr;
	struct hci_sco_hdr *sco_hdr;

	switch (bt_cb(skb)->pkt_type) {
	case H4_EVT_PKT:
		evt_hdr = (struct hci_event_hdr *)skb->data;
		retval = evt_hdr->plen;
		break;
	case H4_ACL_PKT:
		acl_hdr = (struct hci_acl_hdr *)skb->data;
		retval = le16_to_cpu(acl_hdr->dlen);
		break;
	case H4_SCO_PKT:
		sco_hdr = (struct hci_sco_hdr *)skb->data;
		retval = sco_hdr->dlen;
		break;
	case H4_NEG_PKT:
		retval = 0;
		break;
	case H4_ALIVE_PKT:
		retval = 0;
		break;
	}

	return retval;
}

static inline void hci_h4p_recv_frame(struct hci_h4p_info *info,
				      struct sk_buff *skb)
{

	if (unlikely(!test_bit(HCI_RUNNING, &info->hdev->flags))) {
		hci_h4p_parse_fw_event(info, skb);
	} else {
		hci_recv_frame(skb);
		NBT_DBG("Frame sent to upper layer\n");
	}
}

static void hci_h4p_rx_flush(struct hci_h4p_info *info)
{
	while (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR)
		hci_h4p_inb(info, UART_RX);
	if (info->rx_skb != NULL)
		kfree_skb(info->rx_skb);
	info->rx_skb = NULL;
	info->rx_state = WAIT_FOR_PKT_TYPE;
	info->hdev->stat.err_rx++;
}

static void hci_h4p_rx_tasklet(unsigned long data)
{
	u8 byte;
	unsigned long flags;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;
	int count = 0, i;

	NBT_DBG_TRANSFER("rx tasklet\n");

	if (info->rx_skb != NULL && info->rx_timed_out) {
		kfree_skb(info->rx_skb);
		info->rx_skb = NULL;
	}
	info->rx_timed_out = 0;
	info->rx_calls++;

	while (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR) {
		if (info->rx_skb == NULL) {
			hci_h4p_led_blink((unsigned long)info);
			info->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE,
				GFP_ATOMIC | GFP_DMA);
			if (!info->rx_skb) {
				dev_err(info->dev, "Can't allocate memory "
					"for new packet\n");
				hci_h4p_rx_flush(info);
				break;
			}
			info->rx_skb->dev = (void *)info->hdev;
			info->rx_state = WAIT_FOR_PKT_TYPE;
		}

		if (info->rx_state == WAIT_FOR_PKT_TYPE) {
			byte = hci_h4p_inb(info, UART_RX);
			count++;
			bt_cb(info->rx_skb)->pkt_type = byte;
			info->rx_count = hci_h4p_get_hdr_len(info, byte);
			if (info->rx_count < 0) {
				hci_h4p_rx_flush(info);
				break;
			}

			info->rx_calls = 1;
			info->rx_state = WAIT_FOR_HEADER;
		}

		if (info->rx_state == WAIT_FOR_HEADER) {
			while (info->rx_count > 0) {
				if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_DR))
					goto end;
				*skb_put(info->rx_skb, 1) = hci_h4p_inb(info, UART_RX);
				info->rx_count--;
				count++;
			}

			info->rx_count = hci_h4p_get_data_len(info, info->rx_skb);
			if (unlikely(info->rx_count > skb_tailroom(info->rx_skb) ||
						info->rx_count < 0)) {
				dev_err(info->dev, "Frame too long or can't "
					"determine size.\n");
				hci_h4p_rx_flush(info);
				break;
			}

			info->rx_state = WAIT_FOR_DATA;
		}

		if (info->rx_state == WAIT_FOR_DATA) {
			while (info->rx_count > 0) {
				if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_DR))
					goto end;
				*skb_put(info->rx_skb, 1) = hci_h4p_inb(info, UART_RX);
				info->rx_count--;
				count++;
			}

			NBT_DBG("rx done, type %02x, len %i, calls %i\n",
				bt_cb(info->rx_skb)->pkt_type,
				info->rx_skb->len + 1, info->rx_calls);

			NBT_DBG_TRANSFER_NF("  rx: %02x", bt_cb(info->rx_skb)->pkt_type);
			for (i = 0; i < info->rx_skb->len; i++)
				NBT_DBG_TRANSFER_NF(" %02x", info->rx_skb->data[i]);
			NBT_DBG_TRANSFER_NF("\n");

			/* handle the data */
			if (bt_cb(info->rx_skb)->pkt_type == H4_ALIVE_PKT)
				hci_h4p_alive_packet(info, info->rx_skb);
			else
				hci_h4p_recv_frame(info, info->rx_skb);

			info->rx_state = WAIT_FOR_PKT_TYPE;
			info->rx_skb = NULL;
		}
	}

end:
	info->hdev->stat.byte_rx += count;
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) | UART_IER_RDI);
	spin_unlock_irqrestore(&info->lock, flags);
	NBT_DBG_TRANSFER("rx end\n");
}

static void hci_h4p_tx_tasklet(unsigned long data)
{
	unsigned int sent = 0;
	unsigned long flags;
	struct sk_buff *skb;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	NBT_DBG_TRANSFER("tx_tasklet woke up\n data ");

	skb = skb_dequeue(&info->txq);
	if (!skb) {
		/* No data in buffer */
		NBT_DBG_TRANSFER("skb ready\n");
		hci_h4p_disable_tx(info);
		hci_h4p_led_blink((unsigned long)info);
		return;
	}

	NBT_DBG("tx %p %d bytes\n", skb, skb->len);

	/* there hould be no rx in progress at this point */
	if (info->rx_skb != NULL) {
		dev_err(info->dev, "Receive timed out, %i done, %li left, "
			"LSR=%02x, IER=%02x\n", info->rx_skb->len + 1,
			info->rx_count, hci_h4p_inb(info, UART_LSR),
			hci_h4p_inb(info, UART_IER));
		info->rx_timed_out = 1;
	}

	/* Copy data to tx fifo */
	while (!(hci_h4p_inb(info, UART_OMAP_SSR) & UART_OMAP_SSR_TXFULL) &&
	       (sent < skb->len)) {
		NBT_DBG_TRANSFER_NF("0x%.2x ", skb->data[sent]);
		hci_h4p_outb(info, UART_TX, skb->data[sent]);
		sent++;
	}

	info->hdev->stat.byte_tx += sent;
	NBT_DBG_TRANSFER_NF("\n");
	if (skb->len == sent) {
		kfree_skb(skb);
	} else {
		skb_pull(skb, sent);
		skb_queue_head(&info->txq, skb);
	}

	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) | UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);
}

static irqreturn_t hci_h4p_interrupt(int irq, void *data)
{
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;
	unsigned long flags;
	u8 iir;

	iir = hci_h4p_inb(info, UART_IIR);
	if (iir & UART_IIR_NO_INT) {
		/* why does this happen from time to time? */
		NBT_DBG("Interrupt but no reason irq 0x%.2x, "
			"LSR=%02x, IER=%02x\n",iir,hci_h4p_inb(info, UART_LSR),
			hci_h4p_inb(info, UART_IER));
		goto end;
	}

	iir &= 0x3e;

	if (iir == UART_IIR_RDI || iir == UART_IIR_THRI)
		NBT_DBG_TRANSFER("In interrupt handler iir 0x%.2x\n", iir);
	else
		NBT_DBG("irq 0x%.2x, LSR=%02x, IER=%02x\n", iir,
			hci_h4p_inb(info, UART_LSR), hci_h4p_inb(info, UART_IER));

	switch (iir) {
		case UART_IIR_MSI:
			hci_h4p_inb(info, UART_MSR);
			break;
		case UART_IIR_RLSI:
			hci_h4p_inb(info, UART_RX);
			hci_h4p_inb(info, UART_LSR);
			break;

		case 0x0c: /* rx timeout, same mask? */
		case UART_IIR_RDI:
			spin_lock_irqsave(&info->lock, flags);
			hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) & ~UART_IER_RDI);
			spin_unlock_irqrestore(&info->lock, flags);
			break;

		case UART_IIR_THRI:
			spin_lock_irqsave(&info->lock, flags);
			hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) & ~UART_IER_THRI);
			spin_unlock_irqrestore(&info->lock, flags);
			tasklet_schedule(&info->tx_task);
			return IRQ_HANDLED;	/* don't schedule read yet */
	}

end:
	/* if we have data to read, schedule read,
	 * ignoring real reason we were called here.
	 */
	if (iir == UART_IIR_RDI || hci_h4p_inb(info, UART_LSR) & UART_LSR_DR)
		tasklet_schedule(&info->rx_task);

	return IRQ_HANDLED;
}

#if 0
static irqreturn_t hci_h4p_wakeup_interrupt(int irq, void *dev_inst)
{
	struct hci_h4p_info *info = dev_inst;
	int should_wakeup;
	struct hci_dev *hdev;

	if (!info->hdev)
		return IRQ_HANDLED;

	hdev = info->hdev;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return IRQ_HANDLED;

	should_wakeup = gpio_get_value(info->host_wakeup_gpio);
	NBT_DBG_POWER("gpio interrupt %d\n", should_wakeup);
	if (should_wakeup) {
		hci_h4p_enable_rx(info);
	} else {
		hci_h4p_disable_rx(info);
	}

	return IRQ_HANDLED;
}
#endif

static int hci_h4p_reset(struct hci_h4p_info *info)
{
	int err;

	gpio_set_value(info->reset_gpio, 0);

	/* reset needs at least 10ms */
	msleep(20);

	hci_h4p_init_uart(info);
	hci_h4p_change_speed(info, 115200);
	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_CTS | UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);

#if 0
	gpio_set_value(info->bt_wakeup_gpio, 1);
#endif
	gpio_set_value(info->reset_gpio, 1);
	msleep(80);

	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err < 0) {
		dev_err(info->dev, "No cts from bt chip\n");
		return err;
	}
	msleep(20);	/* just in case response was too early */

	return 0;
}

/* hci callback functions */
static int hci_h4p_hci_flush(struct hci_dev *hdev)
{
	struct hci_h4p_info *info;
	info = hdev->driver_data;

	skb_queue_purge(&info->txq);

	return 0;
}

static int hci_h4p_hci_open(struct hci_dev *hdev)
{
	struct hci_h4p_info *info;
	int err;
	struct sk_buff_head fw_queue;

	info = hdev->driver_data;

	if (test_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	skb_queue_head_init(&fw_queue);
	err = hci_h4p_read_fw(info, &fw_queue);
	if (err < 0) {
		dev_err(info->dev, "Cannot read firmware\n");
		return err;
	}

	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);

	tasklet_enable(&info->tx_task);
	tasklet_enable(&info->rx_task);
	info->rx_state = WAIT_FOR_PKT_TYPE;
	info->rx_count = 0;
	info->garbage_bytes = 0;
	info->rx_skb = NULL;
	info->pm_enabled = 0;
	init_completion(&info->fw_completion);

	err = hci_h4p_reset(info);
	if (err < 0)
		goto err_clean;

	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_CTS | UART_EFR_RTS);

	err = hci_h4p_send_speed(info, BAUD_RATE);
	if (err < 0) {
		dev_err(info->dev, "Failed to set chip baud rate "
			"to %d\n", BAUD_RATE);
		goto err_clean;
	}

	/* Change to operational settings */
	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_CTS | UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);
	hci_h4p_change_speed(info, BAUD_RATE);

	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err < 0) {
		dev_err(info->dev, "No CTS after speed change.\n");
		goto err_clean;
	}
	msleep(10);	/* just to be sure both sides changed rate */

	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_CTS | UART_EFR_RTS);

	err = hci_h4p_send_fw(info, &fw_queue);
	if (err < 0) {
		dev_err(info->dev, "Sending firmware failed.\n");
		goto err_clean;
	}

	kfree_skb(info->alive_cmd_skb);
	info->alive_cmd_skb = NULL;
	/* TODO: fix PM */
//	info->pm_enabled = 1;
	info->tx_pm_enabled = 1;
	info->rx_pm_enabled = 0;
	set_bit(HCI_RUNNING, &hdev->flags);
	led_trigger_event(info->led, LED_FULL);
	dev_info(info->dev, "BT hci up and running.\n");

	return 0;

err_clean:
	hci_h4p_hci_flush(hdev);
	tasklet_disable(&info->tx_task);
	tasklet_disable(&info->rx_task);
	hci_h4p_reset_uart(info);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	gpio_set_value(info->reset_gpio, 0);
#if 0
	gpio_set_value(info->bt_wakeup_gpio, 0);
#endif
	skb_queue_purge(&fw_queue);
#if 0
	kfree_skb(neg_cmd_skb);
	neg_cmd_skb = NULL;
#endif
	kfree_skb(info->alive_cmd_skb);
	info->alive_cmd_skb = NULL;
	kfree_skb(info->rx_skb);

	return err;
}

static int hci_h4p_hci_close(struct hci_dev *hdev)
{
	struct hci_h4p_info *info = hdev->driver_data;

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	hci_h4p_hci_flush(hdev);
	del_timer_sync(&info->tx_pm_timer);
	del_timer_sync(&info->rx_pm_timer);
	tasklet_disable(&info->tx_task);
	tasklet_disable(&info->rx_task);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	hci_h4p_reset_uart(info);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	gpio_set_value(info->reset_gpio, 0);
#if 0
	gpio_set_value(info->bt_wakeup_gpio, 0);
#endif
	kfree_skb(info->rx_skb);
#ifdef CONFIG_LEDS_TRIGGERS
	del_timer_sync(&info->led_timer);
	led_trigger_event(info->led, LED_OFF);
#endif
	dev_info(info->dev, "BT hci down.\n");

	return 0;
}

static void hci_h4p_hci_destruct(struct hci_dev *hdev)
{
}

static int hci_h4p_hci_send_frame(struct sk_buff *skb)
{
	struct hci_h4p_info *info;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;

	if (!hdev) {
		printk(KERN_WARNING "hci_h4p: Frame for unknown device\n");
		return -ENODEV;
	}

	NBT_DBG("dev %p, skb %p\n", hdev, skb);

	info = hdev->driver_data;

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		dev_warn(info->dev, "Frame for non-running device\n");
		return -EIO;
	}

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}

	/* Push frame type to skb */
	*skb_push(skb, 1) = (bt_cb(skb)->pkt_type);
	/* We should allways send word aligned data to h4+ devices */
#if 0
	if (skb->len % 2) {
		int err = 0;
		err = skb_pad(skb, 1);
		if (err)
			return err;
	}
#endif
	hci_h4p_enable_tx(info);
	skb_queue_tail(&info->txq, skb);
	tasklet_schedule(&info->tx_task);

	return 0;
}

static int hci_h4p_hci_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int hci_h4p_register_hdev(struct hci_h4p_info *info)
{
	struct hci_dev *hdev;

	/* Initialize and register HCI device */

	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(info->dev, "Can't allocate memory for device\n");
		return -ENOMEM;
	}
	info->hdev = hdev;

	hdev->type = HCI_UART;
	hdev->driver_data = info;

	hdev->open = hci_h4p_hci_open;
	hdev->close = hci_h4p_hci_close;
	hdev->flush = hci_h4p_hci_flush;
	hdev->send = hci_h4p_hci_send_frame;
	hdev->destruct = hci_h4p_hci_destruct;
	hdev->ioctl = hci_h4p_hci_ioctl;

	hdev->owner = THIS_MODULE;

	if (hci_register_dev(hdev) < 0) {
		dev_err(info->dev, "hci_h4p: Can't register HCI device %s.\n", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static int hci_h4p_probe(struct platform_device *pdev)
{
	struct omap_bluetooth_config *bt_config;
	struct hci_h4p_info *info;
	int irq, err;

	dev_info(&pdev->dev, "Registering HCI H4P device\n");
	info = kzalloc(sizeof(struct hci_h4p_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->pm_enabled = 0;
	info->tx_pm_enabled = 0;
	info->rx_pm_enabled = 0;
	info->garbage_bytes = 0;
	info->tx_clocks_en = 0;
	info->rx_clocks_en = 0;
	tasklet_init(&info->tx_task, hci_h4p_tx_tasklet, (unsigned long)info);
	tasklet_init(&info->rx_task, hci_h4p_rx_tasklet, (unsigned long)info);
	/* hci_h4p_hci_open assumes that tasklet is disabled in startup */
	tasklet_disable(&info->tx_task);
	tasklet_disable(&info->rx_task);
	spin_lock_init(&info->lock);
	spin_lock_init(&info->clocks_lock);
	skb_queue_head_init(&info->txq);
	init_timer(&info->tx_pm_timer);
	info->tx_pm_timer.function = hci_h4p_tx_pm_timer;
	info->tx_pm_timer.data = (unsigned long)info;
	init_timer(&info->rx_pm_timer);
	info->rx_pm_timer.function = hci_h4p_rx_pm_timer;
	info->rx_pm_timer.data = (unsigned long)info;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "Could not get Bluetooth config data\n");
		return -ENODATA;
	}

	bt_config = pdev->dev.platform_data;
	info->chip_type = bt_config->chip_type;
	info->bt_wakeup_gpio = bt_config->bt_wakeup_gpio;
	info->host_wakeup_gpio = bt_config->host_wakeup_gpio;
	info->reset_gpio = bt_config->reset_gpio;
	info->bt_sysclk = bt_config->bt_sysclk;

	NBT_DBG("RESET gpio: %d\n", info->reset_gpio);
	NBT_DBG("BTWU gpio: %d\n", info->bt_wakeup_gpio);
	NBT_DBG("HOSTWU gpio: %d\n", info->host_wakeup_gpio);
	NBT_DBG("Uart: %d\n", bt_config->bt_uart);
	NBT_DBG("sysclk: %d\n", info->bt_sysclk);

	err = omap_request_gpio(info->reset_gpio);
	if (err < 0) {
		dev_err(&pdev->dev, "Cannot get GPIO line %d\n",
			info->reset_gpio);
		kfree(info);
		goto cleanup;
	}

#if 0
	err = omap_request_gpio(info->bt_wakeup_gpio);
	if (err < 0)
	{
		dev_err(info->dev, "Cannot get GPIO line 0x%d",
			info->bt_wakeup_gpio);
		omap_free_gpio(info->reset_gpio);
		kfree(info);
		goto cleanup;
	}

	err = omap_request_gpio(info->host_wakeup_gpio);
	if (err < 0)
	{
		dev_err(info->dev, "Cannot get GPIO line %d",
		       info->host_wakeup_gpio);
		omap_free_gpio(info->reset_gpio);
		omap_free_gpio(info->bt_wakeup_gpio);
		kfree(info);
		goto cleanup;
	}

	gpio_direction_output(info->bt_wakeup_gpio, 0);
	gpio_direction_input(info->host_wakeup_gpio);
#endif
	gpio_direction_output(info->reset_gpio, 0);

	switch (bt_config->bt_uart) {
	case 1:
		if (cpu_is_omap16xx()) {
			irq = INT_UART1;
			info->uart_fclk = clk_get(NULL, "uart1_ck");
		} else if (cpu_class_is_omap2()) {
			irq = INT_24XX_UART1_IRQ;
			info->uart_iclk = clk_get(NULL, "uart1_ick");
			info->uart_fclk = clk_get(NULL, "uart1_fck");
		}
		/* FIXME: Use platform_get_resource for the port */
		info->uart_base = ioremap(OMAP_UART1_BASE, 0x16);
		if (!info->uart_base)
			goto cleanup;
		break;
	case 2:
		if (cpu_is_omap16xx()) {
			irq = INT_UART2;
			info->uart_fclk = clk_get(NULL, "uart2_ck");
		} else {
			irq = INT_24XX_UART2_IRQ;
			info->uart_iclk = clk_get(NULL, "uart2_ick");
			info->uart_fclk = clk_get(NULL, "uart2_fck");
		}
		/* FIXME: Use platform_get_resource for the port */
		info->uart_base = ioremap(OMAP_UART2_BASE, 0x16);
		if (!info->uart_base)
			goto cleanup;
		break;
	case 3:
		if (cpu_is_omap16xx()) {
			irq = INT_UART3;
			info->uart_fclk = clk_get(NULL, "uart3_ck");
		} else {
			irq = INT_24XX_UART3_IRQ;
			info->uart_iclk = clk_get(NULL, "uart3_ick");
			info->uart_fclk = clk_get(NULL, "uart3_fck");
		}
		/* FIXME: Use platform_get_resource for the port */
		info->uart_base = ioremap(OMAP_UART3_BASE, 0x16);
		if (!info->uart_base)
			goto cleanup;
		break;
	default:
		dev_err(info->dev, "No uart defined\n");
		goto cleanup;
	}

	info->irq = irq;
	err = request_irq(irq, hci_h4p_interrupt, 0,
			"hci_h4p_uart", (void *)info);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to get IRQ %d\n", irq);
		goto cleanup;
	}

#if 0
	err = request_irq(gpio_to_irq(info->host_wakeup_gpio),
			  hci_h4p_wakeup_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			  "hci_h4p_wkup", (void *)info);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to get wakeup IRQ %d\n",
			  gpio_to_irq(info->host_wakeup_gpio));
		free_irq(irq, (void *)info);
		goto cleanup;
	}
#endif

	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	err = hci_h4p_reset(info);
	if (err < 0)
		goto cleanup_irq;
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);

#ifdef CONFIG_LEDS_TRIGGERS
	init_timer(&info->led_timer);
	info->led_timer.function = hci_h4p_led_blink;
	info->led_timer.data = (unsigned long)info;
	info->led_state = 1;
	info->led_next_change = jiffies;
	led_trigger_register_simple("bluetooth", &info->led);
#endif

	platform_set_drvdata(pdev, info);
	err = hci_h4p_sysfs_create_files(info->dev);
	if (err < 0)
		goto cleanup_irq;

	if (hci_h4p_register_hdev(info) < 0) {
		dev_err(info->dev, "failed to register hci_h4p hci device\n");
		goto cleanup_irq;
	}
	gpio_set_value(info->reset_gpio, 0);

	return 0;

cleanup_irq:
	free_irq(irq, (void *)info);
#if 0
	free_irq(gpio_to_irq(info->host_wakeup_gpio), (void *)info);
#endif
cleanup:
	gpio_set_value(info->reset_gpio, 0);
	omap_free_gpio(info->reset_gpio);
#if 0
	omap_free_gpio(info->bt_wakeup_gpio);
	omap_free_gpio(info->host_wakeup_gpio);
#endif
	kfree(info);

	return err;

}

static int hci_h4p_remove(struct platform_device *dev)
{
	struct hci_h4p_info *info;

	info = platform_get_drvdata(dev);

	hci_h4p_hci_close(info->hdev);
#if 0
	free_irq(gpio_to_irq(info->host_wakeup_gpio), (void *) info);
#endif
	hci_free_dev(info->hdev);
	omap_free_gpio(info->reset_gpio);
#if 0
	omap_free_gpio(info->bt_wakeup_gpio);
	omap_free_gpio(info->host_wakeup_gpio);
#endif
	free_irq(info->irq, (void *) info);
	led_trigger_unregister_simple(info->led);
	kfree(info);

	return 0;
}

static struct platform_driver hci_h4p_driver = {
	.probe		= hci_h4p_probe,
	.remove		= hci_h4p_remove,
	.driver		= {
		.name	= "hci_h4p",
	},
};

static int __init hci_h4p_init(void)
{
	int err = 0;

	/* Register the driver with LDM */
	err = platform_driver_register(&hci_h4p_driver);
	if (err < 0)
		printk(KERN_WARNING "failed to register hci_h4p driver\n");

	return err;
}

static void __exit hci_h4p_exit(void)
{
	platform_driver_unregister(&hci_h4p_driver);
}

module_init(hci_h4p_init);
module_exit(hci_h4p_exit);

MODULE_DESCRIPTION("h4 driver with nokia extensions");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ville Tervo");
