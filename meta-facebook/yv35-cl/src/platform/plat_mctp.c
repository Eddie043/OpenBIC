/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <cmsis_os2.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include "mctp.h"
#include "mctp_ctrl.h"
#include "pldm.h"

LOG_MODULE_REGISTER(plat_mctp);

#define MCTP_MSG_TYPE_SHIFT 0
#define MCTP_MSG_TYPE_MASK 0x7F
#define MCTP_IC_SHIFT 7
#define MCTP_IC_MASK 0x80

 /* i2c 8 bit address */
#define I2C_ADDR_BIC 0x40
#define I2C_ADDR_BMC 0x20
#define I2C_ADDR_NIC 0x64

/* i2c dev bus */
#define I2C_BUS_BMC 0x06
#define I2C_BUS_NIC 0x08

/* mctp endpoint */
#define MCTP_EID_BMC 0x08
#define MCTP_EID_NIC 0x10

typedef struct _mctp_smbus_port {
    mctp *mctp_inst;
    mctp_medium_conf conf;
    uint8_t user_idx;
} mctp_smbus_port;

/* mctp route entry struct */
typedef struct _mctp_route_entry {
    uint8_t endpoint;
    uint8_t bus; /* TODO: only consider smbus/i3c */
    uint8_t addr; /* TODO: only consider smbus/i3c */
} mctp_route_entry;

typedef struct _mctp_msg_handler {
    MCTP_MSG_TYPE type;
    mctp_fn_cb msg_handler_cb;
} mctp_msg_handler;
#if 0
static mctp_msg_handler cmd_tbl[] = {
    {MCTP_MSG_TYPE_PLDM, mctp_pldm_cmd_handler}
};
#endif
static mctp_smbus_port smbus_port[] = {
    {.conf.smbus_conf.addr = I2C_ADDR_BIC, .conf.smbus_conf.bus = I2C_BUS_BMC},
    {.conf.smbus_conf.addr = I2C_ADDR_BIC, .conf.smbus_conf.bus = I2C_BUS_NIC}
};

mctp_route_entry mctp_route_tbl[] = {
    {MCTP_EID_BMC, I2C_BUS_BMC, I2C_ADDR_BMC},
    {MCTP_EID_NIC, I2C_BUS_NIC, I2C_ADDR_NIC}
};

struct k_thread mctp_test_thread;
K_KERNEL_STACK_MEMBER(mctp_test_thread_stack, 4000);

static mctp *find_mctp_by_smbus(uint8_t bus)
{
    uint8_t i;
    for (i = 0; i < ARRAY_SIZE(smbus_port); i++) {
        mctp_smbus_port *p = smbus_port + i;
        
        if (bus == p->conf.smbus_conf.bus)
            return p->mctp_inst;
    }

    return NULL;
}

static void set_ep_resp_handler(void *args, uint8_t *buf, uint16_t len)
{
    if (!buf || !len)
        return;

    LOG_HEXDUMP_WRN(buf, len, __func__);

    // struct _set_eid_resp *resp = (struct _set_eid_resp *)buf;
}

static void set_ep_resp_to(void *args)
{
    printk("%s\n", __func__);
    mctp_route_entry *p = (mctp_route_entry *)args;
    printk("p->addr = %x\n", p->addr);
}

static void set_dev_ep(void)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(mctp_route_tbl); i++) {
        mctp_route_entry *p = mctp_route_tbl + i;
        
        /* skip BMC */
        if (p->bus == I2C_BUS_BMC && p->addr == I2C_ADDR_BMC)
            continue;

        for (uint8_t j = 0; j < ARRAY_SIZE(smbus_port); j++) {
            if (p->bus != smbus_port[j].conf.smbus_conf.bus)
                continue;
            
            struct _set_eid_req req = {0};
            req.op = SET_EID_REQ_OP_SET_EID;
            req.eid = p->endpoint;

            mctp_ctrl_msg msg;
            memset(&msg, 0, sizeof(msg));
            msg.ext_param.type = MCTP_MEDIUM_TYPE_SMBUS;
            msg.ext_param.smbus_ext_param.addr = p->addr;

            msg.hdr.cmd = MCTP_CTRL_CMD_SET_ENDPOINT_ID;
            msg.hdr.rq = 1;

            msg.cmd_data = (uint8_t *)&req;
            msg.cmd_data_len = sizeof(req);

            msg.recv_resp_cb_fn = set_ep_resp_handler;
            msg.timeout_cb_fn = set_ep_resp_to;
            msg.timeout_cb_fn_args = p;

            mctp_ctrl_send_msg(find_mctp_by_smbus(p->bus), &msg);
        }
    }
}

