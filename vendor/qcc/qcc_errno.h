/****************************************************************************************
**
** qcc_errno.h
**
** QRNG command and control protocol client return codes
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/

/** @addtogroup cmdctrl_client 
 *  @{
 */
#ifndef _SRC_QCC_ERRNO_H_
#define _SRC_QCC_ERRNO_H_

/* Function return code */
#define QCC_OK 0
#define QCC_ERROR -1
#define QCC_TIMEOUT -2
#define QCC_INVALID_ARGUMENT -4
#define QCC_MALLOC_ERROR -5
#define QCC_NACK -6

#endif /* _SRC_QCC_ERRNO_H_ */
/**@}*/

