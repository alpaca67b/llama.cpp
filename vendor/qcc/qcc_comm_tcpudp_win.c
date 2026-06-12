/****************************************************************************************
**
** qcc_comm_tcpudp_win.c
**
** TCP/UDP communication interface for QCC on Windows
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "qcc_comm_tcpudp.h"
#include "qcc_errno.h"

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

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
  WSADATA wsaData;
  SOCKET tcp;
  SOCKET udp;
  int timeout;
} tcpudp_hdl;

int _set_timeout (tcpudp_hdl *hdl, int timeout_ms) {
  int ret;
  if(timeout_ms == hdl->timeout)
    return QCC_OK;

  if((timeout_ms < 0 || timeout_ms > 60000) && timeout_ms != 0)
    return QCC_INVALID_ARGUMENT;

  ret = setsockopt( hdl->tcp,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    (const char *)&timeout_ms,
                    sizeof(timeout_ms) );

  if(ret != 0) {
    QCC_DEBUG_PRINT("[TCPUDP] Error setting TCP timeout\n");
  }

  ret = setsockopt( hdl->udp,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    (const char *)&timeout_ms,
                    sizeof(timeout_ms) );

  if(ret != 0) {
    QCC_DEBUG_PRINT("[TCPUDP] Error setting UDP timeout\n");
  }

  QCC_DEBUG_PRINT("[TCPUDP] timeout set\n");
  hdl->timeout = timeout_ms;
  return QCC_OK;
}

int _tcpudp_read_from(qcc_comm_dev *dev, SOCKET socket, char *buf, int len, int timeout_ms) {
  tcpudp_hdl *hdl = dev;
  int ret = 0;

  if(_set_timeout(hdl, timeout_ms) != QCC_OK)
      return QCC_ERROR;

  ret = recv(socket, buf, len, 0);
  QCC_DEBUG_PRINT("[TCPUDP] received %02X\n", buf[0]);
  if (ret > 0) {
    QCC_DEBUG_PRINT("[TCPUDP] Bytes received: %d\n", ret);
  } else {
    /* if WSAEMSGSIZE, The message was truncated because the 
      buffer is to small tio handle all the received data but 
      the buffer was filled anyway see: 
      https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-recv
      */
    if(WSAGetLastError() == WSAEMSGSIZE) 
      return (int)len;
    else{
      QCC_DEBUG_PRINT("[TCPUDP] recv failed with error: %d\n", WSAGetLastError());
      return QCC_ERROR;
    }
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

  ret = WSAStartup(MAKEWORD(2,2), &(hdl->wsaData));
  if (ret != 0) {
      QCC_DEBUG_PRINT("[TCPUDP] WSAStartup failed with error: %d\n", ret);
      free(hdl);
      return QCC_ERROR;
  }

  /* Initialize TCP connection */
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo(dev_id, NULL, &hints, &result);
  if ( ret != 0 ) {
      QCC_DEBUG_PRINT("[TCPUDP] getaddrinfo failed with error: %d\n", ret);
      WSACleanup();
      free(hdl);
      return QCC_ERROR;
  }

  hdl->tcp  = socket(result->ai_family, result->ai_socktype,  result->ai_protocol);
  if (hdl->tcp == INVALID_SOCKET) {
      QCC_DEBUG_PRINT("[TCPUDP] socket failed with error: %ld\n", WSAGetLastError());
      freeaddrinfo(result);
      WSACleanup();
      free(hdl);
      return QCC_ERROR;
  }

  struct sockaddr_in *tcp_addr = (struct sockaddr_in *)result->ai_addr;
  tcp_addr->sin_port = htons(QCC_DEFAULT_PORT);
  ret = connect( hdl->tcp, result->ai_addr, (int)result->ai_addrlen);
  if (ret == SOCKET_ERROR) {
      QCC_DEBUG_PRINT("[TCPUDP] Connect failed with error: %ld\n", WSAGetLastError());
      closesocket(hdl->tcp);
      freeaddrinfo(result);
      free(hdl);
      return QCC_ERROR;
  }
  freeaddrinfo(result);

  /* Start listening to UDP port */
  hdl->udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (hdl->udp == INVALID_SOCKET) {
      QCC_DEBUG_PRINT("[TCPUDP] UDP socket failed with error: %ld\n", WSAGetLastError());
      WSACleanup();
      free(hdl);
      return QCC_ERROR;
  }

  /* Reuse address, if multiple instances of TCP/UDP devices */
  int opt = 1;
  ret = setsockopt( hdl->udp,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    (const char *)&opt,
                    sizeof(opt) );
  if (ret == SOCKET_ERROR) {
      QCC_DEBUG_PRINT("[TCPUDP] Set Socket option SO_REUSEADDR failed with error: %ld\n", WSAGetLastError());
      closesocket(hdl->udp);
      closesocket(hdl->tcp);
      WSACleanup();
      free(hdl);
      return QCC_ERROR;
  }

  /* Bind */
  ZeroMemory(&udp_addr, sizeof(udp_addr));
  udp_addr.sin_family = AF_INET;
  udp_addr.sin_addr.s_addr = INADDR_ANY;
  udp_addr.sin_port = htons(QCC_DEFAULT_PORT);
  ret = bind( hdl->udp, (SOCKADDR *) &udp_addr, sizeof (udp_addr));
  if (ret == SOCKET_ERROR) {
      QCC_DEBUG_PRINT("[TCPUDP] Bind failed with error: %ld\n", WSAGetLastError());
      closesocket(hdl->udp);
      closesocket(hdl->tcp);
      WSACleanup();
      free(hdl);
      return QCC_ERROR;
  }
  
  hdl->timeout = -1;
  *dev = (qcc_comm_dev *)hdl;
  QCC_DEBUG_PRINT("[TCPUDP] device opened\n");

  tcpudp_flush_in(*dev);

  return QCC_OK;
}

int tcpudp_write(qcc_comm_dev dev, char *buf, int len) {
  tcpudp_hdl *hdl = dev;
  int ret;

  ret = send( hdl->tcp, buf, len, 0 );
  if (ret == SOCKET_ERROR) {
      QCC_DEBUG_PRINT("[TCPUDP] send failed with error: %d\n", WSAGetLastError());
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
  tcpudp_hdl *hdl = dev;
  QCC_DEBUG_PRINT("[TCPUDP] Read %d bytes from TCP\n", len);
  return _tcpudp_read_from(dev, hdl->tcp, buf, len, timeout_ms);
}

int tcpudp_read_random(qcc_comm_dev dev, char *buf, int len, int timeout_ms){
  tcpudp_hdl *hdl = dev;
  QCC_DEBUG_PRINT("[TCPUDP] Read %d bytes from UDP\n", len);
  return _tcpudp_read_from(dev, hdl->udp, buf, len, timeout_ms);
}

int tcpudp_close(qcc_comm_dev dev) {
  tcpudp_hdl *hdl = dev;
  QCC_DEBUG_PRINT("[TCPUDP] Close");
  if(hdl) {
    closesocket(hdl->tcp);
    closesocket(hdl->udp);
    WSACleanup();
    free(hdl);
  }
  return QCC_ERROR;
}

int tcpudp_flush_in(qcc_comm_dev dev){
  (void)dev;
  /* Not needed with TCP/UDP*/ 
  return QCC_OK;
}
