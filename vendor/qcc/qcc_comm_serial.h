/****************************************************************************************
**
** qcc_comm_serial.h
**
** Header file for serial communication interface
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include "qcc_comm_if.h"

/** @addtogroup cmdctrl_client 
 *  @{
 */
#ifndef _SRC_QCC_COMM_SERIAL_H_
#define _SRC_QCC_COMM_SERIAL_H_

int serial_init(qcc_comm_dev *dev, char *dev_id);
int serial_write(qcc_comm_dev dev, char *buf, int len);
int serial_read(qcc_comm_dev dev, char *buf, int len, int timeout_ms);
int serial_flush_in(qcc_comm_dev dev);
int serial_close(qcc_comm_dev dev);

#endif /* _SRC_QCC_COMM_SERIAL_H_ */
/**@}*/