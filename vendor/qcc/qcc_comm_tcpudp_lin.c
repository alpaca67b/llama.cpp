/****************************************************************************************
**
** qcc_comm_tcpudp_lin.c
**
** TCP/UDP communication interface for QCC on Linux
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 10/10/2023
****************************************************************************************/
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include "qcc_comm_tcpudp.h"
#include "qcc_errno.h"


#if QCC_COMM_DEBUG_ENABLE
  #define QCC_DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
  #define QCC_DEBUG_PRINT(...)
#endif

qcc_comm_if_t comm_tcpudp = {
  .init = tcpudp_init,
  .write = tcpudp_write,
  .read = tcpudp_read,
  .flush_in = tcpudp_flush_in,
  .read_random = tcpudp_read_random,
  .close = tcpudp_close
};

typedef struct {
  int tcpfd;
  int udpfd;
  int timeout;
} tcpudp_hdl;

int _set_timeout (tcpudp_hdl *hdl, int timeout_ms) {
  int ret;
  struct timeval tval;
  tval.tv_usec = (timeout_ms%1000)*1000;
  tval.tv_sec = timeout_ms/1000;
  if(timeout_ms == hdl->timeout)
    return QCC_OK;

  if((timeout_ms < 0 || timeout_ms > 60000) && timeout_ms != 0)
    return QCC_INVALID_ARGUMENT;

  ret = setsockopt( hdl->tcpfd,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    &tval,
                    sizeof(tval) );

  if(ret != 0) {
    QCC_DEBUG_PRINT("[TCPUDP] Error setting TCP timeout\n");
    return QCC_ERROR;
  }

  ret = setsockopt( hdl->udpfd,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    &tval,
                    sizeof(tval));

  if(ret != 0) {
    QCC_DEBUG_PRINT("[TCPUDP] Error setting UDP timeout\n");
    return QCC_ERROR;
  }

  QCC_DEBUG_PRINT("[TCPUDP] timeout set\n");
  hdl->timeout = timeout_ms;
  return QCC_OK;
}

int _tcpudp_read_from(qcc_comm_dev *dev, int socket, char *buf, int len, int timeout_ms) {
  tcpudp_hdl *hdl = (tcpudp_hdl *)dev;
  int ret = 0;

  if(_set_timeout(hdl, timeout_ms) != QCC_OK)
      return QCC_ERROR;

  ret = recv(socket, buf, len, 0);
  QCC_DEBUG_PRINT("[TCPUDP] received %02X\n", buf[0]);
  if (ret > 0) {
    QCC_DEBUG_PRINT("[TCPUDP] Bytes received: %d\n", ret);
  } else {
    QCC_DEBUG_PRINT("[TCPUDP] recv failed with error: %d\n", errno);
    return QCC_ERROR;
  }
  //QCC_DEBUG_PRINT("[TCPUDP] Readed, timeout = %d, len = %d\n", timeout_ms, len);
  return (int)ret;
}

