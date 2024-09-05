#include "plat_fsc.h"
#include "plat_sensor_table.h"
#include "libutil.h"
#include "plat_pwm.h"

pid_cfg pump_pid_table[] = {};
stepwise_cfg pump_stepwise_table[] = {
	{
		.sensor_num = 0x12,
		.step = {
			{10, 10},
			{30, 30},
			{40, 40},
			{50, 50},
			{60, 60},
			{70, 70},
			{80, 80},
			{90, 90},
			{100, 100},
		},
		.pos_hyst = 2,
		.neg_hyst = 2,
		.last_temp = FSC_TEMP_INVALID,
	},
	{
		.sensor_num = 0x13,
		.step = {
			{10, 10},
			{30, 31},
			{40, 41},
			{50, 51},
			{60, 61},
			{70, 71},
			{80, 81},
			{90, 91},
			{100, 100},
		},
		.pos_hyst = 2,
		.neg_hyst = 2,
		.last_temp = FSC_TEMP_INVALID,
	},
	{
		.sensor_num = 0x14,
		.step = {
			{10, 10},
			{30, 32},
			{40, 42},
			{50, 52},
			{60, 62},
			{70, 72},
			{80, 82},
			{90, 92},
			{100, 100},
		},
		.pos_hyst = 2,
		.neg_hyst = 2,
		.last_temp = FSC_TEMP_INVALID,
	},
};
#if 0
pid_cfg hex_fan_pid_table[] = {};
stepwise_cfg hex_fan_stepwise_table[] = {};

pid_cfg rpu_fan_pid_table[] = {};
stepwise_cfg rpu_fan_stepwise_table[] = {};
#endif
zone_cfg zone_table[] = {
	{
		.sw_tbl = pump_stepwise_table,
		.pid_tbl = NULL,
		.FF_gain = 1,
		.interval = 1,
		.pre_hook = set_manual_pwm,
		.pre_hook_arg = MANUAL_PWM_E_HEX_FAN,
		.set_duty = set_pwm_group,
		.set_duty_arg = PWM_GROUP_E_HEX_FAN,
	},
#if 0
	{
		.sw_tbl = hex_fan_stepwise_table,
		.pid_tbl = hex_fan_pid_table,
		.FF_gain = 1,
		.interval = 2,
		.pre_hook = set_manual_pwm,
		.pre_hook_arg = MANUAL_PWM_E_PUMP,
		.set_duty = set_pwm_group,
		.set_duty_arg = PWM_GROUP_E_PUMP,
	},
	{
		.sw_tbl = rpu_fan_stepwise_table,
		.pid_tbl = rpu_fan_pid_table,
		.FF_gain = 1,
		.interval = 1,
		.pre_hook = set_manual_pwm,
		.pre_hook_arg = MANUAL_PWM_E_RPU_FAN,
		.set_duty = set_pwm_group,
		.set_duty_arg = PWM_GROUP_E_RPU_FAN,
	},
#endif
};

uint32_t zone_table_size = ARRAY_SIZE(zone_table);