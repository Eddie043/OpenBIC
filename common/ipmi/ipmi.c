#include <zephyr.h>
#include <kernel.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include <logging/log.h>
#include "ipmi.h"
#include <string.h>
#include "mctp.h"
#include "pldm.h"

LOG_MODULE_REGISTER(ipmi);
#define IPMI_QUEUE_SIZE 5

struct k_thread IPMI_thread;
K_KERNEL_STACK_MEMBER(IPMI_thread_stack, IPMI_THREAD_STACK_SIZE);

char __aligned(4) ipmi_msgq_buffer[ipmi_buf_len * sizeof(struct ipmi_msg_cfg)];
struct k_msgq ipmi_msgq;

static uint8_t send_msg_by_pldm(ipmi_msg_cfg *msg_cfg)
{
    if (!msg_cfg)
        return 0;

    /* get the mctp/pldm for sending response from buffer */
    uint16_t pldm_hdr_ofs = sizeof(msg_cfg->buffer.data) - sizeof(pldm_hdr);
    uint16_t mctp_ext_param_ofs = pldm_hdr_ofs - sizeof(mctp_ext_param);
    uint16_t mctp_inst_ofs = mctp_ext_param_ofs - 4;

    /* get the mctp_inst */
    mctp *mctp_inst;
    memcpy(&mctp_inst, msg_cfg->buffer.data + mctp_inst_ofs, 4);
    LOG_INF("mctp_inst = %p", mctp_inst);

    LOG_HEXDUMP_INF(msg_cfg->buffer.data + mctp_ext_param_ofs, sizeof(mctp_ext_param), "mctp ext param");

    /* get the pldm hdr for response */
    pldm_hdr *hdr = (pldm_hdr *)(msg_cfg->buffer.data + pldm_hdr_ofs);
    LOG_HEXDUMP_INF(msg_cfg->buffer.data + pldm_hdr_ofs, sizeof(pldm_hdr), "pldm header");

    /* make response data */
    uint8_t resp_buf[PLDM_MAX_DATA_SIZE] = {0};
    pldm_msg resp;
    memset(&resp, 0, sizeof(resp));

    /* pldm header */
    resp.buf = resp_buf;
    resp.hdr = *hdr;
    resp.hdr.rq = 0;

    LOG_DBG("msg_cfg->buffer.data_len = %d", msg_cfg->buffer.data_len);
    LOG_DBG("msg_cfg->buffer.completion_code = %x", msg_cfg->buffer.completion_code);

    /* setup ipmi response data of pldm */
    struct _ipmi_cmd_resp *cmd_resp = (struct _ipmi_cmd_resp *)resp.buf;
    set_iana(cmd_resp->iana, sizeof(cmd_resp->iana));
    cmd_resp->completion_code = PLDM_BASE_CODES_SUCCESS;
    cmd_resp->netfn = msg_cfg->buffer.netfn | 0x01;
    cmd_resp->cmd = msg_cfg->buffer.cmd;
    cmd_resp->ipmi_comp_code = msg_cfg->buffer.completion_code;
    memcpy(&cmd_resp->first_data, msg_cfg->buffer.data, msg_cfg->buffer.data_len);

    resp.len = sizeof(*cmd_resp) - 1 + msg_cfg->buffer.data_len;
    LOG_HEXDUMP_INF(&resp, sizeof(resp.hdr) + resp.len, "pldm resp data");

    memcpy(&resp.ext_param, msg_cfg->buffer.data + mctp_ext_param_ofs, sizeof(resp.ext_param));
    mctp_pldm_send_msg(mctp_inst, &resp);

    return 1;
}

__weak bool pal_is_not_return_cmd(uint8_t netfn, uint8_t cmd)
{
    return 0;
}

void IPMI_CHASSIS_handler(ipmi_msg *msg)
{
    switch (msg->cmd) {
    case CMD_CHASSIS_GET_CHASSIS_STATUS:
        pal_CHASSIS_GET_CHASSIS_STATUS(msg);
        break;
    default:
        printf("invalid chassis msg netfn: %x, cmd: %x\n", msg->netfn, msg->cmd);
        msg->data_len = 0;
        break;
    }
    return;
}

void IPMI_SENSOR_handler(ipmi_msg *msg)
{
    switch (msg->cmd) {
    case CMD_SENSOR_GET_SENSOR_READING:
        pal_SENSOR_GET_SENSOR_READING(msg);
        break;
    default:
        printf("invalid sensor msg netfn: %x, cmd: %x\n", msg->netfn, msg->cmd);
        msg->data_len = 0;
        break;
    }
    return;
}

