#include <zephyr.h>
#include <string.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/slist.h>
#include <cmsis_os2.h>
#include <logging/log.h>
#include "pldm.h"
#include "ipmi.h"

LOG_MODULE_DECLARE(pldm);

uint8_t chk_iana(uint8_t *iana)
{
    if (!iana)
        return PLDM_ERROR;

    for (uint8_t i = 0; i < IANA_LEN; i++) {
        if (iana[i] != ((FIANA >> (i * 8)) & 0xFF))
            return PLDM_ERROR;
    }
    
    return PLDM_SUCCESS;
}

uint8_t set_iana(uint8_t *buf, uint8_t buf_len)
{
    if (!buf || buf_len != IANA_LEN)
        return PLDM_ERROR;

    for (uint8_t i = 0; i < IANA_LEN; i++)
        buf[i] = (FIANA >> (i * 8)) & 0xFF;
    
    return PLDM_SUCCESS;
}

static uint8_t cmd_echo(void *pldm_inst, uint8_t *buf, uint16_t len, uint8_t *resp, uint16_t *resp_len, void *ext_param)
{
    if (!pldm_inst || !buf || !resp || !resp_len)
        return PLDM_ERROR;

    struct _cmd_echo_req *req_p = (struct _cmd_echo_req *)buf;
    struct _cmd_echo_resp *resp_p = (struct _cmd_echo_resp *)resp;

    if (chk_iana(req_p->iana) == PLDM_ERROR) {
        resp_p->completion_code = PLDM_BASE_CODES_ERROR_INVALID_DATA;
        return PLDM_SUCCESS;
    }

    set_iana(resp_p->iana, sizeof(resp_p->iana));
    resp_p->completion_code = PLDM_BASE_CODES_SUCCESS;
    memcpy(&resp_p->first_data, &req_p->first_data, len);
    *resp_len = len + 1;
    return PLDM_SUCCESS;
}

static uint8_t ipmi_cmd(void *pldm_inst, uint8_t *buf, uint16_t len, uint8_t *resp, uint16_t *resp_len, void *ext_param)
{
    if (!pldm_inst || !buf || !resp || !resp_len || !ext_param)
        return PLDM_ERROR;

    struct _ipmi_cmd_req *req_p = (struct _ipmi_cmd_req *)buf;
    
    if (chk_iana(req_p->iana) == PLDM_ERROR) {
        LOG_WRN("iana %08x is uncorret", (uint32_t)req_p->iana);
        struct _ipmi_cmd_resp *resp_p = (struct _ipmi_cmd_resp *)resp;
        resp_p->completion_code = PLDM_BASE_CODES_ERROR_INVALID_DATA;
        set_iana(resp_p->iana, sizeof(resp_p->iana));
        return PLDM_SUCCESS;
    }

    LOG_INF("ipmi over pldm, len = %d\n", len);
    LOG_INF("netfn %x, cmd %x", req_p->netfn, req_p->cmd);
    LOG_HEXDUMP_INF(buf, len, "ipmi cmd data");

    ipmi_msg_cfg msg = {0};

    /* set up ipmi data */
    msg.buffer.netfn = req_p->netfn;
    msg.buffer.cmd = req_p->cmd;
    msg.buffer.data_len = len - sizeof(*req_p) + 1;
    memcpy(msg.buffer.data, &req_p->first_data, msg.buffer.data_len);

    /* for ipmi/ipmb service to know the source is pldm */
    msg.buffer.InF_source = PLDM_IFs;

    /* store the pldm_inst/pldm header/mctp_ext_param in the last of buffer data */
    /* those will use for the ipmi/ipmb service that is done the request */
    uint16_t pldm_hdr_ofs = sizeof(msg.buffer.data) - sizeof(pldm_hdr);
    uint16_t mctp_ext_param_ofs = pldm_hdr_ofs - sizeof(mctp_ext_param);
    uint16_t pldm_inst_ofs = mctp_ext_param_ofs - sizeof(pldm_inst);

    /* store the address of pldm_inst in the buffer */
    memcpy(msg.buffer.data + pldm_inst_ofs, &pldm_inst, 4);

    /* store the ext_param in the buffer */
    memcpy(msg.buffer.data + mctp_ext_param_ofs, ext_param, sizeof(mctp_ext_param));

    /* store the pldm header in the buffer */
    memcpy(msg.buffer.data + pldm_hdr_ofs, buf - sizeof(pldm_hdr), sizeof(pldm_hdr));

    while (k_msgq_put(&ipmi_msgq, &msg, K_NO_WAIT) != 0) {
        k_msgq_purge(&ipmi_msgq);
        LOG_WRN("Retrying put ipmi msgq\n");
    }

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