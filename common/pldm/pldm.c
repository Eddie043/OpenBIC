#include <zephyr.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/printk.h>
#include <sys/slist.h>
#include <cmsis_os2.h>
#include <logging/log.h>
#include "mctp.h"
#include "pldm.h"

LOG_MODULE_REGISTER(pldm);

#define PLDM_HDR_INST_ID_MASK 0x1F
#define PLDM_MSG_CHECK_PER_MS 1000
#define PLDM_MSG_TIMEOUT_MS 5000
#define PLDM_RESP_MSG_PROC_MUTEX_TIMEOUT_MS 500
#define PLDM_TASK_NAME_MAX_SIZE 32

typedef struct _wait_resp_pldm_msg {
    sys_snode_t node;
    pldm_hdr hdr;
    int64_t exp_timeout_time;
    void (*resp_fn)(void *, uint8_t *, uint16_t);
    void *cb_args;
    void (*to_fn)(void *);
    void *to_fn_args;
} wait_resp_pldm_msg;

struct _pldm_handler_query_entry {
    PLDM_TYPE type;
    uint8_t (*handler_query)(uint8_t, void **);
};

static struct _pldm_handler_query_entry query_tbl[] = {
    {PLDM_TYPE_BASE, pldm_base_handler_query},
    {PLDM_TYPE_OEM, pldm_oem_handler_query}
};

static uint8_t list_timeout_chk(sys_slist_t *list, struct k_mutex *mutex)
{
    if (!list || !mutex)
        return PLDM_ERROR;

    if (k_mutex_lock(mutex, K_MSEC(PLDM_RESP_MSG_PROC_MUTEX_TIMEOUT_MS))) {
        LOG_WRN("pldm mutex is locked over %d ms!!", PLDM_RESP_MSG_PROC_MUTEX_TIMEOUT_MS);
        return PLDM_ERROR;
    }

    sys_snode_t *node;
    sys_snode_t *s_node;
    sys_snode_t *pre_node = NULL;
    int64_t cur_uptime = k_uptime_get();

    SYS_SLIST_FOR_EACH_NODE_SAFE(list, node, s_node) {
        wait_resp_pldm_msg *p = (wait_resp_pldm_msg *)node;

        if ((p->exp_timeout_time <= cur_uptime)) {
            LOG_INF("pldm msg timeout!!");
            LOG_INF("type %x, cmd %x, inst_id %x", p->hdr.pldm_type, p->hdr.cmd, p->hdr.inst_id);
            sys_slist_remove(list, pre_node, node);

            if (p->to_fn)
                p->to_fn(p->to_fn_args);

            free(p);
        } else {
            pre_node = node;
        }
    }

    k_mutex_unlock(mutex);
    return PLDM_SUCCESS;
}

static void list_monitor(void *pldm_p, void *dummy0, void *dummy1)
{
    if (!pldm_p) {
        LOG_ERR("pldm is null");
        return;
    }

    ARG_UNUSED(dummy0);
    ARG_UNUSED(dummy1);

    pldm_t *pldm_inst = (pldm_t *)pldm_p;

    while (1) {
        k_msleep(PLDM_MSG_CHECK_PER_MS);

        list_timeout_chk(&pldm_inst->wait_recv_resp_list, &pldm_inst->wait_recv_resp_list_mutex);
        list_timeout_chk(&pldm_inst->wait_send_resp_list, &pldm_inst->wait_send_resp_list_mutex);
    }
}
#if 0
static uint8_t pldm_wait_resp_append(void *mctp_p, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
    if (!mctp_p || !buf || !len)
        return PLDM_ERROR;

    wait_resp_pldm_msg *msg = (wait_resp_pldm_msg *)malloc(sizeof(*msg));
    if (!msg) {
        LOG_WRN("malloc FAILED!!");
        return PLDM_ERROR;
    }
    memset(msg, 0, sizeof(*msg));
    memcpy(&msg->hdr, buf, sizeof(msg->hdr));

    /* the uptime is int64_t millisecond, don't care overflow */
    msg->exp_timeout_time = PLDM_MSG_TIMEOUT_MS;

    /* TODO: should set timeout? */
    k_mutex_lock(&pldm.wait_send_resp_list_mutex, K_FOREVER);
    sys_slist_append(&pldm.wait_send_resp_list, &msg->node);
    k_mutex_unlock(&pldm.wait_send_resp_list_mutex);
    return PLDM_SUCCESS;
}
#endif
static uint8_t pldm_resp_msg_proc(pldm_t *pldm_inst, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
    if (!pldm_inst || !buf || !len)
        return PLDM_ERROR;

    pldm_msg *msg = (pldm_msg *)buf;
    sys_snode_t *node;
    sys_snode_t *s_node;
    sys_snode_t *pre_node = NULL;
    sys_snode_t *found_node = NULL;

    if (k_mutex_lock(&pldm_inst->wait_recv_resp_list_mutex, K_MSEC(PLDM_RESP_MSG_PROC_MUTEX_TIMEOUT_MS))) {
        LOG_WRN("pldm mutex is locked over %d ms!!", PLDM_RESP_MSG_PROC_MUTEX_TIMEOUT_MS);
        return PLDM_ERROR;
    }

    SYS_SLIST_FOR_EACH_NODE_SAFE(&pldm_inst->wait_recv_resp_list, node, s_node) {
        wait_resp_pldm_msg *p = (wait_resp_pldm_msg *)node;

        /* found the proper handler */
        if ((p->hdr.inst_id == msg->hdr.inst_id) && 
            (p->hdr.pldm_type == msg->hdr.pldm_type) && 
            (p->hdr.cmd == msg->hdr.cmd)) {
            
            found_node = node;
            sys_slist_remove(&pldm_inst->wait_recv_resp_list, pre_node, node);
            break;
        } else {
            pre_node = node;
        }
    }
    k_mutex_unlock(&pldm_inst->wait_recv_resp_list_mutex);

    if (found_node) {
        /* invoke resp handler */
        wait_resp_pldm_msg *p = (wait_resp_pldm_msg *)found_node;
        if (p->resp_fn)
            p->resp_fn(p->cb_args, buf + sizeof(p->hdr), len - sizeof(p->hdr)); /* remove pldm header for handler */
        free(p);
    }

    return PLDM_SUCCESS;
}

