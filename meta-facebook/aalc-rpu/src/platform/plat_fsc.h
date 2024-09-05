/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PLAT_FSC_H
#define PLAT_FSC_H
#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

#define FSC_TEMP_INVALID 0x8000
#define FSC_RPM_INVALID 0xFFFF

#define FSC_ENABLE 1
#define FSC_DISABLE 0

#define SENSOR_STEPWISE_STEPS_MAX 16

enum FSC_ERROR {
	FSC_ERROR_NONE = 0, // OK status
	FSC_ERROR_UNKNOW,
	FSC_ERROR_OUT_OF_RANGE,
	FSC_ERROR_NOT_FOUND_ZONE_TABLE,
	FSC_ERROR_NOT_FOUND_STEPWISE_TABLE,
	FSC_ERROR_NOT_FOUND_PID_TABLE,
	FSC_ERROR_NULL_ARG,
};

enum FSC_TYPE {
	FSC_TYPE_DISABLE = 0,
	FSC_TYPE_STEPWISE,
	FSC_TYPE_PID,
	FSC_TYPE_BOTH,
	FSC_TYPE_DEFAULT,
};

/* stepwise */
typedef struct {
	uint8_t temp;
	uint8_t duty;
} stepwise_dict;

typedef struct {
	uint8_t sensor_num;
	stepwise_dict step[SENSOR_STEPWISE_STEPS_MAX];
	uint8_t pos_hyst; // positive_hysteresis
	uint8_t neg_hyst; // negative_hysteresis

	// calculate use
	int16_t last_temp;
} stepwise_cfg;

/* pid */
typedef struct {
	uint8_t sensor_num;
	int setpoint;
	uint16_t kp;
	uint8_t ki;
	uint8_t kd;
	uint16_t i_limit_min; // RPM
	uint16_t i_limit_max; // RPM
	uint16_t out_limit_min; // RPM
	uint16_t out_limit_max; // RPM
	uint16_t slew_neg; // RPM/s
	uint16_t slew_pos; // RPM/s
	uint8_t pos_hyst; // positive_hysteresis
	uint8_t neg_hyst; // negative_hysteresis

	// calculate use
	int integral;
	int last_error; //for kd
	uint8_t last_duty;
} pid_cfg;

/* zone control */
typedef struct {
	uint8_t sensor_num;
	uint8_t type; // stepwise or pid
} fsc_type_mapping;

typedef struct {
	stepwise_cfg *sw_tbl;
	uint8_t sw_tbl_num;

	pid_cfg *pid_tbl;
	uint8_t pid_tbl_num;

	float FF_gain; // Duty/RPM
	uint8_t i_limit_min; // Duty
	uint8_t i_limit_max; // Duty
	uint8_t out_limit_min; // Duty
	uint8_t out_limit_max; // Duty
	uint16_t slew_neg; // RPM
	uint16_t slew_pos; // RPM
	uint8_t interval; // sec

	bool (*pre_hook)(uint8_t); // pre_hook function
	uint8_t pre_hook_arg; //  pre_hook arg
	bool (*post_hook)(uint8_t); // post_hook function
	uint8_t post_hook_arg; //  post_hook arg
	uint8_t (*set_duty)(uint8_t, uint8_t); // set duty function
	uint8_t set_duty_arg; //  set_duty arg

	// calculate use
	uint16_t fsc_poll_count;
	uint8_t last_duty;
} zone_cfg;

uint8_t get_fsc_enable_flag(void);
void set_fsc_enable_flag(uint8_t flag);
void fsc_init(void);

#endif