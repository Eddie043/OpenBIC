/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "plat_mctp.h"

#include <zephyr.h>
#include <sys/printk.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include "libutil.h"
#include "mctp.h"
#include "mctp_ctrl.h"
#include "pldm.h"
#include "ipmi.h"
#include "plat_i3c.h"
#include "plat_i2c.h"
#include "hal_i3c.h"
#include "hal_i2c_target.h"
#include "plat_power_status.h"

LOG_MODULE_REGISTER(plat_mctp);

K_WORK_DEFINE(send_cmd_work, send_cmd_to_dev_handler);

mctp_port plat_mctp_port[] = {
	{ .channel_target = PLDM,
	  .medium_type = MCTP_MEDIUM_TYPE_TARGET_I3C,
	  .conf.i3c_conf.bus = MCTP_I3C_BMC_BUS,
	  .conf.i3c_conf.addr = MCTP_I2C_BIC_ADDR },
	{ .channel_target = SATMC_PLDM,
	  .medium_type = MCTP_MEDIUM_TYPE_SMBUS,
	  .conf.smbus_conf.bus = MCTP_I2C_SATMC_BUS,
	  .conf.smbus_conf.addr = MCTP_I2C_BIC_ADDR },
};

mctp_route_entry mctp_route_tbl[] = {
	{ MCTP_EID_BMC, MCTP_I3C_BMC_BUS, MCTP_I3C_BMC_ADDR },
	{ MCTP_EID_SATMC, MCTP_I2C_SATMC_BUS, MCTP_I2C_SATMC_ADDR },
};

static void set_endpoint_resp_handler(void *args, uint8_t *buf, uint16_t len)
{
	ARG_UNUSED(args);
	CHECK_NULL_ARG(buf);

	set_satmc_status(true);

	LOG_HEXDUMP_INF(buf, len, __func__);
}

static void set_endpoint_resp_timeout(void *args)
{
	CHECK_NULL_ARG(args);

	mctp_route_entry *p = (mctp_route_entry *)args;
	LOG_ERR("Failed to set endpoint 0x%x on bus %d addr 0x%x", p->endpoint, p->bus, p->addr);
}

static void set_dev_endpoint(void)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(mctp_route_tbl); i++) {
		mctp_route_entry *p = mctp_route_tbl + i;

		/* skip BMC */
		if (p->bus == MCTP_I3C_BMC_BUS && p->addr == MCTP_I3C_BMC_ADDR)
			continue;

		for (uint8_t j = 0; j < ARRAY_SIZE(plat_mctp_port); j++) {
			if (p->bus != plat_mctp_port[j].conf.i3c_conf.bus)
				continue;

			struct _set_eid_req req = { 0 };
			req.op = SET_EID_REQ_OP_SET_EID;
			req.eid = p->endpoint;

			mctp_ctrl_msg msg;
			memset(&msg, 0, sizeof(msg));
			msg.ext_params.type = MCTP_MEDIUM_TYPE_SMBUS;
			msg.ext_params.smbus_ext_params.addr = p->addr;

			msg.hdr.cmd = MCTP_CTRL_CMD_SET_ENDPOINT_ID;
			msg.hdr.rq = 1;

			msg.cmd_data = (uint8_t *)&req;
			msg.cmd_data_len = sizeof(req);

			msg.recv_resp_cb_fn = set_endpoint_resp_handler;
			msg.timeout_cb_fn = set_endpoint_resp_timeout;
			msg.timeout_cb_fn_args = p;

			mctp_ctrl_send_msg(pal_find_mctp_by_bus(p->bus), &msg);
		}
	}
}

void set_dev_endpoint_global()
{
	set_dev_endpoint();
}

uint8_t get_mctp_info(uint8_t dest_endpoint, mctp **mctp_inst, mctp_ext_params *ext_params)
{
	CHECK_NULL_ARG_WITH_RETURN(mctp_inst, MCTP_ERROR);
	CHECK_NULL_ARG_WITH_RETURN(ext_params, MCTP_ERROR);

	uint8_t ret = MCTP_ERROR;
	uint32_t index = 0;

	for (index = 0; index < ARRAY_SIZE(mctp_route_tbl); index++) {
		mctp_route_entry *port = mctp_route_tbl + index;
		CHECK_NULL_ARG_WITH_RETURN(port, MCTP_ERROR);

		if (port->endpoint == dest_endpoint) {
			*mctp_inst = pal_find_mctp_by_bus(port->bus);
			CHECK_NULL_ARG_WITH_RETURN(mctp_inst, MCTP_ERROR);

			if (dest_endpoint == MCTP_EID_BMC) {
				ext_params->type = MCTP_MEDIUM_TYPE_TARGET_I3C;
				ext_params->i3c_ext_params.addr = port->addr;
			} else {
				ext_params->type = MCTP_MEDIUM_TYPE_SMBUS;
				ext_params->smbus_ext_params.addr = port->addr;
			}
			ext_params->ep = port->endpoint;
			ret = MCTP_SUCCESS;
			break;
		}
	}
	return ret;
}

static uint8_t get_mctp_route_info(uint8_t dest_endpoint, void **mctp_inst,
				   mctp_ext_params *ext_params)
{
	CHECK_NULL_ARG_WITH_RETURN(mctp_inst, MCTP_ERROR);
	CHECK_NULL_ARG_WITH_RETURN(ext_params, MCTP_ERROR);

	return get_mctp_info(dest_endpoint, (mctp **)mctp_inst, ext_params);
}

