/****************************************************************************************
**
** cmdctrl_types.h
**
** QRNG command and control protocol command and response types
**
** Copyright (c) Crypta Labs 2022
**
** Originated by davide@cryptalabs.com 14/01/2020
****************************************************************************************/
#include <stdint.h>

/** @addtogroup cmdctrl 
 *  @{
 */
#ifndef SRC_CMD_CTRL_TYPES_H_
#define SRC_CMD_CTRL_TYPES_H_

#define QRNG_CMDCTRL_INFO_STR_LEN 24
#define QRNG_CMDCTRL_HASH_TEST_MAX_CHUNK 1024
#define QRNG_CMDCTRL_DIGEST_LEN 32 // SHA256 digest

#define QRNG_CMDCTRL_UPDATE_MAX_CHUNK 1024

#define QRNG_CMDCTRL_UPDATE_MAX_CUSTOM_PAYLOAD 1024

/** Post-process type */
typedef enum {
  /** Apply NIST approved SHA256 conditioning */
  CMDCTRL_POSTPROCESS_SHA256,
  /** Get data after senscap, without post-processing */
  CMDCTRL_POSTPROCESS_RAW_NOISE,
  /** Get raw full samples from sensif */
  CMDCTRL_POSTPROCESS_RAW_SAMPLES
} cmdctrl_postprocess_t;

/** Command codes */
typedef enum {
  /** Get status command code, the command doesn't have any payload */
  CMDCTRL_CMD_GET_STATUS = 0x01,
  /** Start command code, the payload contains the start options (see @ref cmdctrl_start_payload_t) */
  CMDCTRL_CMD_START = 0x04,
  /** Stop command code, the command doesn't have any payload, used to stop the continuous mode */
  CMDCTRL_CMD_STOP = 0x05,
  /** Get configuration command code, the command doesn't have any payload */
  CMDCTRL_CMD_GET_CONFIG = 0x07,
  /** Set configuration command code, the payload contains the QPS configuration (see @ref cmdctrl_config_t) */
  CMDCTRL_CMD_SET_CONFIG = 0x08,
  /** Get Statistics command code, the command doesn't have any payload */
  CMDCTRL_CMD_GET_STATISTICS = 0x09,
  /** Reset command, restart the generation (startup test) and reset the statistics */
  CMDCTRL_CMD_RESET = 0x0A,
  /** Get Info command, read information about FW and HW */
  CMDCTRL_CMD_GET_INFO = 0x0B,

  /** Start HASH test */
  CMDCTRL_CMD_HASH_TEST_INIT=0x20,
  /** Accumulate HASH test */
  CMDCTRL_CMD_HASH_TEST_ACCUMULATE=0x22,
  /** Get digest */
  CMDCTRL_CMD_HASH_TEST_DIGEST=0x23,

  /** Initialize the FW update, so the firmware can prepare to receive the chunks, the payload is the total firmware image length */
  CMDCTRL_CMD_UPDATE_INIT=0x40,
  /** Send a chunk, every chunk has length and checksum, max chunk length is QRNG_CMDCTRL_UPDATE_MAX_CHUNK bytes */
  CMDCTRL_CMD_UPDATE_CHUNK=0x41,

  /** Read data with signature */
  CMDCTRL_CMD_SIGNED_READ=0x51,

  /** Custom command specific for device, every command byte that starts with 0xFx is a custom command 
   * the implementation must include a cmdctrl_custom_if.h implementation and the QRNG_CMDCTRL_CUSTOM must be defined in qrng_config.h
   * The maximum payload length is QRNG_CMDCTRL_UPDATE_MAX_CUSTOM_PAYLOAD
  */
  CMDCTRL_CMD_CUSTOM_MASK=0xF0

} cmdctrl_command_t;

/** Response codes */
typedef enum {
  /** ACK response code, the ACK response contains the status (see @ref cmdctrl_status_t) */
  CMDCTRL_RESP_ACK = 0x11,
  /** NACK response code, error. The response doesn't have any payload */
  CMDCTRL_RESP_NACK = 0x12,
  /** Configuration response code, the response contains the configuration (see @ref cmdctrl_config_t) */
  CMDCTRL_RESP_CONFIG = 0x17,
  /** Statistics response code, the response contains the configuration (see @ref cmdctrl_statistics_t) */
  CMDCTRL_RESP_STATISTICS = 0x19,
  /** Get Info response, the response contains information about FW and HW (see @ref cmdctrl_info_t) */
  CMDCTRL_RESP_INFO =0x1B,

  /* HASH test ACK, The response doesn't have any payload */
  CMDCTRL_RESP_HASH_TEST_ACK = 0x21,
  /* HASH test ACK, the response contains the 32 byte SHA256 digest */
  CMDCTRL_RESP_HASH_TEST_DIGEST = 0x24,

  /** Update commands response, the payload contains the update status and the number of remaining bytes
    * for the firmware image transfer 
    */
  CMDCTRL_RESP_UPDATE = 0x43,

  CMDCTRL_RESP_SIGNED_READ=0x52,

} cmdctrl_response_t;

