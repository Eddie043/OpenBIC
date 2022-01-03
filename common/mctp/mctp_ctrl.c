#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <cmsis_os2.h>
#include <logging/log.h>
#include "mctp.h"
#include "mctp_ctrl.h"

LOG_MODULE_DECLARE(mctp);

#define DEFAULT_WAIT_TO_MS 3000
#define RESP_MSG_PROC_MUTEX_WAIT_TO_MS 1000
#define TO_CHK_INTERVAL_MS 1000

#define MCTP_CTRL_INST_ID_MASK 0x3F

typedef struct _wait_msg {
    sys_snode_t node;
    mctp *mctp_inst;
    int64_t exp_to_ms;
    mctp_ctrl_msg msg;
} wait_msg;

static K_MUTEX_DEFINE(wait_recv_resp_mutex);

static sys_slist_t wait_recv_resp = SYS_SLIST_STATIC_INIT(&wait_recv_resp);

static uint8_t to_chk(sys_slist_t *list, struct k_mutex *mutex)
{
    if (!list || !mutex)
        return MCTP_ERROR;

    if (k_mutex_lock(mutex, K_MSEC(RESP_MSG_PROC_MUTEX_WAIT_TO_MS))) {
        LOG_WRN("pldm mutex is locked over %d ms!!", RESP_MSG_PROC_MUTEX_WAIT_TO_MS);
        return MCTP_ERROR;
    }

    sys_snode_t *node;
    sys_snode_t *s_node;
    sys_snode_t *pre_node = NULL;
    int64_t cur_uptime = k_uptime_get();

    SYS_SLIST_FOR_EACH_NODE_SAFE(list, node, s_node) {
        wait_msg *p = (wait_msg *)node;

        if ((p->exp_to_ms <= cur_uptime)) {
            printk("mctp ctrl msg timeout!!\n");
            printk("cmd %x, inst_id %x\n", p->msg.hdr.cmd, p->msg.hdr.inst_id);
            sys_slist_remove(list, pre_node, node);

            if (p->msg.timeout_cb_fn)
                p->msg.timeout_cb_fn(p->msg.timeout_cb_fn_args);

            free(p);
        } else {
            pre_node = node;
        }
    }

    k_mutex_unlock(mutex);
    return MCTP_SUCCESS;
}

static void to_monitor(void *dummy0, void *dummy1, void *dummy2)
{
    ARG_UNUSED(dummy0);
    ARG_UNUSED(dummy1);
    ARG_UNUSED(dummy2);

    while (1) {
        k_msleep(TO_CHK_INTERVAL_MS);

        to_chk(&wait_recv_resp, &wait_recv_resp_mutex);
    }
}

static uint8_t mctp_ctrl_cmd_resp_proc(mctp *mctp_inst, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
    if (!mctp_inst || !buf || !len)
        return MCTP_ERROR;
    
    if (k_mutex_lock(&wait_recv_resp_mutex, K_MSEC(RESP_MSG_PROC_MUTEX_WAIT_TO_MS))) {
        LOG_WRN("mutex is locked over %d ms!", RESP_MSG_PROC_MUTEX_WAIT_TO_MS);
        return MCTP_ERROR;
    }

    mctp_ctrl_hdr *hdr = (mctp_ctrl_hdr *)buf;
    sys_snode_t *node;
    sys_snode_t *s_node;
    sys_snode_t *pre_node = NULL;
    sys_snode_t *found_node = NULL;

    SYS_SLIST_FOR_EACH_NODE_SAFE(&wait_recv_resp, node, s_node) {
        wait_msg *p = (wait_msg *)node;
        /* found the proper handler */
        if ((p->msg.hdr.inst_id == hdr->inst_id) && 
            (p->mctp_inst == mctp_inst) &&
            (p->msg.hdr.cmd == hdr->cmd)) {

            found_node = node;
            sys_slist_remove(&wait_recv_resp, pre_node, node);
            break;
        } else {
            pre_node = node;
        }
    }
    k_mutex_unlock(&wait_recv_resp_mutex);

    if (found_node) {
        /* invoke resp handler */
        wait_msg *p = (wait_msg *)found_node;
        if (p->msg.recv_resp_cb_fn)
            p->msg.recv_resp_cb_fn(p->msg.recv_resp_cb_args, buf + sizeof(p->msg.hdr), len - sizeof(p->msg.hdr)); /* remove mctp ctrl header for handler */
        free(p);
    }

    return MCTP_SUCCESS; 
}

uint8_t mctp_ctrl_cmd_handler(void *mctp_p, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
    if (!mctp_p || !buf || !len)
        return MCTP_ERROR;

    mctp *mctp_inst = (mctp *)mctp_p;
    mctp_ctrl_hdr *hdr = (mctp_ctrl_hdr *)buf;

    /* the message is a response, check if any callback function should be invoked */
    if (!hdr->rq)
        return mctp_ctrl_cmd_resp_proc(mctp_inst, buf, len, ext_params);

    /* TODO: the message is a request, find the proper handler to handle it */

    return MCTP_SUCCESS;
}

uint8_t mctp_ctrl_send_msg(void *mctp_p, mctp_ctrl_msg *msg)
{
    if (!mctp_p || !msg || !msg->cmd_data)
        return MCTP_ERROR;

    mctp *mctp_inst = (mctp *)mctp_p;

    if (msg->hdr.rq) {
        static uint8_t inst_id;

        msg->hdr.inst_id = (inst_id++) & MCTP_CTRL_INST_ID_MASK;
        msg->hdr.msg_type = MCTP_MSG_TYPE_CTRL;

        msg->ext_param.tag_owner = 1;
    }

    uint16_t len = sizeof(msg->hdr) + msg->cmd_data_len;
    uint8_t buf[len];

    memcpy(buf, &msg->hdr, sizeof(msg->hdr));
    memcpy(buf + sizeof(msg->hdr), msg->cmd_data, msg->cmd_data_len);

    LOG_HEXDUMP_WRN(buf, len, __func__);

    uint8_t rc = mctp_send_msg(mctp_inst, buf, len, msg->ext_param);
    if (rc == MCTP_ERROR) {
        LOG_WRN("mctp_send_msg error!!");
        return MCTP_ERROR;
    }

    if (msg->hdr.rq) {
        wait_msg *p = (wait_msg *)malloc(sizeof(*p));
        if (!p) {
            LOG_WRN("wait_msg alloc failed!");
            return MCTP_ERROR;
        }

        memset(p, 0, sizeof(*p));
        p->mctp_inst = mctp_inst;
        p->msg = *msg;
        p->exp_to_ms = k_uptime_get() + (msg->timeout_ms ? msg->timeout_ms : DEFAULT_WAIT_TO_MS);

        k_mutex_lock(&wait_recv_resp_mutex, K_FOREVER);
        sys_slist_append(&wait_recv_resp, &p->node);
        k_mutex_unlock(&wait_recv_resp_mutex);
    }

    return MCTP_SUCCESS;
}

K_THREAD_DEFINE(monitor_tid, 1024, to_monitor, NULL, NULL, NULL, 7, 0, 0);