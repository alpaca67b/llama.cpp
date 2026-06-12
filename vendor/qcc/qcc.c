/****************************************************************************************
**
** qcc.c
**
** QRNG command and control protocol client C implementation
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include <string.h>
#include "qcc.h"

#if QCC_DEBUG_ENABLE
  #include <stdio.h>
  #define QCC_DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
  #define QCC_DEBUG_PRINT(...)
#endif

#define STOP_COMMAND_MAX_BYTES_WAIT 16384

/* Communication interfaces */
extern qcc_comm_if_t comm_serial;
extern qcc_comm_if_t comm_tcpudp;

int _qcc_checksum8_calc(uint8_t *data, uint16_t length)
{
  uint8_t checksum = 0;

  for (uint16_t i = 0; i < length; i++) {
    checksum += data[i];
  }

  checksum = ~checksum;

  return checksum;
}

int _get_cmd_payload_len(uint8_t cmd_code) {
  switch(cmd_code) {
    case CMDCTRL_CMD_GET_STATUS: 
    case CMDCTRL_CMD_STOP:
    case CMDCTRL_CMD_GET_CONFIG:
    case CMDCTRL_CMD_GET_STATISTICS:
    case CMDCTRL_CMD_RESET:
    case CMDCTRL_CMD_GET_INFO:
    case CMDCTRL_CMD_HASH_TEST_INIT:
    case CMDCTRL_CMD_HASH_TEST_DIGEST:
      return 0;
    case CMDCTRL_CMD_SET_CONFIG: return sizeof(cmdctrl_config_t);
    case CMDCTRL_CMD_START: return sizeof(cmdctrl_start_payload_t);
    case CMDCTRL_CMD_HASH_TEST_ACCUMULATE: return sizeof(cmdctrl_hash_test_accumulate_t);
    case CMDCTRL_CMD_UPDATE_INIT: return sizeof(cmdctrl_update_init_t);
    case CMDCTRL_CMD_UPDATE_CHUNK: return sizeof(cmdctrl_update_chunk_t);
    default:
      return -1;
  }
}

int _get_success_response_code(uint8_t command) {
  switch(command) {
    case CMDCTRL_CMD_GET_STATUS: 
    case CMDCTRL_CMD_STOP:
    case CMDCTRL_CMD_START:
    case CMDCTRL_CMD_SET_CONFIG:
    case CMDCTRL_CMD_RESET:
      return CMDCTRL_RESP_ACK;
    case CMDCTRL_CMD_GET_CONFIG: return CMDCTRL_RESP_CONFIG;
    case CMDCTRL_CMD_GET_STATISTICS: return CMDCTRL_RESP_STATISTICS;
    case CMDCTRL_CMD_GET_INFO: return CMDCTRL_RESP_INFO;
    case CMDCTRL_CMD_HASH_TEST_INIT: return CMDCTRL_RESP_HASH_TEST_ACK;
    case CMDCTRL_CMD_HASH_TEST_DIGEST: return CMDCTRL_RESP_HASH_TEST_DIGEST;
    case CMDCTRL_CMD_HASH_TEST_ACCUMULATE: return CMDCTRL_RESP_HASH_TEST_ACK;
    case CMDCTRL_CMD_UPDATE_INIT:
    case CMDCTRL_CMD_UPDATE_CHUNK: 
      return CMDCTRL_RESP_UPDATE;
    default:
      return -1;
  }
}

int _get_response_payload_len(uint8_t resp_code) {
  switch(resp_code) {
    case CMDCTRL_RESP_ACK: return sizeof(cmdctrl_status_t);
    case CMDCTRL_RESP_NACK:
    case CMDCTRL_RESP_HASH_TEST_ACK:
      return 0;
    case CMDCTRL_RESP_CONFIG: return sizeof(cmdctrl_config_t);
    case CMDCTRL_RESP_STATISTICS: return sizeof(cmdctrl_statistics_t);
    case CMDCTRL_RESP_INFO: return sizeof(cmdctrl_info_t);
    case CMDCTRL_RESP_HASH_TEST_DIGEST: return QRNG_CMDCTRL_DIGEST_LEN;
    case CMDCTRL_RESP_UPDATE: return sizeof(cmdctrl_update_resp_t);
    default:
        return -1;
  }
}

