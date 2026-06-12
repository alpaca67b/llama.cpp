/****************************************************************************************
**
** qcc.h
**
** QRNG command and control protocol client C implementation
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include "qcc_errno.h"
#include "qcc_comm_if.h"
#include "cmdctrl_types.h"

/** @addtogroup cmdctrl_client 
 *  @{
 */
#ifndef _SRC_QCC_H_
#define _SRC_QCC_H_

#define QCC_VERSION 0x00000007

typedef enum {
  QCC_SERIAL,
  QCC_TCPUDP
} qcc_comm_type_t;

typedef struct {
  qcc_comm_dev dev;
  qcc_comm_if_t comm;
  cmdctrl_status_t sts;
  int read_size;
  int timeout;
} qcc_hdl_t;

extern int qcc_version(void);
extern int qcc_init(qcc_hdl_t *hdl, qcc_comm_type_t comm, char *dev_id, int timeout_ms, int read_size);
extern int qcc_cmd_get_status(qcc_hdl_t *hdl, cmdctrl_status_t *sts);
extern int qcc_cmd_start(qcc_hdl_t *hdl, cmdctrl_start_mode_t mode, uint8_t *buf, uint16_t len);
extern int qcc_read_continuous(qcc_hdl_t *hdl, uint8_t *buf, int len);
extern int qcc_cmd_stop(qcc_hdl_t *hdl);
extern int qcc_cmd_reset(qcc_hdl_t *hdl);
extern int qcc_cmd_set_config(qcc_hdl_t *hdl, cmdctrl_config_t *cfg);
extern int qcc_cmd_get_config(qcc_hdl_t *hdl, cmdctrl_config_t *cfg);
extern int qcc_cmd_get_statistics(qcc_hdl_t *hdl, cmdctrl_statistics_t *stats);
extern int qcc_cmd_get_info(qcc_hdl_t *hdl, cmdctrl_info_t *info);
extern int qcc_cmd_hash_test_init(qcc_hdl_t *hdl);
extern int qcc_cmd_hash_test_accumulate(qcc_hdl_t *hdl, cmdctrl_hash_test_accumulate_t *acc);
extern int qcc_cmd_hash_test_digest(qcc_hdl_t *hdl, uint8_t *digest);
extern int qcc_cmd_update_init(qcc_hdl_t *hdl, uint32_t img_size, cmdctrl_update_resp_t *upd_resp);
extern int qcc_cmd_update_chunk(qcc_hdl_t *hdl, uint8_t *data, uint16_t len, cmdctrl_update_resp_t *upd_resp);
extern int qcc_close(qcc_hdl_t *hdl);

#endif /* _SRC_QCC_H_ */
/**@}*/