#include <zephyr.h>
#include <string.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/slist.h>
#include <cmsis_os2.h>
#include <logging/log.h>
#include "pldm.h"

LOG_MODULE_DECLARE(pldm);

static uint8_t cmd_echo(void *pldm_inst, uint8_t *buf, uint16_t len, uint8_t *resp, uint16_t *resp_len)
{
    if (!pldm_inst || !buf || !resp || !resp_len)
        return PLDM_ERROR;

    struct _cmd_echo_req *req_p = (struct _cmd_echo_req *)buf;
    struct _cmd_echo_resp *resp_p = (struct _cmd_echo_resp *)resp;
    resp_p->completion_code = PLDM_BASE_CODES_SUCCESS;
    memcpy(&resp_p->first_data, &req_p->first_data, len);
    *resp_len = len + 1;
    return PLDM_SUCCESS;
}

static uint8_t ipmi_cmd(void *pldm_inst, uint8_t *buf, uint16_t len, uint8_t *resp, uint16_t *resp_len)
{
    if (!pldm_inst || !buf || !resp || !resp_len)
        return PLDM_ERROR;

    struct _ipmi_cmd_req *req_p = (struct _ipmi_cmd_req *)buf;
    struct _ipmi_cmd_resp *resp_p = (struct _ipmi_cmd_resp *)resp;

    LOG_DBG("ipmi over pldm");
    LOG_DBG("netfn %x, cmd %x", req_p->netfn, req_p->cmd);

    

    return PLDM_LATER_RESP;
}

/* the last entry shoule be {PLDM_CMD_TBL_TERMINATE_CMD_CODE, NULL} in the cmd table */
static pldm_cmd_handler pldm_oem_cmd_tbl[] = {
    {PLDM_OEM_CMD_ECHO, cmd_echo},
    {PLDM_OEM_IPMI_BRIDGE, ipmi_cmd}
};

uint8_t pldm_oem_handler_query(uint8_t code, void **ret_fn)
{
    if (!ret_fn)
        return PLDM_ERROR;

    pldm_cmd_proc_fn fn = NULL;
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(pldm_oem_cmd_tbl); i++) {
        if (pldm_oem_cmd_tbl[i].cmd_code == code) {
            fn = pldm_oem_cmd_tbl[i].fn;
            break;
        }
    }

    *ret_fn = (void *)fn;
    return fn ? PLDM_SUCCESS : PLDM_ERROR;
}