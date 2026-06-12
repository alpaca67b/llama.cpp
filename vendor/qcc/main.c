/****************************************************************************************
**
** main.c
**
** Cmd&ctrl command line utility and qcc-test
**
** Copyright (c) Crypta Labs 2023
**
** Originated by davide@cryptalabs.com 07/09/2023
****************************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include "qcc.h"
#include "cargs.h"

#define DEFAULT_TIMEOUT_MS 500
#define DEFAULT_READ_SIZE 1760
#define FW_CHUNK_MAX_RETRIES 5

#define SET_POSTPROCESS 0x001
#define SET_INITAL_LEVEL 0x002
#define SET_STARTUP_TEST 0x004
#define SET_AUTO_CALIBRATION 0x008
#define SET_REPETITION_COUNT 0x010
#define SET_ADAPTIVE_PROPORTION 0x020
#define SET_BIT_COUNT 0x040
#define SET_GENERATE_ON_ERROR 0x080
#define SET_N_LSBITS 0x100
#define SET_HASH_INPUT_SIZE 0x200
#define SET_BLOCK_SIZE 0x400
#define SET_AUTOCALIBRATION_TARGET 0x800


static struct cag_option options[] = {
  {.identifier = 'i',
  .access_letters = "i",
  .access_name = "interface",
  .value_name = "INTERFACE",
  .description = "Communication interface, [serial|tcpudp], default serial"},

  {.identifier = 'd',
  .access_letters = "d",
  .access_name = "device",
  .value_name = "DEVICE",
  .description = "QRNG device (e.g /dev/ttyACM0)"},

  {.identifier = 'm',
  .access_letters = NULL,
  .access_name = "timeout",
  .value_name = "MS",
  .description = "Communication timeout milliseconds"},

  {.identifier = 's',
  .access_letters = "s",
  .access_name = "get-status",
  .description = "Read status"},

  {.identifier = 'f',
  .access_letters = "f",
  .access_name = "get-info",
  .description = "Read device information"},

  {.identifier = 'g',
  .access_letters = "g",
  .access_name = "get-config",
  .description = "Read configuration"},

  {.identifier = 't',
  .access_letters = "t",
  .access_name = "get-statistics",
  .description = "Read statistics"},

  {.identifier = 'z',
  .access_letters = NULL,
  .access_name = "start",
  .description = "Start continuous mode"},

  {.identifier = 'r',
  .access_letters = "r",
  .access_name = "read",
  .value_name = "SIZE",
  .description = "Read SIZE bytes of data"},

  {.identifier = 'c',
  .access_letters = "c",
  .access_name = "continuous-mode",
  .description = "Read data in continuous mode"},

  {.identifier = 'u',
  .access_letters = NULL,
  .access_name = "stop",
  .description = "Stop continuous mode"},

  {.identifier = 'o',
  .access_letters = "o",
  .access_name = "out-file",
  .value_name = "FILE",
  .description = "Output file"},

  {.identifier = 'a',
  .access_letters = "a",
  .access_name = "append",
  .description = "Append to output file"},

  {.identifier = 'x',
  .access_letters = "x",
  .access_name = "reset",
  .description = "Reset QRNG"},

  {.identifier = 'P',
  .access_letters = "P",
  .access_name = "postprocess",
  .value_name = "PP",
  .description = "set PP postprocess, 0=SHA256, 1=raw noise, 2=raw_samples"},

  {.identifier = 'L',
  .access_letters = "L",
  .access_name = "led-ctrl",
  .value_name = "VALUE",
  .description = "Set VALUE initial led control level (0-100)"},

  {.identifier = 'A',
  .access_letters = "A",
  .access_name = "auto-calibration",
  .value_name = "ENABLE",
  .description = "Enable/disable auto-calibration"},

  {.identifier = 'S',
  .access_letters = "S",
  .access_name = "startup-test",
  .value_name = "ENABLE",
  .description = "Enable/disable startup test"},

  {.identifier = 'R',
  .access_letters = "R",
  .access_name = "repetition-count",
  .value_name = "ENABLE",
  .description = "Enable/disable repetition count health test"},

  {.identifier = 'D',
  .access_letters = "D",
  .access_name = "adaptive-proportion",
  .value_name = "ENABLE",
  .description = "Enable/disable adaptive proportion health test"},

  {.identifier = 'B',
  .access_letters = "B",
  .access_name = "bit-count",
  .value_name = "ENABLE",
  .description = "Enable/disable bit count health test"},

  {.identifier = 'G',
  .access_letters = "G",
  .access_name = "generate-on-error",
  .value_name = "ENABLE",
  .description = "Enable/disable generate on error"},

  {.identifier = 'N',
  .access_letters = "N",
  .access_name = "n-lsbit",
  .value_name = "LSBITS",
  .description = "Number of LSbits to extract"},

  {.identifier = 'H',
  .access_letters = "H",
  .access_name = "hash-input-size",
  .value_name = "SIZE",
  .description = "SIZE bytes hash input size"},

  {.identifier = 'K',
  .access_letters = "K",
  .access_name = "block-size",
  .value_name = "SIZE",
  .description = "SIZE bytes block size"},
  
  {.identifier = 'T',
  .access_letters = "T",
  .access_name = "target",
  .value_name = "VALUE",
  .description = "Auto-calibration target"},

  {.identifier = 'v',
  .access_letters = "v",
  .access_name = "verbose",
  .description = "print messages on stdout"},

  {.identifier = 'w',
  .access_letters = NULL,
  .access_name = "hash-test",
  .value_name = "INPUT_FILE",
  .description = "Perform hash test on INPUT_FILE"},

  {.identifier = 'U',
   .access_letters = "U",
   .access_name = "update",
   .value_name = "FW_IMAGE",
   .description = "Update firmware"},

  {.identifier = 'k',
  .access_letters = NULL,
  .access_name = "test",
  .description = "Execute QCC library test"},

  {.identifier = 'h',
   .access_letters = "h",
   .access_name = "help",
   .description = "Shows the command help"}
};

struct app_ctx {
  qcc_comm_type_t comm_if;
  const char *device_uri;
  int timeout;
  qcc_hdl_t qcc;
  cmdctrl_status_t sts;
  cmdctrl_config_t cfg;
  cmdctrl_statistics_t stats;
  cmdctrl_info_t info;

  int reset;
  int get_config;
  int get_status;
  int get_stats;
  int read_size;
  int continuous;
  int get_info;
  int start;
  int stop;
  int set_cfg_mask;
  cmdctrl_config_t new_cfg;
  int append;
  const char *output_file;
  int verbose;
  const char *hash_input_file;
  int execute_test;
  const char *fw_image;
};

extern void execute_tests(qcc_hdl_t *hdl);

struct app_ctx ctx;

#define VERBOSE_LOG(...) if(ctx.verbose) printf(__VA_ARGS__);

void print_cfg(cmdctrl_config_t *cfg) {
  VERBOSE_LOG(" postprocess:            %d\n", cfg->postprocess);
  VERBOSE_LOG(" inital_level:           %.02f\n", cfg->inital_level);
  VERBOSE_LOG(" startup_test:           %d\n", cfg->startup_test);
  VERBOSE_LOG(" auto_calibration:       %d\n", cfg->auto_calibration);
  VERBOSE_LOG(" repetition_count:       %d\n", cfg->repetition_count);
  VERBOSE_LOG(" adaptive_proportion:    %d\n", cfg->adaptive_proportion);
  VERBOSE_LOG(" bit_count:              %d\n", cfg->bit_count);
  VERBOSE_LOG(" generate_on_error:      %d\n", cfg->generate_on_error);
  VERBOSE_LOG(" n_lsbits:               %d\n", cfg->n_lsbits);
  VERBOSE_LOG(" hash_input_size:        %d\n", cfg->hash_input_size);
  VERBOSE_LOG(" block_size:             %d\n", cfg->block_size);
  VERBOSE_LOG(" autocalibration_target: %d\n", cfg->autocalibration_target);
}

int main(int argc, char *argv[]) {
  printf("#### QCC command-line tool ####\n");
  printf("#### Copyright (C) Crypta Labs 2023 ####\n");
  int ret;
  char identifier;
  const char *value_str;
  cag_option_context opt_ctx;

  memset(&ctx, 0, sizeof(ctx));
  ctx.timeout = DEFAULT_TIMEOUT_MS;
  ctx.comm_if = QCC_SERIAL;

  cag_option_prepare(&opt_ctx, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&opt_ctx)) {
    identifier = cag_option_get(&opt_ctx);
    switch (identifier) {
      case 'i':
        value_str = cag_option_get_value(&opt_ctx);
        if(strncmp(value_str,"serial",sizeof("serial")) == 0)
          ctx.comm_if = QCC_SERIAL;
        else if(strncmp(value_str,"tcpudp",sizeof("tcpudp")) == 0)
          ctx.comm_if = QCC_TCPUDP;
        else{
          printf("Invalid interface name %s, use: serial | tcpudp\n", value_str);
          return 1;
        }
        break;
      case 'd':
        ctx.device_uri = cag_option_get_value(&opt_ctx);
        break;
      case 'm':
        value_str = cag_option_get_value(&opt_ctx);
        ctx.timeout = (int)atoi(value_str);
        break;
      case 's':
        ctx.get_status = 1;
        break;
      case 'f':
        ctx.get_info = 1;
        break;
      case 'g':
        ctx.get_config = 1;
        break;
      case 't':
        ctx.get_stats = 1;
        break;
      case 'P':
        ctx.set_cfg_mask |= SET_POSTPROCESS;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.postprocess = atoi(value_str);
        break;
      case 'L':
        ctx.set_cfg_mask |= SET_INITAL_LEVEL;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.inital_level = (float)atof(value_str);
        break;
      case 'A':
        ctx.set_cfg_mask |= SET_AUTO_CALIBRATION;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.auto_calibration = atoi(value_str)? 1 : 0;
        break;
      case 'S':
        ctx.set_cfg_mask |= SET_STARTUP_TEST;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.startup_test = atoi(value_str)? 1 : 0;
        break;
      case 'R':
        ctx.set_cfg_mask |= SET_REPETITION_COUNT;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.repetition_count = atoi(value_str)? 1 : 0;
        break;
      case 'D':
        ctx.set_cfg_mask |= SET_ADAPTIVE_PROPORTION;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.adaptive_proportion = atoi(value_str)? 1 : 0;
        break;
      case 'B':
        ctx.set_cfg_mask |= SET_BIT_COUNT;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.bit_count = atoi(value_str)? 1 : 0;
        break;
      case 'G':
        ctx.set_cfg_mask |= SET_GENERATE_ON_ERROR;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.generate_on_error = atoi(value_str)? 1 : 0;
        break;
      case 'N':
        ctx.set_cfg_mask |= SET_N_LSBITS;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.n_lsbits = atoi(value_str);
        break;
      case 'H':
        ctx.set_cfg_mask |= SET_HASH_INPUT_SIZE;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.hash_input_size = atoi(value_str);
        break;
      case 'K':
        ctx.set_cfg_mask |= SET_BLOCK_SIZE;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.block_size = atoi(value_str);
        break;
      case 'T':
        ctx.set_cfg_mask |= SET_AUTOCALIBRATION_TARGET;
        value_str = cag_option_get_value(&opt_ctx);
        ctx.new_cfg.autocalibration_target = atoi(value_str);
        break;
      case 'z':
        ctx.start = 1;
        break;
      case 'r':
        value_str = cag_option_get_value(&opt_ctx);
        ctx.read_size = atoi(value_str);
        break;
      case 'c':
        ctx.continuous = 1;
        break;
      case 'u':
        ctx.stop = 1;
        break;
      case 'o':
        ctx.output_file = cag_option_get_value(&opt_ctx);
        break;
      case 'a':
        ctx.append = 1;
        break;
      case 'x':
        ctx.reset = 1;
        break;
      case 'v':
        ctx.verbose = 1;
        break;
      case 'w':
        ctx.hash_input_file = cag_option_get_value(&opt_ctx);
        break;
      case 'U':
        ctx.fw_image = cag_option_get_value(&opt_ctx);
        break;
      case 'k':
        ctx.execute_test = 1;
        break;
      case 'h':
      default:
        printf("Usage: %s [OPTION]...\n", argv[0]);
        printf("qcc library Command line interface, options:\n\n");
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return EXIT_SUCCESS;
    }
  }

  if(ctx.device_uri == NULL) {
    printf("Must provide a device name with -d|--device!\n");
    return EXIT_FAILURE;
  }

  VERBOSE_LOG("\nInitialize QCC\n");
  ret = qcc_init(&ctx.qcc, ctx.comm_if, (char*)ctx.device_uri, ctx.timeout, DEFAULT_READ_SIZE);
  if(ret != QCC_OK){
    printf("ERROR: initializing QRNG device %s, return=%d\n", ctx.device_uri, ret);
    return EXIT_FAILURE;
  }

  if(ctx.get_status) {
    ret = qcc_cmd_get_status(&ctx.qcc, &ctx.sts);
    if(ret != QCC_OK){
      printf("ERROR: Reading QRNG status, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }

    VERBOSE_LOG("\n## QRNG STATUS:\n");
    VERBOSE_LOG(" Initialized:              %d\n", ctx.sts.initialized);
    VERBOSE_LOG(" startup_test_in_progress: %d\n", ctx.sts.startup_test_in_progress);
    VERBOSE_LOG(" voltage_low:              %d\n", ctx.sts.voltage_low);
    VERBOSE_LOG(" voltage_high:             %d\n", ctx.sts.voltage_high);
    VERBOSE_LOG(" voltage_undefined:        %d\n", ctx.sts.voltage_undefined);
    VERBOSE_LOG(" bitcount:                 %d\n", ctx.sts.bitcount);
    VERBOSE_LOG(" repetition_count:         %d\n", ctx.sts.repetition_count);
    VERBOSE_LOG(" adaptive_proportion:      %d\n", ctx.sts.adaptive_proportion);
    VERBOSE_LOG(" ready_bytes:              %d\n", ctx.sts.ready_bytes);
  }

  if(ctx.get_info) {
    ret = qcc_cmd_get_info(&ctx.qcc, &ctx.info);
    if(ret != QCC_OK){
      printf("ERROR: Reading QRNG info, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("\n## QRNG INFO:\n");
    VERBOSE_LOG(" core version: 0x%04X\n", ctx.info.core_version);
    VERBOSE_LOG(" FW version:   0x%04X\n", ctx.info.fw_version);
    VERBOSE_LOG(" Serial:       %.*s\n", QRNG_CMDCTRL_INFO_STR_LEN, ctx.info.serial);
    VERBOSE_LOG(" HW info:      %.*s\n", QRNG_CMDCTRL_INFO_STR_LEN, ctx.info.hw_info);
  }

  if(ctx.get_config || ctx.set_cfg_mask != 0) {
    ret = qcc_cmd_get_config(&ctx.qcc, &ctx.cfg);
    if(ret != QCC_OK){
      printf("ERROR: Reading QRNG config, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("\n## QRNG CONFIGURATION:\n");
    print_cfg(&ctx.cfg);
  }

  if(ctx.set_cfg_mask) {

    if(ctx.set_cfg_mask & SET_ADAPTIVE_PROPORTION)
      ctx.cfg.adaptive_proportion = ctx.new_cfg.adaptive_proportion;
    if(ctx.set_cfg_mask & SET_AUTO_CALIBRATION)
      ctx.cfg.auto_calibration = ctx.new_cfg.auto_calibration;
    if(ctx.set_cfg_mask & SET_AUTOCALIBRATION_TARGET)
      ctx.cfg.autocalibration_target = ctx.new_cfg.autocalibration_target;
    if(ctx.set_cfg_mask & SET_BIT_COUNT)
      ctx.cfg.bit_count = ctx.new_cfg.bit_count;
    if(ctx.set_cfg_mask & SET_BLOCK_SIZE)
      ctx.cfg.block_size = ctx.new_cfg.block_size;
    if(ctx.set_cfg_mask & SET_GENERATE_ON_ERROR)
      ctx.cfg.generate_on_error = ctx.new_cfg.generate_on_error;
    if(ctx.set_cfg_mask & SET_HASH_INPUT_SIZE)
      ctx.cfg.hash_input_size = ctx.new_cfg.hash_input_size;
    if(ctx.set_cfg_mask & SET_INITAL_LEVEL)
      ctx.cfg.inital_level = ctx.new_cfg.inital_level;
    if(ctx.set_cfg_mask & SET_N_LSBITS)
      ctx.cfg.n_lsbits = ctx.new_cfg.n_lsbits;
    if(ctx.set_cfg_mask & SET_POSTPROCESS)
      ctx.cfg.postprocess = ctx.new_cfg.postprocess;
    if(ctx.set_cfg_mask & SET_REPETITION_COUNT)
      ctx.cfg.repetition_count = ctx.new_cfg.repetition_count;
    if(ctx.set_cfg_mask & SET_STARTUP_TEST)
      ctx.cfg.startup_test = ctx.new_cfg.startup_test;

    ret = qcc_cmd_set_config(&ctx.qcc, &ctx.cfg);
    if(ret != QCC_OK){
      printf("ERROR: Setting QRNG config, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("\n## NEW QRNG CONFIGURATION:\n");
    print_cfg(&ctx.cfg);
  }

  if(ctx.get_stats) {
    ret = qcc_cmd_get_statistics(&ctx.qcc, &ctx.stats);
    if(ret != QCC_OK){
      printf("ERROR: Reading QRNG statistics, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("\n## QRNG STATISTICS:\n");
    VERBOSE_LOG(" generated_bytes:              %lu\n", ctx.stats.generated_bytes);
    VERBOSE_LOG(" repetition_count_failures:    %d\n", ctx.stats.repetition_count_failures);
    VERBOSE_LOG(" adaptive_proportion_failures: %d\n", ctx.stats.adaptive_proportion_failures);
    VERBOSE_LOG(" bitcount_failures:            %d\n", ctx.stats.bitcount_failures);
    VERBOSE_LOG(" speed:                        %d\n", ctx.stats.speed);
    VERBOSE_LOG(" sensif_average:               %d\n", ctx.stats.sensif_average);
    VERBOSE_LOG(" ledctrl_level:                %.02f\n", ctx.stats.ledctrl_level);
  }

  if(ctx.start) {
    ret = qcc_cmd_start(&ctx.qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
    if(ret != QCC_OK){
      printf("ERROR: Start continuous mode, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("Continuous mode started!\n");
  }

  if(ctx.read_size) {
    uint8_t *buffer;
    VERBOSE_LOG("Read %d bytes\n", ctx.read_size);
    buffer = malloc(ctx.read_size);
    if(buffer == NULL) {
      printf("ERROR: buffer allocation, size=%d\n", ctx.read_size);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }

    if(ctx.start || ctx.continuous) {
      ret = qcc_read_continuous(&ctx.qcc, buffer, ctx.read_size);
    } else {
      ret = qcc_cmd_start(&ctx.qcc, CMDCTRL_START_ONE_SHOT, buffer, ctx.read_size);
    }

    if(ret != QCC_OK){
      printf("ERROR: Reading data, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }

    if(ctx.output_file != NULL) {
      FILE *out_f = fopen(ctx.output_file, ctx.append? "ab" : "wb");
      if(out_f != NULL ){
        fwrite(buffer,sizeof(uint8_t), ctx.read_size, out_f);
        fclose(out_f);
      } else {
        printf("ERROR: opening file %s\n", ctx.output_file);
        qcc_close(&ctx.qcc);
        return EXIT_FAILURE;
      }

      VERBOSE_LOG("%d bytes of data written to file %s\n", ctx.read_size, ctx.output_file);
    }

    VERBOSE_LOG("Data= %02x ... %02x\n", buffer[0], buffer[ctx.read_size-1]);
  }

  if(ctx.stop) {
    ret = qcc_cmd_stop(&ctx.qcc);
    if(ret != QCC_OK){
      printf("ERROR: Stop continuous mode, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("Continuous mode STOP!\n");
  }

  if(ctx.reset) {
    ret = qcc_cmd_reset(&ctx.qcc);
    if(ret != QCC_OK){
      printf("ERROR: reset, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    VERBOSE_LOG("Reset done!\n");
  }

  if(ctx.hash_input_file != NULL) {
    cmdctrl_hash_test_accumulate_t acc;
    uint8_t digest[QRNG_CMDCTRL_DIGEST_LEN];
    qcc_cmd_hash_test_digest(&ctx.qcc, digest);
    ret = qcc_cmd_hash_test_init(&ctx.qcc);
    if(ret != QCC_OK){
      printf("ERROR: HASH TEST INIT, return=%d\n", ret);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
    FILE *in_f = fopen(ctx.hash_input_file, "rb");
    if(in_f != NULL ){

      do{
        acc.length = fread(acc.data, 1, QRNG_CMDCTRL_HASH_TEST_MAX_CHUNK, in_f);
        if(acc.length == 0)
          break;
        ret =qcc_cmd_hash_test_accumulate(&ctx.qcc, &acc);
        if(ret != QCC_OK){
          printf("ERROR: HASH TEST ACCUMULATE, return=%d\n", ret);
          fclose(in_f);
          qcc_close(&ctx.qcc);
          return EXIT_FAILURE;
        }
      }while(acc.length == QRNG_CMDCTRL_HASH_TEST_MAX_CHUNK);
      fclose(in_f);

      ret = qcc_cmd_hash_test_digest(&ctx.qcc, digest);
      if(ret != QCC_OK){
        printf("ERROR: HASH TEST DIGEST, return=%d\n", ret);
        qcc_close(&ctx.qcc);
        return EXIT_FAILURE;
      }
      for(int i = 0; i < QRNG_CMDCTRL_DIGEST_LEN; i++)
        printf("%02X", digest[i]);
      printf("\n");
    } else {
      printf("ERROR: opening file %s\n", ctx.hash_input_file);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }
  }

  if(ctx.fw_image) {
    FILE *fw_f = fopen(ctx.fw_image, "rb");
    uint8_t buffer[QRNG_CMDCTRL_UPDATE_MAX_CHUNK];
    uint16_t chunk_len;
    int retry = 0;
    cmdctrl_update_resp_t upd_resp;
    if(fw_f != NULL ){
      uint32_t remaining;
      fseek(fw_f, 0L, SEEK_END); 
      remaining = (uint32_t)ftell(fw_f); 
      fseek(fw_f, 0L, SEEK_SET); 
      VERBOSE_LOG("Initialize update, FW image size %d bytes\n", remaining);
      ret = qcc_cmd_update_init(&ctx.qcc, remaining, &upd_resp);
      if(ret != QCC_OK){
        printf("ERROR initialize update\n");
        qcc_close(&ctx.qcc);
        return EXIT_FAILURE;
      }

      do{
        chunk_len = (uint16_t)fread(buffer, 1, QRNG_CMDCTRL_UPDATE_MAX_CHUNK, fw_f);
        if(chunk_len == 0)
          break;

        retry = 0;
        do {
          ret = qcc_cmd_update_chunk(&ctx.qcc, buffer, chunk_len, &upd_resp);
          if(ret == QCC_OK && 
            (upd_resp.sts == CMDCTRL_UPDATE_STS_TRANSFER_IN_PROGRESS || upd_resp.sts == CMDCTRL_UPDATE_STS_TRANSFER_COMPLETED)){
            VERBOSE_LOG("Chunk len %d written, Update status %d, remaining %d\n", chunk_len, upd_resp.sts, upd_resp.remaining);
            remaining = upd_resp.remaining;
            break;
          }
          
          retry++;
        }while(retry <= FW_CHUNK_MAX_RETRIES);

        if(retry > FW_CHUNK_MAX_RETRIES){
          printf("ERROR Update\n");
          qcc_close(&ctx.qcc);
          return EXIT_FAILURE;
        }

      }while(remaining != 0);

      fclose(fw_f);

      if(remaining != 0){
        printf("ERROR: FW update fail\n");
        qcc_close(&ctx.qcc);
        return EXIT_FAILURE;
      }

      printf("FW UPDATED!\n");

    } else {
      printf("ERROR: opening file %s\n", ctx.fw_image);
      qcc_close(&ctx.qcc);
      return EXIT_FAILURE;
    }

  }

  if(ctx.execute_test) {
    execute_tests(&ctx.qcc);
  }

  qcc_close(&ctx.qcc);
  return EXIT_SUCCESS;
}