int tcpudp_init(qcc_comm_dev *dev, char *dev_id) {
  tcpudp_hdl *hdl;
  int ret;
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct sockaddr_in udp_addr;
  
  if(*dev)
    return QCC_INVALID_ARGUMENT;

  hdl = malloc(sizeof(tcpudp_hdl));
    
  if(!hdl)
    return QCC_MALLOC_ERROR;

  /* Initialize TCP connection */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo(dev_id, NULL, &hints, &result);
  if ( ret != 0 ) {
      QCC_DEBUG_PRINT("[TCPUDP] getaddrinfo failed with error: %d\n", errno);
      free(hdl);
      return QCC_ERROR;
  }

  hdl->tcpfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (hdl->tcpfd == -1) {
      QCC_DEBUG_PRINT("[TCPUDP] socket failed with error: %d\n", errno);
      free(hdl);
      return QCC_ERROR;
  }
  
  struct sockaddr_in *tcp_addr = (struct sockaddr_in *)result->ai_addr;
  tcp_addr->sin_port = htons(QCC_DEFAULT_PORT);
  ret = connect(hdl->tcpfd, result->ai_addr, (int)result->ai_addrlen);
  if (ret == -1) {
      QCC_DEBUG_PRINT("[TCPUDP] Connect failed with error: %d\n", errno);
      freeaddrinfo(result);
      close(hdl->tcpfd);
      free(hdl);
      return QCC_ERROR;
  }
  freeaddrinfo(result);

  /* Start listening to UDP port */
  hdl->udpfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (hdl->udpfd == -1) {
      QCC_DEBUG_PRINT("[TCPUDP] UDP socket failed with error: %d\n", errno);
      close(hdl->tcpfd);
      free(hdl);
      return QCC_ERROR;
  }

  /* Reuse address, if multiple instances of TCP/UDP devices */
  int opt = 1;
  ret = setsockopt( hdl->udpfd,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    (const char *)&opt,
                    sizeof(opt) );
  if (ret != 0) {
      QCC_DEBUG_PRINT("[TCPUDP] Set Socket option SO_REUSEADDR failed with error: %d\n", errno);
      close(hdl->udpfd);
      close(hdl->tcpfd);
      free(hdl);
      return QCC_ERROR;
  }
  
  memset(&udp_addr, 0, sizeof(udp_addr));
  udp_addr.sin_family = AF_INET;
  udp_addr.sin_addr.s_addr = INADDR_ANY;
  udp_addr.sin_port = htons(QCC_DEFAULT_PORT);
  ret = bind(hdl->udpfd, (struct sockaddr*) &udp_addr, sizeof (udp_addr));
  if (ret == -1) {
      QCC_DEBUG_PRINT("[TCPUDP] Bind failed with error: %d\n", errno);
      close(hdl->udpfd);
      close(hdl->tcpfd);
      free(hdl);
      return QCC_ERROR;
  }
  
  hdl->timeout = -1;
  *dev = (qcc_comm_dev *)hdl;
  QCC_DEBUG_PRINT("[TCPUDP] device opened\n");

  return QCC_OK;
}

int tcpudp_write(qcc_comm_dev dev, char *buf, int len) {
  tcpudp_hdl *hdl = (tcpudp_hdl *)dev;
  int ret;

  ret = send( hdl->tcpfd, buf, len, 0 );
  if (ret == -1) {
      QCC_DEBUG_PRINT("[TCPUDP] send failed with error: %d\n", errno);
      return QCC_ERROR;
  }

  if (ret != len){
    QCC_DEBUG_PRINT("[TCPUDP] Error, sent = %d\n", ret);
    return QCC_ERROR;
  }

  QCC_DEBUG_PRINT("[TCPUDP] Written, len = %d\n", ret);
  return ret;
}

int tcpudp_read(qcc_comm_dev dev, char *buf, int len, int timeout_ms) {
  tcpudp_hdl *hdl = (tcpudp_hdl *)dev;
  QCC_DEBUG_PRINT("[TCPUDP] Read %d bytes from TCP\n", len);
  return _tcpudp_read_from(dev, hdl->tcpfd, buf, len, timeout_ms);
}

int tcpudp_read_random(qcc_comm_dev dev, char *buf, int len, int timeout_ms){
  tcpudp_hdl *hdl = (tcpudp_hdl *)dev;
  QCC_DEBUG_PRINT("[TCPUDP] Read %d bytes from UDP\n", len);
  return _tcpudp_read_from(dev, hdl->udpfd, buf, len, timeout_ms);
}

int tcpudp_close(qcc_comm_dev dev) {
  tcpudp_hdl *hdl = (tcpudp_hdl *)dev;
  QCC_DEBUG_PRINT("[TCPUDP] Close");
  if(hdl) {
    close(hdl->tcpfd);
    close(hdl->udpfd);
    free(hdl);
  }
  return QCC_ERROR;
}

int tcpudp_flush_in(qcc_comm_dev dev){
  (void)dev;
  /* Not needed with TCP/UDP*/ 
  return QCC_OK;
}