static uint8_t mctp_msg_recv(void *mctp_p, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
    if (!mctp_p || !buf || !len)
        return MCTP_ERROR;
    
    /* first byte is message type and ic */
    uint8_t msg_type = (buf[0] & MCTP_MSG_TYPE_MASK) >> MCTP_MSG_TYPE_SHIFT;
    uint8_t ic = (buf[0] & MCTP_IC_MASK) >> MCTP_IC_SHIFT;
    (void)ic;

    switch (msg_type) {
    case MCTP_MSG_TYPE_CTRL:
        mctp_ctrl_cmd_handler(mctp_p, buf, len, ext_params);
        break;
        
    case MCTP_MSG_TYPE_PLDM:
        mctp_pldm_cmd_handler(mctp_p, buf, len, ext_params);
        break;
    }

#if 0
    uint8_t i;
    for (i = 0; i < sizeof(cmd_tbl) / sizeof(*cmd_tbl); i++) {
        if (cmd_tbl[i].type == msg_type) {
            cmd_tbl[i].msg_handler_cb(mctp_p, buf, len, ext_params);
            break;
        }
    }
#endif
    return MCTP_SUCCESS;
}

static uint8_t get_mctp_route_info(uint8_t dest_endpoint, void **mctp_inst, mctp_ext_param *ext_params)
{
    if (!mctp_inst || !ext_params)
        return MCTP_ERROR;

    uint8_t rc = MCTP_ERROR;
    uint32_t i;

    for (i = 0; ARRAY_SIZE(mctp_route_tbl); i++) {
        mctp_route_entry *p = mctp_route_tbl + i;
        if (p->endpoint == dest_endpoint) {
            *mctp_inst = find_mctp_by_smbus(p->bus);
            ext_params->type = MCTP_MEDIUM_TYPE_SMBUS;
            ext_params->smbus_ext_param.addr = p->addr;
            rc = MCTP_SUCCESS;
            break;
        }
    }

    return rc;
}

void mctp_test_handler(void *arug0, void *arug1, void *arug2){
    k_msleep(2000);

    while (1) {
        k_msleep(10);
        set_dev_ep();
#if 0
        pldm_cmd_proc_fn handler = NULL;
        pldm_oem_handler_query(PLDM_OEM_IPMI_BRIDGE, (void **)&handler);
        printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
        if (handler) {
            pldm_msg resp = {0};
            mctp_ext_param ext_params;
            uint8_t ipmi_buf[] = {0x0a, 0x0b, 0x0c, 0x0d, 0x15, 0xa0, 0x00, 0x06, 0x01};

            memset(&ext_params, 0, sizeof(ext_params));
            ext_params.ep = 0x08;
            ext_params.msg_tag = 0x05;
            ext_params.type = MCTP_MEDIUM_TYPE_SMBUS;
            ext_params.smbus_ext_param.addr = 0x20;

            printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
            handler(smbus_port[0].mctp_inst, ipmi_buf + sizeof(pldm_hdr), sizeof(ipmi_buf) - sizeof(pldm_hdr), resp.buf, &resp.len, &ext_params);
        }
#endif
#if 0
        mctp_ext_param ext_param = {0};
        ext_param.type = MCTP_MEDIUM_TYPE_SMBUS;
        ext_param.smbus_ext_param.addr = 0x20;
        ext_param.ep = 0x08;

        pldm_msg msg = {0};
        msg.hdr.pldm_type = PLDM_TYPE_OEM;
        msg.hdr.cmd = PLDM_OEM_IPMI_BRIDGE;
        msg.hdr.rq = 1;
        struct _ipmi_cmd_req *req = (struct _ipmi_cmd_req *)msg.buf;
        req->netfn = 0x06 << 2;
        req->cmd = 0x01;
        msg.len = 2;

        mctp_pldm_send_msg(smbus_port[0].pldm_inst, &msg, ext_param, main_gettid, NULL);
#endif
    }
}

void plat_mctp_init(void)
{
    LOG_INF("plat_mctp_init");

    /* init the mctp/pldm instance */
    for (uint8_t i = 0; i < ARRAY_SIZE(smbus_port); i++) {
        mctp_smbus_port *p = smbus_port + i;
        LOG_DBG("smbus port %d", i);
        LOG_DBG("bus = %x, addr = %x", p->conf.smbus_conf.bus, p->conf.smbus_conf.addr);

        p->mctp_inst = mctp_init();
        if (!p->mctp_inst) {
            LOG_ERR("mctp_init failed!!");
            continue;
        }

        LOG_DBG("mctp_inst = %p", p->mctp_inst);
        uint8_t rc = mctp_set_medium_configure(p->mctp_inst, MCTP_MEDIUM_TYPE_SMBUS, p->conf);
        LOG_DBG("mctp_set_medium_configure %s", (rc == MCTP_SUCCESS)? "success": "failed");

        mctp_reg_endpoint_resolve_func(p->mctp_inst, get_mctp_route_info);
        mctp_reg_msg_rx_func(p->mctp_inst, mctp_msg_recv);

        mctp_start(p->mctp_inst);
    }

    /* init the device endpoint */
    k_msleep(10);
    set_dev_ep();

#if 0
    k_thread_create(&mctp_test_thread, mctp_test_thread_stack,
                    K_THREAD_STACK_SIZEOF(mctp_test_thread_stack),
                    mctp_test_handler,
                    NULL, NULL, NULL,
                    K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    k_thread_name_set(&mctp_test_thread, "IPMI_thread");
#endif
}