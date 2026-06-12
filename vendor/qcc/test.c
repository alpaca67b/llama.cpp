/****************************************************************************************
**
** test.c
**
** QCC library test
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

typedef struct {
	const char *name;
	int (*func)(void);
	int result;
} test_t;

static qcc_hdl_t *_qcc;
static cmdctrl_status_t _sts;
static cmdctrl_info_t _info;
static cmdctrl_config_t _cfg;
static cmdctrl_statistics_t _stats;

int get_status(void) {
  return qcc_cmd_get_status(_qcc, &_sts);
}

int reset(void) {
	int ret;
	ret = qcc_cmd_get_status(_qcc, &_sts);
	if(ret != QCC_OK)
		return ret;

	ret = qcc_cmd_reset(_qcc);
	if(ret != QCC_OK)
		return ret;

  return ret;
}

int wait_startup(void) {
	int ret;
	do{
		ret = qcc_cmd_get_status(_qcc,&_sts);
		if(ret != QCC_OK)
			return ret;
	}while(_sts.startup_test_in_progress);
  return QCC_OK;
}

int get_info(void) {
	return qcc_cmd_get_info(_qcc, &_info);
}

int get_statistics(void) {
	return qcc_cmd_get_statistics(_qcc, &_stats);
}

int read_data(int size, int continuous) {
	uint8_t *buffer;
	int ret;

	buffer = malloc(size);
	if(buffer == NULL) {
		return QCC_MALLOC_ERROR;
	}

	if(continuous) {
		ret = qcc_read_continuous(_qcc, buffer, size);
	} else {
		ret = qcc_cmd_start(_qcc, CMDCTRL_START_ONE_SHOT, buffer, size);
	}

	free(buffer);
	return ret;
}

int one_shot_1B_to_1kB(void) {
	int ret = QCC_OK;
	int retry = 0;
	int size = 1;
	for(size = 1; size <= 1024; size = size*2){
		retry = 0;
		do{
			qcc_cmd_get_status(_qcc, &_sts);
			retry++;
		}while(_sts.ready_bytes < size && retry <= 10);

		if(retry > 1)
			printf("Retry for %d size: %d\n", size, retry);

		if(read_data(size, 0) != QCC_OK) {
			printf("Fail for %d size\n", size);
			ret = QCC_ERROR;
		}
	}
	return ret;
}

int one_shot_all(void) {
	get_status();
	return read_data(_sts.ready_bytes, 0);
}

int one_shot_NACK(void) {
	get_status();
	return read_data(0, 0) == QCC_NACK ? QCC_OK : QCC_ERROR;
}

int continuous_start_stop(void) {
	int ret;
	ret = qcc_cmd_start(_qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
	if(ret != QCC_OK)
		return ret;
	
	return qcc_cmd_stop(_qcc);
}

int continuous_start_1MB_stop(void) {

	int ret;
	ret = qcc_cmd_start(_qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
	if(ret != QCC_OK)
		return ret;
	
	ret = read_data(1024*1024, 1);
	if(ret != QCC_OK)
		return ret;

	
	return qcc_cmd_stop(_qcc);
}

int continuous_start(void) {
	return qcc_cmd_start(_qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
}

int continuous_read_1kB(void) {
	return read_data(1024, 1);
}

int continuous_read_1MB(void) {
	return read_data(1024*1024, 1);
}

int continuous_stop(void) {
	return qcc_cmd_stop(_qcc);
}

int continuous_start_nack_stop(void) {
	int ret;
	ret = qcc_cmd_start(_qcc, CMDCTRL_START_CONTINUOUS, NULL, 0);
	if(ret != QCC_OK)
		return ret;
	
	ret = read_data(1024, 1);
	if(ret != QCC_OK)
		return ret;

	ret = qcc_cmd_get_status(_qcc, &_sts);
	if(ret == QCC_OK)
		return QCC_ERROR;

	ret = read_data(1024, 1);
	if(ret != QCC_OK)
		return ret;

	qcc_cmd_stop(_qcc);

	return 0;
}

int get_configuration(void) {
	return qcc_cmd_get_config(_qcc, &_cfg);
}

int set_configuration_ok(void) {
	static cmdctrl_config_t old_cfg;
	static cmdctrl_config_t new_cfg;
	int ret;

	ret = qcc_cmd_get_config(_qcc, &old_cfg);
	if (ret != QCC_OK)
		return ret;

	memcpy(&new_cfg, &old_cfg, sizeof(cmdctrl_config_t));
	new_cfg.generate_on_error = ~old_cfg.generate_on_error;

	ret = qcc_cmd_set_config(_qcc, &new_cfg);
	if (ret != QCC_OK)
		return ret;
	ret = qcc_cmd_get_config(_qcc, &_cfg);
	if (ret != QCC_OK)
		return ret;
	if(memcmp(&new_cfg, &_cfg, sizeof(cmdctrl_config_t)) != 0)
		return QCC_ERROR;

	ret = qcc_cmd_set_config(_qcc, &old_cfg);
	if (ret != QCC_OK)
		return ret;

	ret = qcc_cmd_get_config(_qcc, &_cfg);
	if (ret != QCC_OK)
		return ret;

	if(memcmp(&old_cfg, &_cfg, sizeof(cmdctrl_config_t)) != 0)
		return QCC_ERROR;

	return QCC_OK;
}

int set_configuration_nack(void) {
	cmdctrl_config_t new_cfg = {0};
	return qcc_cmd_set_config(_qcc, &new_cfg) == QCC_NACK ? QCC_OK : QCC_ERROR;
}

test_t tests[] = {
	{"Get status", get_status},
	{"Get info", get_info },
	{"Get statistics", get_statistics},
	{"Reset", reset},
	{"Get status after reset", get_status},
	{"Wait Startup test", wait_startup},
	{"One shot: 1B to 1kB", one_shot_1B_to_1kB},
	{"One shot: NACK ", one_shot_NACK },
	{"One shot: all available ", one_shot_all },
	{"2nd Reset", reset},
	{"Continuous Mode - Start and stop", continuous_start_stop},
	{"Continuous Mode - Start, Read 1MB and Stop", continuous_start_1MB_stop},
	{"Continuous Mode - Start", continuous_start},
	{"Continuous Mode - Read 1kB", continuous_read_1kB},
	{"Continuous Mode - Read 1MB", continuous_read_1MB },
	{"Continuous Mode - Stop", continuous_stop },
	{"Continuous Mode - Start, NACK, stop", continuous_start_nack_stop },
	{"Get configuration", get_configuration },
	{"Set configuration - ok", set_configuration_ok },
	{"Set configuration - NACK", set_configuration_nack },
};

#define N_TEST (int)(sizeof(tests)/sizeof(test_t))

void execute_tests(qcc_hdl_t *hdl) {
	int passed = 0;
	_qcc = hdl;


	for(int i = 0; i < N_TEST; i++) {
		printf("Executing test %d of %d ...", i+1, N_TEST);
		tests[i].result = tests[i].func();
		if(tests[i].result == QCC_OK)
			passed++;
		printf("\r%2d : Test \"%s\"\n", tests[i].result, tests[i].name);
	}

	printf("## TEST Environment:\n");
	printf(" QRNG FW version: %08X\n", _info.fw_version);
	printf(" QRNG core version: %08X\n", _info.core_version);
	printf(" QRNG HW: %s\n", _info.hw_info);
	printf(" QCC lib version: %08X\n", qcc_version());
	printf("## TEST RESULTS:\n");
	for(int i = 0; i < N_TEST; i++) {
		printf(" %s : Test \"%s\"\n", tests[i].result == QCC_OK? "PASS" : "FAIL", tests[i].name);
	}

	printf("Passed %d/%d\n", passed, N_TEST);
}