/** Mode for start command */
typedef enum {
#if !QRNG_CMDCTRL_ONE_SHOT_ONLY
  /** continuous mode, the cmdctrl sends out random data continuously */
  CMDCTRL_START_CONTINUOUS = 0x00,
#endif
  /** One shot, the cmdctrl sends the required amount of random data, len parameter of start payload */
  CMDCTRL_START_ONE_SHOT = 0x01
} cmdctrl_start_mode_t;

/** status of the firmware update */
typedef enum {
  CMDCTRL_UPDATE_STS_READY = 0,
  CMDCTRL_UPDATE_STS_TRANSFER_IN_PROGRESS = 1,
  CMDCTRL_UPDATE_STS_TRANSFER_COMPLETED = 2,
  CMDCTRL_UPDATE_STS_ERR_NOT_INITIALIZED = 3,
  CMDCTRL_UPDATE_STS_ERR_MEMORY = 4,
  CMDCTRL_UPDATE_STS_ERR_VERIFICATION = 5,
  CMDCTRL_UPDATE_STS_ERR_INVALID = 6,
} cmdctrl_update_sts_t;

/** Start command payload */
#pragma pack(1)
typedef struct {
  /** Mode: continuous or one-shot */
  uint8_t  mode:8;
  /** Len parameter, used for one-shot mode */
  uint16_t  len:16;
} cmdctrl_start_payload_t;

/** QRNG configuration structure used as payload for 
 * 'set configuration' command and 'get configuration' response.
 * It contains a subset of the QPS configuration options (see @ref qrng_qps_cfg_t)
 */
typedef struct {
  uint8_t  postprocess:8;
#if QRNG_CMDCTRL_FULL_MODE
  float inital_level;
  uint8_t startup_test :1;
  uint8_t auto_calibration :1;
  uint8_t repetition_count :1;
  uint8_t adaptive_proportion :1;
  uint8_t bit_count :1;
  uint8_t generate_on_error :1;
  uint8_t _dummy :2;
  uint8_t n_lsbits :8;
  uint8_t hash_input_size :8;
  uint16_t block_size :16;
  uint16_t autocalibration_target :16;
#endif
} cmdctrl_config_t;

typedef struct {
  uint16_t length;
  uint8_t data[QRNG_CMDCTRL_HASH_TEST_MAX_CHUNK];
} cmdctrl_hash_test_accumulate_t;

typedef struct {
  uint32_t total_length;
} cmdctrl_update_init_t;

typedef struct {
  uint16_t len;
} cmdctrl_signed_read_t;

typedef struct {
  uint16_t len;
  uint8_t data[QRNG_CMDCTRL_UPDATE_MAX_CHUNK];
  uint8_t checksum;
} cmdctrl_update_chunk_t;

/** General command structure */
typedef struct {
  uint8_t command:8;
  union {
    uint8_t bytes[sizeof(cmdctrl_update_chunk_t)];
    cmdctrl_start_payload_t start;
    cmdctrl_config_t config;
    cmdctrl_hash_test_accumulate_t hash_test;
    cmdctrl_update_init_t update_init;
    cmdctrl_update_chunk_t update_chunk;
    cmdctrl_signed_read_t signed_read;
  } payload;
}cmdctrl_cmdandpayload_t;

/** Status, payload of the ACK response */
typedef struct {
  uint8_t initialized :1;
  uint8_t startup_test_in_progress :1;
  uint8_t voltage_low :1;
  uint8_t voltage_high :1;
  uint8_t voltage_undefined :1;
  uint8_t bitcount :1;
  uint8_t repetition_count :1;
  uint8_t adaptive_proportion :1;
  uint32_t ready_bytes :32;
}cmdctrl_status_t;

/** Get statistics response payload  */
typedef struct {
  uint64_t generated_bytes;
  uint32_t repetition_count_failures;
  uint32_t adaptive_proportion_failures;
  uint32_t bitcount_failures;
  uint32_t speed;
#if QRNG_CMDCTRL_FULL_MODE
  uint16_t sensif_average;
  float ledctrl_level;
#endif
}cmdctrl_statistics_t;

/** Info, payload of the get info response */
typedef struct {
  uint32_t core_version;
  uint32_t fw_version;
  char serial[QRNG_CMDCTRL_INFO_STR_LEN];
  char hw_info[QRNG_CMDCTRL_INFO_STR_LEN];
}cmdctrl_info_t;

/** Payload for update commands response, update status and remaining bytes to be sent */
#pragma pack(1)
typedef struct {
  uint8_t sts:8; /* type cmdctrl_update_sts_t */
  uint32_t remaining:32;
}cmdctrl_update_resp_t;

/** General response structure */
typedef struct {
  uint8_t response_code:8;
  union {
#ifdef CMDCTRL_CUSTOM_COMMAND_RESP_MAX_LEN
    uint8_t bytes[CMDCTRL_CUSTOM_COMMAND_RESP_MAX_LEN];
#else
    uint8_t bytes[sizeof(cmdctrl_info_t)];
#endif
    cmdctrl_status_t status;
    cmdctrl_statistics_t stats;
    cmdctrl_config_t config;
    cmdctrl_info_t info;
    cmdctrl_update_resp_t update;
    uint8_t digest[QRNG_CMDCTRL_DIGEST_LEN];
  } payload;
}cmdctrl_responseandpayload_t;
#pragma pack()
#endif /* SRC_CMD_CTRL_TYPES_H_ */
/**@}*/