void IPMI_APP_handler(ipmi_msg *msg)
{
    switch (msg->cmd) {
    case CMD_APP_GET_DEVICE_ID:
        pal_APP_GET_DEVICE_ID(msg);
        break;
    case CMD_APP_COLD_RESET:
        break;
    case CMD_APP_WARM_RESET:
        pal_APP_WARM_RESET(msg);
        break;
    case CMD_APP_GET_SELFTEST_RESULTS:
        pal_APP_GET_SELFTEST_RESULTS(msg);
        break;
    case CMD_APP_GET_SYSTEM_GUID:
        pal_APP_GET_SYSTEM_GUID(msg);
        break;
    case CMD_APP_MASTER_WRITE_READ:
        pal_APP_MASTER_WRITE_READ(msg);
        break;
    default:
        printf("invalid APP msg netfn: %x, cmd: %x\n", msg->netfn, msg->cmd);
        msg->data_len = 0;
        break;
    }

    return;
}

void IPMI_Storage_handler(ipmi_msg *msg)
{
    switch (msg->cmd) {
    case CMD_STORAGE_GET_FRUID_INFO:
        pal_STORAGE_GET_FRUID_INFO(msg);
        break;
    case CMD_STORAGE_READ_FRUID_DATA:
        pal_STORAGE_READ_FRUID_DATA(msg);
        break;
    case CMD_STORAGE_WRITE_FRUID_DATA:
        pal_STORAGE_WRITE_FRUID_DATA(msg);
        break;
    case CMD_STORAGE_RSV_SDR:
        pal_STORAGE_RSV_SDR(msg);
        break;
    case CMD_STORAGE_GET_SDR:
        pal_STORAGE_GET_SDR(msg);
        break;
    default:
        printf("invalid Storage msg netfn: %x, cmd: %x\n", msg->netfn, msg->cmd);
        msg->data_len = 0;
        break;
    }
    return;
}


void IPMI_OEM_handler(ipmi_msg *msg)
{
    switch (msg->cmd) {
    case CMD_OEM_SET_SYSTEM_GUID:
        pal_OEM_SET_SYSTEM_GUID(msg);
        break;
    default:
        printf("invalid OEM msg netfn: %x, cmd: %x\n", msg->netfn, msg->cmd);
        msg->data_len = 0;
        break;
    }
    return;
}

void IPMI_OEM_1S_handler(ipmi_msg *msg)
{
    switch (msg->cmd) {
    case CMD_OEM_MSG_IN:
        break;
    case CMD_OEM_MSG_OUT:
        pal_OEM_MSG_OUT(msg);
        break;
    case CMD_OEM_GET_GPIO:
        pal_OEM_GET_GPIO(msg);
        break;
    case CMD_OEM_SET_GPIO:
        pal_OEM_SET_GPIO(msg);
        break;
    case CMD_OEM_GET_GPIO_CONFIG:
        break;
    case CMD_OEM_SET_GPIO_CONFIG:
        break;
    case CMD_OEM_SEND_INTERRUPT_TO_BMC:
        pal_OEM_SEND_INTERRUPT_TO_BMC(msg);
        break;
    case CMD_OEM_FW_UPDATE:
        pal_OEM_FW_UPDATE(msg);
        break;
    case CMD_OEM_GET_FW_VERSION:
        pal_OEM_GET_FW_VERSION(msg);
        break;
  case CMD_OEM_PECIaccess:
    pal_OEM_PECIaccess(msg);
    break;
  case CMD_OEM_GET_POST_CODE:
    pal_OEM_GET_POST_CODE(msg);
    break;
    case CMD_OEM_SET_JTAG_TAP_STA:
        pal_OEM_SET_JTAG_TAP_STA(msg);
        break;
    case CMD_OEM_JTAG_DATA_SHIFT:
        pal_OEM_JTAG_DATA_SHIFT(msg);
        break;
    case CMD_OEM_SENSOR_POLL_EN:
        pal_OEM_SENSOR_POLL_EN(msg);
    case CMD_OEM_ACCURACY_SENSNR:
        pal_OEM_ACCURACY_SENSNR(msg);
        break;
    case CMD_OEM_GET_SET_GPIO:
        pal_OEM_GET_SET_GPIO(msg);
        break;
    case CMD_OEM_I2C_DEV_SCAN: // debug command
        pal_OEM_I2C_DEV_SCAN(msg);
        break;
    default:
        printf("invalid OEM msg netfn: %x, cmd: %x\n", msg->netfn, msg->cmd);
        msg->data_len = 0;
        break;

    }
    return;
}

