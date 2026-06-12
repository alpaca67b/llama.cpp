/****************************************************************************************
**
** qcc_comm_if.h
**
** Communication interface for QRNG Cmd&Ctrl client C implementation
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/

/** @addtogroup cmdctrl_client 
 *  @{
 */
#ifndef _SRC_QCC_COMM_IF_H_
#define _SRC_QCC_COMM_IF_H_

typedef void* qcc_comm_dev;

typedef struct {
  int (*init)(qcc_comm_dev *dev, char *dev_id);
  int (*write)(qcc_comm_dev dev, char *buf, int len);
  int (*read)(qcc_comm_dev dev, char *buf, int len, int timeout_ms);
  int (*flush_in)(qcc_comm_dev dev);
  int (*read_random)(qcc_comm_dev dev, char *buf, int len, int timeout_ms);
  int (*close)(qcc_comm_dev dev);
} qcc_comm_if_t;

#endif /* _SRC_QCC_COMM_IF_H_ */
/**@}*/