uint8_t mctp_pldm_cmd_handler(void *pldm_p, uint8_t *buf, uint32_t len, mctp_ext_param ext_params)
{
    if (!pldm_p || !buf || !len)
        return PLDM_ERROR;

    pldm_t *pldm_inst = (pldm_t *)pldm_p;
    pldm_hdr *hdr = (pldm_hdr *)buf;
    LOG_DBG("msg_type = %d", hdr->msg_type);
    LOG_DBG("req_d_id = 0x%x", hdr->req_d_id);
    LOG_DBG("pldm_type = 0x%x", hdr->pldm_type);
    LOG_DBG("cmd = 0x%x", hdr->cmd);

    /* the message is a response, check if any callback function should be invoked */
    if (!hdr->rq)
        return pldm_resp_msg_proc(pldm_inst, buf, len, ext_params);

    /* the message is a request, find the proper handler to handle it */
    /* initial response data */
    uint16_t resp_len = 0;
    pldm_msg resp;
    memset(&resp, 0, sizeof(resp));
    resp.hdr = *hdr;
    resp.hdr.rq = 0;
    resp.len = 1; /* at least 1 byte comp code */

    uint8_t *comp = resp.buf;

    pldm_cmd_proc_fn handler = NULL;
    uint8_t (*handler_query)(uint8_t, void **) = NULL;

    uint8_t i;
    for (i = 0; i < ARRAY_SIZE(query_tbl); i++) {
        if (hdr->pldm_type == query_tbl[i].type) {
            handler_query = query_tbl[i].handler_query;
            break;
        }
    }

    if (!handler_query) {
        *comp = PLDM_BASE_CODES_ERROR_UNSUPPORT_PLDM_TYPE;
        goto send_msg;
    }
    
    uint8_t rc = PLDM_ERROR;
    /* found the proper cmd handler in the pldm_type_cmd table */
    rc = handler_query(hdr->cmd, (void **)&handler);
    if (rc == PLDM_ERROR || !handler) {
        *comp = PLDM_BASE_CODES_ERROR_UNSUPPORT_PLDM_CMD;
        goto send_msg;
    }
    
    /* invoke the cmd handler to process */
    rc = handler(pldm_inst, buf + sizeof(*hdr), len - sizeof(*hdr), resp.buf, &resp.len, &ext_params);
    if (rc == PLDM_LATER_RESP)
        return PLDM_SUCCESS;

send_msg:
    /* send the pldm response data */
    resp_len = sizeof(resp.hdr) + resp.len;
    LOG_DBG("resp_len = %d", resp_len);
	return mctp_send_msg((mctp *)pldm_inst->interface, (uint8_t *)&resp, resp_len, ext_params);
}

