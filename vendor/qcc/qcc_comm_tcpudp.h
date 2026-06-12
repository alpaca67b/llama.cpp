/****************************************************************************************
**
** qcc_comm_tcpudp.h
**
** Header file for TCP/UDP communication interface
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include "qcc_comm_if.h"

/** @addtogroup cmdctrl_client 
 *  @{
 */
#ifndef _SRC_QCC_COMM_TCPUDP_H_
#define _SRC_QCC_COMM_TCPUDP_H_

#define QCC_DEFAULT_PORT 54936

int tcpudp_init(qcc_comm_dev *dev, char *dev_id);
int tcpudp_write(qcc_comm_dev dev, char *buf, int len);
int tcpudp_read(qcc_comm_dev dev, char *buf, int len, int timeout_ms);
int tcpudp_read_random(qcc_comm_dev dev, char *buf, int len, int timeout_ms);
int tcpudp_flush_in(qcc_comm_dev dev);
int tcpudp_close(qcc_comm_dev dev);

#endif /* _SRC_QCC_COMM_TCPUDP_H_ */
/**@}*/