int _command_response(qcc_hdl_t *hdl, 
                      uint8_t cmd, 
                      uint8_t *cmd_payload,
                      uint8_t *response_payload) {
  int payload_len;
  static cmdctrl_cmdandpayload_t fullcmd;
  fullcmd.command = cmd;
  payload_len = _get_cmd_payload_len(cmd);
  memcpy(&(fullcmd.payload), cmd_payload, payload_len);

  if(hdl->comm.write(hdl->dev, (char*)&fullcmd, 1 + payload_len) != 1 + payload_len) {
    QCC_DEBUG_PRINT("[QCC] ERROR writing command!\n");
    return QCC_ERROR;
  }

  uint8_t response_code;
  if(hdl->comm.read(hdl->dev, (char*)&response_code, 1, hdl->timeout) != 1){
    QCC_DEBUG_PRINT("[QCC] ERROR reading response code byte!\n");
    return QCC_ERROR;
  }

  if(response_code == CMDCTRL_RESP_NACK) {
    return QCC_NACK;
  }

  if((int)response_code != _get_success_response_code(cmd)){
    QCC_DEBUG_PRINT("[QCC] Response code not valid!\n");
    return QCC_ERROR;
  }

  payload_len = _get_response_payload_len(response_code);
  if(payload_len < 0) {
    QCC_DEBUG_PRINT("[QCC] Unknown response code %02X\n", response_code);
    return QCC_ERROR;
  }

  if(payload_len) {
    if(hdl->comm.read(hdl->dev, (char*)response_payload, payload_len, hdl->timeout) != payload_len) {
      QCC_DEBUG_PRINT("[QCC] ERROR reading response payload for response 0x%02X\n", response_code);
      return QCC_ERROR;
    }
  }

  return QCC_OK;
}

int _read_random(qcc_hdl_t *hdl, uint8_t *buf, int len) {
  int index = 0;
  int to_read;
  int ret = QCC_OK;
  if(len <= 0)
    return QCC_INVALID_ARGUMENT;

  while(index < len) {
    
    if( len - index > hdl->read_size)
      to_read = hdl->read_size;
    else
      to_read = len - index;
    //QCC_DEBUG_PRINT("[QCC] Read random: index: %d, len:%d, to_read:%d\n", index, len, to_read);
    ret = hdl->comm.read_random(hdl->dev, (char*)&(buf[index]), to_read, hdl->timeout);
    if(ret == -1)
      return QCC_ERROR;
    
    index += ret;
  }
  return QCC_OK;
}

int qcc_version(void){
  return (int)QCC_VERSION;
}

int qcc_init(qcc_hdl_t *hdl, qcc_comm_type_t comm, char *dev_id, int timeout_ms, int read_size) {
  switch(comm) {
    case QCC_SERIAL:
      hdl->comm = comm_serial;
      break;
    case QCC_TCPUDP:
      hdl->comm = comm_tcpudp;
      break;
    default:
      return QCC_INVALID_ARGUMENT;
  }

  if(read_size < 1)
    return QCC_INVALID_ARGUMENT;

  hdl->read_size = read_size;
  hdl->timeout = timeout_ms;
  return hdl->comm.init(&(hdl->dev), dev_id);
}

int qcc_cmd_get_status(qcc_hdl_t *hdl, cmdctrl_status_t *sts) {
  int ret;
  ret = _command_response(hdl, CMDCTRL_CMD_GET_STATUS, NULL, (uint8_t *)sts);
  if(ret == 0)
    memcpy(&(hdl->sts),sts, sizeof(cmdctrl_status_t));
  return ret;
}

int qcc_cmd_start(qcc_hdl_t *hdl, cmdctrl_start_mode_t mode, uint8_t *buf, uint16_t len) {
  int ret;
  cmdctrl_start_payload_t payload;
  payload.mode = mode;

  if(mode == CMDCTRL_START_ONE_SHOT) {
    payload.len = len;
    ret = _command_response(hdl, CMDCTRL_CMD_START, (uint8_t *)&payload, (uint8_t *)&(hdl->sts));
    if(ret == 0) {
      return _read_random(hdl, buf, len);
    }
  } else {
    payload.len = 0;
    ret = _command_response(hdl, CMDCTRL_CMD_START, (uint8_t *)&payload, (uint8_t *)&(hdl->sts));
  }

  return ret;
}

int qcc_read_continuous(qcc_hdl_t *hdl, uint8_t *buf, int len) {
  return _read_random(hdl, buf, len);
}