static uint8_t mctp_msg_recv(void *mctp_p, uint8_t *buf, uint32_t len, mctp_ext_params ext_params)
{
	CHECK_NULL_ARG_WITH_RETURN(mctp_p, MCTP_ERROR);
	CHECK_NULL_ARG_WITH_RETURN(buf, MCTP_ERROR);

	/* first byte is message type */
	uint8_t msg_type = (buf[0] & MCTP_MSG_TYPE_MASK) >> MCTP_MSG_TYPE_SHIFT;

	switch (msg_type) {
	case MCTP_MSG_TYPE_CTRL:
		mctp_ctrl_cmd_handler(mctp_p, buf, len, ext_params);
		break;

	case MCTP_MSG_TYPE_PLDM:
		mctp_pldm_cmd_handler(mctp_p, buf, len, ext_params);
		break;

	default:
		LOG_WRN("Cannot find message receive function!!");
		return MCTP_ERROR;
	}

	return MCTP_SUCCESS;
}

void send_cmd_to_dev_handler(struct k_work *work)
{
	/* init the device endpoint */
	set_dev_endpoint();
}

void send_cmd_to_dev(struct k_timer *timer)
{
	k_work_submit(&send_cmd_work);
}

bool mctp_add_sel_to_ipmi(struct ipmi_storage_add_sel_req *sel_msg, uint8_t sel_type)
{
	CHECK_NULL_ARG_WITH_RETURN(sel_msg, false);

	pldm_msg msg = { 0 };
	struct mctp_to_ipmi_sel_req req = { 0 };

	msg.hdr.pldm_type = PLDM_TYPE_OEM;
	msg.hdr.cmd = PLDM_OEM_IPMI_BRIDGE;
	msg.hdr.rq = 1;
	msg.buf = (uint8_t *)&req;
	msg.len = sizeof(struct mctp_to_ipmi_sel_req);

	if (set_iana(req.header.iana, sizeof(req.header.iana))) {
		LOG_ERR("Set IANA fail");
		return false;
	}
	req.header.netfn_lun = (NETFN_STORAGE_REQ << 2);
	req.header.ipmi_cmd = CMD_STORAGE_ADD_SEL;
	req.req_data = *sel_msg;

	switch (sel_type) {
	case ADD_COMMON_SEL:
		req.req_data.event.record_type = 0x02; // System Event
		req.req_data.event.gen_id[0] = (MCTP_I3C_BIC_ADDR << 1);
		req.req_data.event.evm_rev = 0x04;
		break;

	case ADD_OEM_SEL:
		req.req_data.oem_event.record_type = 0xFB; // Unified SEL
		req.req_data.oem_event.record_id = 0x0000;
		req.req_data.oem_event.timestamp = 0x00000000;
		req.req_data.oem_event.rsv_1 = 0xFF;
		req.req_data.oem_event.rsv_2 = 0xFF;
		req.req_data.oem_event.rsv_3 = 0xFF;
		req.req_data.oem_event.failure_event_details = 0xFF;
		req.req_data.oem_event.pxe_http_fail_type = 0xFF;
		req.req_data.oem_event.pxe_http_error_code = 0xFF;
		req.req_data.oem_event.rsv_4 = 0xFF;
		break;

	default:
		break;
	}

	mctp *mctp_inst = NULL;
	if (get_mctp_info_by_eid(MCTP_EID_BMC, &mctp_inst, &msg.ext_params) == false) {
		LOG_ERR("Failed to get mctp info by eid 0x%x", MCTP_EID_BMC);
		return false;
	}

	uint8_t resp_len = sizeof(struct mctp_to_ipmi_sel_resp);
	uint8_t rbuf[resp_len];

	memset(&rbuf, 0, resp_len);
	if (!mctp_pldm_read(mctp_inst, &msg, rbuf, resp_len)) {
		LOG_ERR("mctp_pldm_read fail");
		return false;
	}

	struct mctp_to_ipmi_sel_resp *resp = (struct mctp_to_ipmi_sel_resp *)rbuf;

	if ((resp->header.completion_code != MCTP_SUCCESS) ||
	    (resp->header.ipmi_comp_code != CC_SUCCESS)) {
		LOG_ERR("Check reponse completion code fail %x %x", resp->header.completion_code,
			resp->header.ipmi_comp_code);
		return false;
	}

	return true;
}

void plat_mctp_init(void)
{
	int ret = 0;

	/* init the mctp/pldm instance */
	for (uint8_t i = 0; i < ARRAY_SIZE(plat_mctp_port); i++) {
		mctp_port *p = plat_mctp_port + i;

		if (p->medium_type == MCTP_MEDIUM_TYPE_SMBUS) {
			struct _i2c_target_config cfg;
			memset(&cfg, 0, sizeof(cfg));
			cfg.address = p->conf.smbus_conf.addr;
			cfg.i2c_msg_count = 0x0A;

			if (i2c_target_control(p->conf.smbus_conf.bus, &cfg,
					       I2C_CONTROL_REGISTER) != I2C_TARGET_API_NO_ERR) {
				LOG_ERR("i2c %d register target failed", p->conf.smbus_conf.bus);
				continue;
			}
		}

		p->mctp_inst = mctp_init();
		if (!p->mctp_inst) {
			LOG_ERR("mctp_init failed!!");
			continue;
		}

		uint8_t rc = mctp_set_medium_configure(p->mctp_inst, p->medium_type, p->conf);
		if (rc != MCTP_SUCCESS) {
			LOG_INF("mctp set medium configure failed");
		}

		mctp_reg_endpoint_resolve_func(p->mctp_inst, get_mctp_route_info);

		mctp_reg_msg_rx_func(p->mctp_inst, mctp_msg_recv);

		ret = mctp_start(p->mctp_inst);
	}
}

uint8_t plat_get_mctp_port_count()
{
	return ARRAY_SIZE(plat_mctp_port);
}

mctp_port *plat_get_mctp_port(uint8_t index)
{
	return plat_mctp_port + index;
}
