#ifndef _MCTP_CTRL_H
#define _MCTP_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zephyr.h>
#include "mctp.h"

#define MCTP_CTRL_CMD_SET_ENDPOINT_ID 0x01
#define MCTP_CTRL_CMD_GET_ENDPOINT_ID 0x02

#define SET_EID_REQ_OP_SET_EID   0x00
#define SET_EID_REQ_OP_FORCE_EID 0x01

struct _set_eid_req {
    uint8_t op;
    uint8_t eid;
} __attribute__((packed));

struct _set_eid_resp {
    uint8_t completion_code;
    uint8_t status;
    uint8_t eid;
    uint8_t eid_pool_size;
} __attribute__((packed));

typedef struct __attribute__((packed)) {
    uint8_t msg_type : 7;
    uint8_t ic : 1;

    union {
        struct {
            uint8_t inst_id : 5;
            uint8_t rsvd : 1;
            uint8_t d : 1;
            uint8_t rq : 1;
        };
        uint8_t req_d_id;
    };
    
    uint8_t cmd;
} mctp_ctrl_hdr;

typedef struct {
    mctp_ctrl_hdr hdr;
    uint8_t *cmd_data;
    uint16_t cmd_data_len;
    mctp_ext_param ext_param;
    void (*recv_resp_cb_fn)(void *, uint8_t *, uint16_t);
    void *recv_resp_cb_args;
    uint16_t timeout_ms;
    void (*timeout_cb_fn)(void *);
    void *timeout_cb_fn_args;
} mctp_ctrl_msg;

uint8_t mctp_ctrl_cmd_handler(void *mctp_p, uint8_t *buf, uint32_t len, mctp_ext_param ext_params);

uint8_t mctp_ctrl_send_msg(void *mctp_p, mctp_ctrl_msg *msg);

#ifdef __cplusplus
}
#endif

#endif /* _MCTP_CTRL_H */