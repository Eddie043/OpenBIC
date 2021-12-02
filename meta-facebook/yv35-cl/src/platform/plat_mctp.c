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
#include "pldm.h"

LOG_MODULE_REGISTER(plat_mctp);

#define MCTP_MSG_TYPE_SHIFT 0
#define MCTP_MSG_TYPE_MASK 0x7F
#define MCTP_IC_SHIFT 7
#define MCTP_IC_MASK 0x80

typedef struct _mctp_smbus_port {
	mctp *mctp_inst;
	mctp_medium_conf conf;
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

static mctp_msg_handler cmd_tbl[] = {
	{MCTP_MSG_TYPE_PLDM, mctp_pldm_cmd_handler}
};

static mctp_smbus_port smbus_port[] = {
	{.conf.smbus_conf.addr = 0xB2, .conf.smbus_conf.bus = 0x01},
	// {.conf.smbus_conf.addr = 0x20, .conf.smbus_conf.bus = 0x02}
};

mctp_route_entry mctp_route_tbl[] = {
	{0x11, 0x01, 0x41},
	{0x12, 0x02, 0x42},
	{0x13, 0x01, 0x43},
	{0x14, 0x02, 0x44},
	{0x15, 0x01, 0x45},
	{0x16, 0x02, 0x46}
};

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

uint8_t mctp_control_cmd_handler(void *mctp_p, uint8_t src_ep, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
	if (!mctp_p || !buf || !len)
		return MCTP_ERROR;

	return MCTP_SUCCESS;
}

static uint8_t mctp_msg_recv(void *mctp_p, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
	if (!mctp_p || !buf || !len)
		return MCTP_ERROR;
	
	/* first byte is message type and ic */
	uint8_t msg_type = (buf[0] & MCTP_MSG_TYPE_MASK) >> MCTP_MSG_TYPE_SHIFT;
	uint8_t ic = (buf[0] & MCTP_IC_MASK) >> MCTP_IC_SHIFT;
	(void)ic;
	
	uint8_t i;
	for (i = 0; i < sizeof(cmd_tbl) / sizeof(*cmd_tbl); i++) {
		if (cmd_tbl[i].type == msg_type) {
			cmd_tbl[i].msg_handler_cb(mctp_p, buf, len, ext_params);
			break;
		}
	}

	return MCTP_SUCCESS;
}

static uint8_t get_route_info(uint8_t dest_endpoint, void **mctp_inst, mctp_ext_param *ext_params)
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

static void main_gettid(void *args, uint8_t *buf, uint16_t len)
{
	LOG_DBG("");
}

static void to_test(void *to_args)
{
	uint8_t *i = (uint8_t *)to_args;
	LOG_DBG("*i = %d", *i);
}

void plat_mctp_init(void)
{
	LOG_INF("plat_mctp_init");

	uint32_t i;
	for (i = 0; i < ARRAY_SIZE(smbus_port); i++) {
		mctp_smbus_port *p = smbus_port + i;
		LOG_DBG("smbus port %d", i);
		LOG_DBG("bus = %x, addr = %x", p->conf.smbus_conf.bus, p->conf.smbus_conf.addr);

		p->mctp_inst = mctp_init();
		if (!p->mctp_inst) {
			LOG_ERR("mctp_init failed!!");
			continue;
		}

		uint8_t rc = mctp_set_medium_configure(p->mctp_inst, MCTP_MEDIUM_TYPE_SMBUS, p->conf);
		LOG_DBG("mctp_set_medium_configure %s", (rc == MCTP_SUCCESS)? "success": "failed");

		mctp_reg_endpoint_resolve_func(p->mctp_inst, get_route_info);
		mctp_reg_msg_rx_func(p->mctp_inst, mctp_msg_recv);
		mctp_start(p->mctp_inst);
	}

	pldm_init();
}