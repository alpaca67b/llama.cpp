/****************************************************************************************
**
** qcc_comm_serial_win.c
**
** Serial communication interface for QCC on Windows
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include <stdio.h> 
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include "qcc_comm_serial.h"
#include "qcc_errno.h"

#if QCC_COMM_DEBUG_ENABLE
  #define QCC_DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
  #define QCC_DEBUG_PRINT(...)
#endif

qcc_comm_if_t comm_serial = {
  .init = serial_init,
  .write = serial_write,
  .read = serial_read,
  .flush_in = serial_flush_in,
  .read_random = serial_read,
  .close = serial_close
};

typedef struct {
  HANDLE handle;
  int timeout;
} serial_hdl;

static int _set_tty_param(serial_hdl *hdl) {
  /* Set the baudrate to 1000000, this is for Cicada, for USB-CDC (Virtual COM Port) the baudrate settings has no effects on he actual speed */
  DCB state = {0};
  BOOL success;
  success = GetCommState(hdl->handle, &state);
  if (!success) {
    QCC_DEBUG_PRINT("[SERIAL] Failed to Get serial comm state");
    CloseHandle(hdl->handle);
    return QCC_ERROR;
  }
  state.BaudRate = 1000000;
  state.ByteSize = 8;
  state.Parity = NOPARITY;
  state.StopBits = ONESTOPBIT;
  success = SetCommState(hdl->handle, &state);
  if (!success) {
    QCC_DEBUG_PRINT("[SERIAL] Failed to set serial baudrate");
    CloseHandle(hdl->handle);
    return QCC_ERROR;
  }
  return QCC_OK;
}

static int _set_timeout(serial_hdl *hdl, int timeout_ms) {
  
  if(timeout_ms == hdl->timeout)
    return QCC_OK;

  if((timeout_ms < 0 || timeout_ms > 60000) && timeout_ms != 0)
    return QCC_INVALID_ARGUMENT;

  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = timeout_ms? 0 : MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = timeout_ms;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = timeout_ms;
  timeouts.WriteTotalTimeoutMultiplier = 0;
 
  BOOL success = SetCommTimeouts(hdl->handle, &timeouts);
  if (!success)
  {
    QCC_DEBUG_PRINT("[SERIAL] Failed to set serial timeouts");
    CloseHandle(hdl->handle);
    return QCC_ERROR;
  }

  QCC_DEBUG_PRINT("[SERIAL] timeout set\n");
  hdl->timeout = timeout_ms;
  return QCC_OK;
}

int serial_init(qcc_comm_dev *dev, char *dev_id) {
  serial_hdl *hdl;
  if(*dev)
    return QCC_INVALID_ARGUMENT;

  hdl = malloc(sizeof(serial_hdl));
    
  if(!hdl)
    return QCC_MALLOC_ERROR;

  hdl->handle = CreateFileA(dev_id, GENERIC_READ | GENERIC_WRITE, 0, NULL,
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if(hdl->handle == INVALID_HANDLE_VALUE) {
    free(hdl);
    return QCC_ERROR;
  } 

  hdl->timeout = -1;
  *dev = (qcc_comm_dev *)hdl;

  QCC_DEBUG_PRINT("[SERIAL] device opened, handle = %d\n", hdl->handle);

  serial_flush_in(*dev);
  
  if(_set_tty_param(hdl) != QCC_OK) {
    free(hdl);
    return QCC_ERROR;
  }
  return QCC_OK;
}

int serial_write(qcc_comm_dev dev, char *buf, int len) {
  serial_hdl *hdl = dev;
  DWORD written = 0;
  QCC_DEBUG_PRINT("[SERIAL] Write, len = %d\n", len);
  BOOL success = WriteFile(hdl->handle, buf, len, &written, NULL);
  if (!success) {
    return QCC_ERROR;
  }

  if (written != (DWORD) len) {
    return QCC_ERROR;
  }
  
  QCC_DEBUG_PRINT("[SERIAL] Written, handle = %d\n", hdl->handle);
  return (int)len;
}

int serial_read(qcc_comm_dev dev, char *buf, int len, int timeout_ms) {
  serial_hdl *hdl = dev;
  DWORD received = 0;

  //QCC_DEBUG_PRINT("[SERIAL] Read, len = %d\n", len);

  if(_set_timeout(hdl, timeout_ms) != QCC_OK)
      return QCC_ERROR;

  BOOL success = ReadFile(hdl->handle, buf, len, &received, NULL);
  if (!success) {
    return QCC_ERROR;
  }

  if (received != len){
    QCC_DEBUG_PRINT("[SERIAL] Error, received = %d\n", received);
    return QCC_ERROR;
  }

  //QCC_DEBUG_PRINT("[SERIAL] Readed, timeout = %d, len = %d\n", timeout_ms, len);
  return (int)len;
}

int serial_close(qcc_comm_dev dev) {
  serial_hdl *hdl = dev;
  QCC_DEBUG_PRINT("[SERIAL] Close, handle = %d\n", hdl->handle);
  if(hdl) {
    CloseHandle(hdl->handle);
    free(hdl);
  }
  return QCC_ERROR;
}

int serial_flush_in(qcc_comm_dev dev){
  serial_hdl *hdl = dev;
  COMSTAT stat;
  char buffer[1];
  DWORD received = 0;
  BOOL success = ClearCommError(hdl->handle, NULL, &stat);
  if (!success) {
    QCC_DEBUG_PRINT("[SERIAL] Flush , ClearCommError error\n");
    return QCC_ERROR;
  }
  QCC_DEBUG_PRINT("[SERIAL] Flush %d bytes \n", stat.cbInQue);
  for(int i = 0; i < stat.cbInQue; i++)
    ReadFile(hdl->handle, buffer, 1, &received, NULL);

  return QCC_OK;
}