ipmi_error IPMI_handler(void *arug0, void *arug1, void *arug2)
{
    uint8_t i;
  ipmi_msg_cfg msg_cfg;

  while(1) {

    k_msgq_get(&ipmi_msgq, &msg_cfg, K_FOREVER);
    if (DEBUG_IPMI) {
      printf("IPMI_handler[%d]: netfn: %x\n", msg_cfg.buffer.data_len, msg_cfg.buffer.netfn);
      for (i = 0; i < msg_cfg.buffer.data_len; i++) {
        printf(" 0x%2x", msg_cfg.buffer.data[i]);
      }
      printf("\n");
    }

    msg_cfg.buffer.completion_code = CC_INVALID_CMD;
    switch (msg_cfg.buffer.netfn) {
      case NETFN_CHASSIS_REQ:
        IPMI_CHASSIS_handler(&msg_cfg.buffer);
        break;
      case NETFN_BRIDGE_REQ:
        // IPMI_BRIDGE_handler();
        break;
      case NETFN_SENSOR_REQ:
        IPMI_SENSOR_handler(&msg_cfg.buffer);
        break;
      case NETFN_APP_REQ:
        IPMI_APP_handler(&msg_cfg.buffer);
        break;
      case NETFN_FIRMWARE_REQ:
        break;
      case NETFN_STORAGE_REQ:
        IPMI_Storage_handler(&msg_cfg.buffer);
        break;
      case NETFN_TRANSPORT_REQ:
        break;
      case NETFN_DCMI_REQ:
        break;
      case NETFN_NM_REQ:
        break;
      case NETFN_OEM_REQ:
        IPMI_OEM_handler(&msg_cfg.buffer);
        break;
      case NETFN_OEM_1S_REQ:
        if ((msg_cfg.buffer.data[0] | (msg_cfg.buffer.data[1] << 8) | (msg_cfg.buffer.data[2] << 16)) == WW_IANA_ID) {
          memcpy(&msg_cfg.buffer.data[0], &msg_cfg.buffer.data[3], msg_cfg.buffer.data_len);
          msg_cfg.buffer.data_len -= 3;
          IPMI_OEM_1S_handler(&msg_cfg.buffer);
          break;
        } else if (pal_is_not_return_cmd(msg_cfg.buffer.netfn, msg_cfg.buffer.cmd)) {
          msg_cfg.buffer.completion_code = CC_INVALID_IANA;
          IPMI_OEM_1S_handler(&msg_cfg.buffer); // Due to command not returning, enter command handler and return with other invalid CC
          break;
        } else {
          msg_cfg.buffer.completion_code = CC_INVALID_IANA;
          msg_cfg.buffer.data_len = 0;
          break;
        }
      default: // invalid net function
        printf("invalid msg netfn: %x, cmd: %x\n", msg_cfg.buffer.netfn, msg_cfg.buffer.cmd);
        msg_cfg.buffer.data_len = 0;
        break;
    }
    
    if (pal_is_not_return_cmd(msg_cfg.buffer.netfn, msg_cfg.buffer.cmd)) {
      ;
    } else {
      ipmb_error status;

      if (msg_cfg.buffer.completion_code != CC_SUCCESS) {
        msg_cfg.buffer.data_len = 0;
      } else if (msg_cfg.buffer.netfn == NETFN_OEM_1S_REQ) {
        uint8_t copy_data[msg_cfg.buffer.data_len];
        memcpy(&copy_data[0], &msg_cfg.buffer.data[0], msg_cfg.buffer.data_len);
        memcpy(&msg_cfg.buffer.data[3], &copy_data[0], msg_cfg.buffer.data_len);
        msg_cfg.buffer.data_len += 3;
        msg_cfg.buffer.data[0] = WW_IANA_ID & 0xFF;
        msg_cfg.buffer.data[1] = (WW_IANA_ID >> 8) & 0xFF;
        msg_cfg.buffer.data[2] = (WW_IANA_ID >> 16) & 0xFF;
      }

      if (msg_cfg.buffer.InF_source == BMC_USB_IFs) {
        USB_write(&msg_cfg.buffer);
      } else if (msg_cfg.buffer.InF_source == HOST_KCS_IFs) {
        ;
      } else if (msg_cfg.buffer.InF_source == PLDM_IFs) {
        /* the message should be passed to source by pldm format */
        send_msg_by_pldm(&msg_cfg);
      } else {
        status = ipmb_send_response(&msg_cfg.buffer, IPMB_inf_index_map[msg_cfg.buffer.InF_source]);
        if (status != ipmb_error_success) {
          printf("IPMI_handler send IPMB resp fail status: %x", status);
        }
      }
    }

  }

}

void ipmi_init(void)
{
  printf("ipmi_init\n"); // rain

  k_msgq_init(&ipmi_msgq, ipmi_msgq_buffer, sizeof(struct ipmi_msg_cfg), ipmi_buf_len);

  k_thread_create(&IPMI_thread, IPMI_thread_stack,
                  K_THREAD_STACK_SIZEOF(IPMI_thread_stack),
                  IPMI_handler,
                  NULL, NULL, NULL,
                  osPriorityBelowNormal, 0, K_NO_WAIT);
  k_thread_name_set(&IPMI_thread, "IPMI_thread");

  // ipmb_init();
}