uint8_t mctp_pldm_send_msg_with_timeout(void *pldm_p, pldm_msg *msg, mctp_ext_param ext_param, 
                        void (*resp_fn)(void *, uint8_t *, uint16_t), void *cb_args,
                        uint16_t timeout_ms, void (*to_fn)(void *), void *to_fn_args)
{
    if (!pldm_p || !msg)
        return PLDM_ERROR;

    pldm_t *pldm_inst = (pldm_t *)pldm_p;

    /* the request should be set inst_id/msg_type/mctp_tag_owner in the header */
    if (msg->hdr.rq) {
        static uint8_t inst_id;

        /* set pldm header */
        msg->hdr.inst_id = (inst_id++) & PLDM_HDR_INST_ID_MASK;
        msg->hdr.msg_type = MCTP_MSG_TYPE_PLDM;
        
        /* set mctp extra parameters */
        ext_param.tag_owner = 1;
    }

    uint16_t send_len = sizeof(msg->hdr) + msg->len;
    LOG_DBG("msg->hdr.inst_id = %x", msg->hdr.inst_id);
    LOG_DBG("msg->hdr.pldm_type = %x", msg->hdr.pldm_type);
    LOG_DBG("msg->hdr.cmd = %x", msg->hdr.cmd);
    LOG_HEXDUMP_DBG((uint8_t *)msg, send_len, "pldm data");
	uint8_t rc = mctp_send_msg((mctp *)pldm_inst->interface, (uint8_t *)msg, send_len, ext_param);

    if (rc == MCTP_ERROR) {
        LOG_WRN("mctp_send_msg error!!");
        return PLDM_ERROR;
    }

    if (msg->hdr.rq) {
        /* if the msg is sending request, should store the msg/resp_fn/cb_args, which are used to handle the response data */
        wait_resp_pldm_msg *wait_msg = (wait_resp_pldm_msg *)malloc(sizeof(*wait_msg));
        if (!wait_msg) {
            printk("malloc FAILED!!\n");
            return PLDM_ERROR;
        }
        memset(wait_msg, 0, sizeof(*wait_msg));

        wait_msg->hdr = msg->hdr;
        wait_msg->resp_fn = resp_fn;
        wait_msg->cb_args = cb_args;
        wait_msg->to_fn = to_fn;
        wait_msg->to_fn_args = to_fn_args;
        
        /* the uptime is int64_t millisecond, don't care overflow */
        wait_msg->exp_timeout_time = k_uptime_get() + ((timeout_ms) ? timeout_ms : PLDM_MSG_TIMEOUT_MS);

        /* TODO: should set timeout? */
        k_mutex_lock(&pldm_inst->wait_recv_resp_list_mutex, K_FOREVER);
        sys_slist_append(&pldm_inst->wait_recv_resp_list, &wait_msg->node);
        k_mutex_unlock(&pldm_inst->wait_recv_resp_list_mutex);
    }

    /* store the msg for waiting response */
    return PLDM_SUCCESS;
}

/* send the pldm cmd through mctp */
uint8_t mctp_pldm_send_msg(void *pldm_p, pldm_msg *msg, mctp_ext_param ext_param, 
                        void (*resp_fn)(void *, uint8_t *, uint16_t), void *cb_args)
{
    return mctp_pldm_send_msg_with_timeout(pldm_p, msg, ext_param, resp_fn, cb_args, 0, NULL, NULL);
}

pldm_t *pldm_init(void *interface, uint8_t user_idx)
{
    pldm_t *pldm_inst = (pldm_t *)malloc(sizeof(*pldm_inst));
    if (!pldm_inst) {
        LOG_WRN("can't alloc pldm_inst!!");
        goto err;
    }

    if (!interface)
        goto err;

    sys_slist_init(&pldm_inst->wait_recv_resp_list);
    if (k_mutex_init(&pldm_inst->wait_recv_resp_list_mutex))
        goto err;
    
    sys_slist_init(&pldm_inst->wait_send_resp_list);
    if (k_mutex_init(&pldm_inst->wait_send_resp_list_mutex))
        goto err;

    pldm_inst->monitor_task = k_thread_create(&pldm_inst->thread_data,
        pldm_inst->monitor_thread_stack,
        K_THREAD_STACK_SIZEOF(pldm_inst->monitor_thread_stack),
        list_monitor,
        pldm_inst, NULL, NULL,
        7, 0, K_MSEC(10)
    );

    if (!pldm_inst->monitor_task) {
        LOG_ERR("create pldm monitor task failed!!");
        goto err;
    }

    uint8_t t_name[PLDM_TASK_NAME_MAX_SIZE];
    snprintf(t_name, sizeof(t_name), "pldm_%d_monitor", user_idx);
    k_thread_name_set(pldm_inst->monitor_task, t_name);

    pldm_inst->interface = interface;
    pldm_inst->user_idx = user_idx;

    return pldm_inst;

err:
    if (pldm_inst)
        free(pldm_inst);
    return PLDM_SUCCESS;
}

uint8_t pldm_deinit(void)
{
    /* TODO: deinit all resource */
    return PLDM_SUCCESS;
}