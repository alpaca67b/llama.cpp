/****************************************************************************************
**
** qcc_comm_serial_lin.c
**
** Serial communication interface for QCC on Linux
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include <stdio.h> 
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include	<sys/ioctl.h>
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
  int fd;
  int timeout;
} serial_hdl;

static int _set_tty_param(serial_hdl *hdl) {
  struct termios tty;
  memset (&tty, 0, sizeof tty);
  if (tcgetattr (hdl->fd, &tty) != 0) {
    return QCC_ERROR;
  }
  /* Disable all input/output modes */
  tty.c_iflag = 0;
  tty.c_oflag = 0;
  tty.c_lflag = 0;
  /* Set the baudrate to 1000000, this is for Cicada, for USB-CDC (Virtual COM Port) the baudrate settings has no effects on he actual speed */
  cfsetispeed(&tty, B1000000);
  cfsetospeed(&tty, B1000000);
  if (tcsetattr (hdl->fd, TCSANOW, &tty) != 0)
    return QCC_ERROR;

  return QCC_OK;
}

static int _set_timeout (serial_hdl *hdl, int timeout_ms) {
  struct termios tty;
  
  if(timeout_ms == hdl->timeout)
    return QCC_OK;

  if((timeout_ms < 100 || timeout_ms > 25500) && timeout_ms != 0)
    return QCC_INVALID_ARGUMENT;

  memset (&tty, 0, sizeof tty);
  if (tcgetattr (hdl->fd, &tty) != 0) {
    return QCC_ERROR;
  }
  
  /* Set timeout */
  tty.c_cc[VMIN]  = 0 ;
  tty.c_cc[VTIME] = timeout_ms/100;

  if (tcsetattr (hdl->fd, TCSANOW, &tty) != 0)
    return QCC_ERROR;

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

  hdl->fd= open(dev_id, O_RDWR);

  if(!(hdl->fd)) {
    free(hdl);
    return QCC_ERROR;
  } 

  hdl->timeout = -1;
  *dev = (qcc_comm_dev *)hdl;

  QCC_DEBUG_PRINT("[SERIAL] device opened, fd = %d\n", hdl->fd);

  serial_flush_in(*dev);
  if(_set_tty_param(hdl) != QCC_OK) {
    free(hdl);
    return QCC_ERROR;
  }

  return QCC_OK;
}

int serial_write(qcc_comm_dev dev, char *buf, int len) {
  serial_hdl *hdl = dev;
  QCC_DEBUG_PRINT("[SERIAL] Write, fd = %d\n", hdl->fd);
  return write(hdl->fd, buf, len);
}

int serial_read(qcc_comm_dev dev, char *buf, int len, int timeout_ms) {
  serial_hdl *hdl = dev;

  if(_set_timeout(hdl, timeout_ms) != QCC_OK)
      return QCC_ERROR;
  QCC_DEBUG_PRINT("[SERIAL] Read, timeout = %d, len = %d\n", timeout_ms, len);
  return read(hdl->fd, buf, len);
}

int serial_close(qcc_comm_dev dev) {
  serial_hdl *hdl = dev;
  QCC_DEBUG_PRINT("[SERIAL] Close, fd = %d\n", hdl->fd);
  if(hdl) {
    close(hdl->fd);
    free(hdl);
  }
  return QCC_ERROR;
}

int serial_flush_in(qcc_comm_dev dev){
  serial_hdl *hdl = dev;
  int bytes_available = 0;
  char dummy;
  if(ioctl(hdl->fd, FIONREAD, &bytes_available) < 0)
    return QCC_ERROR;

  if(bytes_available){
    QCC_DEBUG_PRINT("[SERIAL] Flush %d bytes from input buffer\n", bytes_available);
    for(int i = 0; i < bytes_available; i++)
      serial_read(dev, &dummy, 1, hdl->timeout);
  }

  return QCC_OK;
}


