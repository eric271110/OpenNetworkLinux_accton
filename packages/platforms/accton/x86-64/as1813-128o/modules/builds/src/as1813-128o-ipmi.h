/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared IPMI helper for AS1813-128O platform drivers.
 *
 * This header provides common IPMI data structures and helper functions
 * used by fan, thermal, psu, sys, and leds drivers.
 */
#ifndef __AS1813_128O_IPMI_H__
#define __AS1813_128O_IPMI_H__

#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/completion.h>
#include <linux/string_helpers.h>

#define ACCTON_IPMI_NETFN       0x34
#define IPMI_TIMEOUT            (5 * HZ)
#define IPMI_ERR_RETRY_TIMES    1

struct ipmi_data {
    struct completion   read_complete;
    struct ipmi_addr    address;
    struct ipmi_user    *user;
    int                 interface;

    struct kernel_ipmi_msg tx_message;
    long                tx_msgid;

    void                *rx_msg_data;
    unsigned short      rx_msg_len;
    unsigned char       rx_result;
    int                 rx_recv_type;

    struct ipmi_user_hndl ipmi_hndlrs;
};

static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data)
{
    unsigned short rx_len;
    struct ipmi_data *ipmi = user_msg_data;

    if (msg->msgid != ipmi->tx_msgid) {
        ipmi_free_recv_msg(msg);
        return;
    }

    ipmi->rx_recv_type = msg->recv_type;
    if (msg->msg.data_len > 0)
        ipmi->rx_result = msg->msg.data[0];
    else
        ipmi->rx_result = IPMI_UNKNOWN_ERR_COMPLETION_CODE;

    if (msg->msg.data_len > 1) {
        rx_len = msg->msg.data_len - 1;
        if (ipmi->rx_msg_len < rx_len)
            rx_len = ipmi->rx_msg_len;
        ipmi->rx_msg_len = rx_len;
        memcpy(ipmi->rx_msg_data, msg->msg.data + 1, ipmi->rx_msg_len);
    } else {
        ipmi->rx_msg_len = 0;
    }

    ipmi_free_recv_msg(msg);
    complete(&ipmi->read_complete);
}

static int init_ipmi_data(struct ipmi_data *ipmi, int iface, struct device *dev)
{
    int err;

    init_completion(&ipmi->read_complete);

    ipmi->address.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
    ipmi->address.channel = IPMI_BMC_CHANNEL;
    ipmi->address.data[0] = 0;
    ipmi->interface = iface;

    ipmi->tx_msgid = 0;
    ipmi->tx_message.netfn = ACCTON_IPMI_NETFN;

    ipmi->ipmi_hndlrs.ipmi_recv_hndl = ipmi_msg_handler;

    err = ipmi_create_user(ipmi->interface, &ipmi->ipmi_hndlrs,
                           ipmi, &ipmi->user);
    if (err < 0) {
        dev_err(dev, "Unable to register user with IPMI interface %d\n",
                ipmi->interface);
        return -EACCES;
    }

    return 0;
}

static int _ipmi_send_message(struct ipmi_data *ipmi, struct device *dev,
                              unsigned char cmd,
                              unsigned char *tx_data, unsigned short tx_len,
                              unsigned char *rx_data, unsigned short rx_len)
{
    int err;

    ipmi->tx_message.cmd      = cmd;
    ipmi->tx_message.data     = tx_data;
    ipmi->tx_message.data_len = tx_len;
    ipmi->rx_msg_data         = rx_data;
    ipmi->rx_msg_len          = rx_len;

    err = ipmi_validate_addr(&ipmi->address, sizeof(ipmi->address));
    if (err) {
        dev_err(dev, "validate_addr=%x\n", err);
        return err;
    }

    ipmi->tx_msgid++;
    err = ipmi_request_settime(ipmi->user, &ipmi->address, ipmi->tx_msgid,
                               &ipmi->tx_message, ipmi, 0, 0, 0);
    if (err) {
        dev_err(dev, "request_settime=%x\n", err);
        return err;
    }

    err = wait_for_completion_timeout(&ipmi->read_complete, IPMI_TIMEOUT);
    if (!err) {
        dev_err(dev, "request_timeout\n");
        return -ETIMEDOUT;
    }

    return 0;
}

static int ipmi_send_message(struct ipmi_data *ipmi, struct device *dev,
                             unsigned char cmd,
                             unsigned char *tx_data, unsigned short tx_len,
                             unsigned char *rx_data, unsigned short rx_len)
{
    int status = 0, retry;

    for (retry = 0; retry <= IPMI_ERR_RETRY_TIMES; retry++) {
        status = _ipmi_send_message(ipmi, dev, cmd, tx_data, tx_len,
                                    rx_data, rx_len);
        if (unlikely(status != 0)) {
            dev_err(dev, "ipmi_send_%d err status(%d) cmd=0x%02x\n",
                    retry, status, cmd);
            continue;
        }

        if (unlikely(ipmi->rx_result != 0)) {
            dev_err(dev, "ipmi_send_%d err result(%d) cmd=0x%02x\n",
                    retry, ipmi->rx_result, cmd);
            continue;
        }

        break;
    }

    return status;
}

#endif /* __AS1813_128O_IPMI_H__ */
