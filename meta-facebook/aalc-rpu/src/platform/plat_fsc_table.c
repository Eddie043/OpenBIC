#include "plat_fsc.h"
#include "plat_sensor_table.h"
#include "libutil.h"
#include "plat_pwm.h"

pid_cfg pump_pid_table[] = {
	{
		.sensor_num = 0x12,
		.setpoint = 60,
		.kp = -0.1,
		.ki = -0.2,
		.kd = 0.3,
		.i_limit_min = 20,
		.i_limit_max = 80,
		.pos_hyst = 4,
		.neg_hyst = 3,
	},
	{
		.sensor_num = 0x13,
		.setpoint = 60,
		.kp = -0.3,
		.ki = -0.2,
		.kd = 0.1,
		.i_limit_min = 30,
		.i_limit_max = 70,
		.pos_hyst = 1,
		.neg_hyst = 2,
	}
};

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
	},
};

pid_cfg hex_fan_pid_table[] = {
	{
		.sensor_num = 0x12,
		.setpoint = 60,
		.kp = -0.1,
		.ki = -0.2,
		.kd = 0.3,
		.i_limit_min = 20,
		.i_limit_max = 80,
		.pos_hyst = 4,
		.neg_hyst = 3,
	},
};

stepwise_cfg hex_fan_stepwise_table[] = {
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
	},
};

zone_cfg zone_table[] = {
	{
		.sw_tbl = pump_stepwise_table,
		.sw_tbl_num = ARRAY_SIZE(pump_stepwise_table),
		.pid_tbl = pump_pid_table,
		.pid_tbl_num = ARRAY_SIZE(pump_pid_table),
		.interval = 1,
		.pre_hook = NULL,
		.pre_hook_arg = MANUAL_PWM_E_HEX_FAN,
		.set_duty = NULL,
		.set_duty_arg = PWM_GROUP_E_HEX_FAN,
		.out_limit_min = 20,
		.out_limit_max = 80,
		.slew_neg = 1,
		.slew_pos = 1,
	},
	{
		.sw_tbl = hex_fan_stepwise_table,
		.sw_tbl_num = ARRAY_SIZE(pump_stepwise_table),
		.pid_tbl = hex_fan_pid_table,
		.pid_tbl_num = ARRAY_SIZE(pump_pid_table),
		.interval = 1,
		.pre_hook = NULL,
		.pre_hook_arg = MANUAL_PWM_E_HEX_FAN,
		.set_duty = NULL,
		.set_duty_arg = PWM_GROUP_E_HEX_FAN,
		.out_limit_min = 20,
		.out_limit_max = 80,
		.slew_neg = 1,
		.slew_pos = 1,
	},
};

uint32_t zone_table_size = ARRAY_SIZE(zone_table);