int qcc_cmd_stop(qcc_hdl_t *hdl) {
  /* Write stop command */
  uint8_t cmd = CMDCTRL_CMD_STOP; 
  uint8_t buffer[(1+sizeof(cmdctrl_status_t))*2] = {0};

  //if(hdl->comm.flush_in(hdl->dev) != QCC_OK)
  //  return QCC_ERROR;

  if(hdl->comm.write(hdl->dev, (char*)&cmd, 1) != 1) {
    QCC_DEBUG_PRINT("[QCC] ERROR writing stop command byte!\n");
    return QCC_ERROR;
  }

  /* read data until last byte or timeout*/
  int index = 0;
  int counter = 0;
  int received = 0;
  while(counter < STOP_COMMAND_MAX_BYTES_WAIT) {
    //QCC_DEBUG_PRINT("[QCC] stop wait counter = %d!\n", counter);
    received = hdl->comm.read(hdl->dev, (char*)&(buffer[index]), 1, hdl->timeout);
    if(received == 1) {      
      index = (index+1)%(sizeof(buffer));
    }else{
      break;
    }
    counter++;
  }
 
  if(counter == STOP_COMMAND_MAX_BYTES_WAIT) {
    return QCC_ERROR;
  }

  QCC_DEBUG_PRINT("[QCC] stop, counter=%d, index=%d\n", counter, index);
  int response_index = (index >= sizeof(buffer)/2)? index - sizeof(buffer)/2 : index + sizeof(buffer)/2;
  if(buffer[response_index] == CMDCTRL_RESP_ACK)
    return QCC_OK;
  else
    return QCC_ERROR;
}

int qcc_cmd_reset(qcc_hdl_t *hdl) {
  return _command_response(hdl, CMDCTRL_CMD_RESET, NULL, (uint8_t *)&(hdl->sts));
}

int qcc_cmd_set_config(qcc_hdl_t *hdl, cmdctrl_config_t *cfg) {
  return _command_response(hdl, CMDCTRL_CMD_SET_CONFIG, (uint8_t *)cfg, (uint8_t *)&(hdl->sts));
}

int qcc_cmd_get_config(qcc_hdl_t *hdl, cmdctrl_config_t *cfg) {
  return _command_response(hdl, CMDCTRL_CMD_GET_CONFIG, NULL, (uint8_t *)cfg);
}

int qcc_cmd_get_statistics(qcc_hdl_t *hdl, cmdctrl_statistics_t *stats) {
  return _command_response(hdl, CMDCTRL_CMD_GET_STATISTICS, NULL, (uint8_t *)stats);
}

int qcc_cmd_get_info(qcc_hdl_t *hdl, cmdctrl_info_t *info) {
  return _command_response(hdl, CMDCTRL_CMD_GET_INFO, NULL, (uint8_t *)info);
}

int qcc_cmd_hash_test_init(qcc_hdl_t *hdl) {
  return _command_response(hdl, CMDCTRL_CMD_HASH_TEST_INIT, NULL, NULL);
}

int qcc_cmd_hash_test_accumulate(qcc_hdl_t *hdl, cmdctrl_hash_test_accumulate_t *acc) {
  return _command_response(hdl, CMDCTRL_CMD_HASH_TEST_ACCUMULATE, (uint8_t *)acc, NULL);
}

int qcc_cmd_hash_test_digest(qcc_hdl_t *hdl, uint8_t *digest) {
  return _command_response(hdl, CMDCTRL_CMD_HASH_TEST_DIGEST, NULL, digest);
}

int qcc_cmd_update_init(qcc_hdl_t *hdl, uint32_t img_size, cmdctrl_update_resp_t *upd_resp){
  static cmdctrl_update_init_t update;
  update.total_length = img_size;
  return _command_response(hdl, CMDCTRL_CMD_UPDATE_INIT, (uint8_t*)&update, (uint8_t*)upd_resp);
}

int qcc_cmd_update_chunk(qcc_hdl_t *hdl, uint8_t *data, uint16_t len, cmdctrl_update_resp_t *upd_resp) {
  cmdctrl_update_chunk_t chunk;
  memset(&chunk,0x00,sizeof(cmdctrl_update_chunk_t));
  if(len > QRNG_CMDCTRL_UPDATE_MAX_CHUNK) {
    QCC_DEBUG_PRINT("Update chunk len cannot exceed %d bytes, given length is %d\n", QRNG_CMDCTRL_UPDATE_MAX_CHUNK, len);
    return QCC_ERROR;
  }
  memcpy(chunk.data, data, len);
  chunk.len = len;
  chunk.checksum = _qcc_checksum8_calc(data, len);
  return _command_response(hdl, CMDCTRL_CMD_UPDATE_CHUNK, (uint8_t*)&chunk, (uint8_t*)upd_resp);
}

int qcc_close(qcc_hdl_t *hdl) {
  return hdl->comm.close(hdl->dev);
}