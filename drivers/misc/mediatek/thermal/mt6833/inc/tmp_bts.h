/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef __TMP_BTS_H__
#define __TMP_BTS_H__

#define APPLY_PRECISE_NTC_TABLE
#define APPLY_AUXADC_CALI_DATA
#define APPLY_PRECISE_BTS_TEMP

#define AUX_IN0_NTC (0)
#define AUX_IN1_NTC (1)
#define AUX_IN2_NTC (2)
#define AUX_IN3_NTC (3)
#define AUX_IN4_NTC (4)
#define AUX_IN5_NTC (5)
#define AUX_IN6_NTC (6)

#define BTS_RAP_PULL_UP_R		100000 /* 100K, pull up resister */

#define BTS_TAP_OVER_CRITICAL_LOW	4397119 /* base on 100K NTC temp
						 * default value -40 deg
						 */

#define BTS_RAP_PULL_UP_VOLTAGE		1800 /* 1.8V ,pull up voltage */

#define BTS_RAP_NTC_TABLE		7 /* default is NCP15WF104F03RC(100K) */

#define BTS_RAP_ADC_CHANNEL		AUX_IN0_NTC /* default is 0 */

#define BTSMDPA_RAP_PULL_UP_R		100000 /* 100K, pull up resister */

#define BTSMDPA_TAP_OVER_CRITICAL_LOW	4397119 /* base on 100K NTC temp
						 * default value -40 deg
						 */

#define BTSMDPA_RAP_PULL_UP_VOLTAGE	1800 /* 1.8V ,pull up voltage */

#define BTSMDPA_RAP_NTC_TABLE		7 /* default is NCP15WF104F03RC(100K) */

#define BTSMDPA_RAP_ADC_CHANNEL		AUX_IN1_NTC /* default is 1 */


#define BTSNRPA_RAP_PULL_UP_R		100000	/* 100K,pull up resister */
#define BTSNRPA_TAP_OVER_CRITICAL_LOW	4397119	/* base on 100K NTC temp
						 *default value -40 deg
						 */

#define BTSNRPA_RAP_PULL_UP_VOLTAGE	1800	/* 1.8V ,pull up voltage */
#define BTSNRPA_RAP_NTC_TABLE		7

#define BTSNRPA_RAP_ADC_CHANNEL		AUX_IN2_NTC


//+songyuanqiao, ADD, 20210108, S98812AA1 add new NTC configs
/* Add the NTC of charger(RT1704), to detect the temperature of Charge's pmic IC. */
#define WTCHARGER_RAP_PULL_UP_R               100000 /* 100K, pull up resister */

#define WTCHARGER_TAP_OVER_CRITICAL_LOW       4397119 /* base on 100K NTC temp
                                                 * default value -40 deg
                                                 */

#define WTCHARGER_RAP_PULL_UP_VOLTAGE         1800 /* 1.8V ,pull up voltage */

#define WTCHARGER_RAP_NTC_TABLE               7 /* default is NCP15WF104F03RC(100K) */

#define WTCHARGER_RAP_ADC_CHANNEL             AUX_IN3_NTC /* default is 0 */

/* Add the NTC of FLASH_LED(RT1705), to detect the temperature of flash light. */
#define FLASHLIGHT_RAP_PULL_UP_R               100000 /* 100K, pull up resister */

#define FLASHLIGHT_TAP_OVER_CRITICAL_LOW       4397119 /* base on 100K NTC temp
                                                 * default value -40 deg
                                                 */

#define FLASHLIGHT_RAP_PULL_UP_VOLTAGE         1800 /* 1.8V ,pull up voltage */

#define FLASHLIGHT_RAP_NTC_TABLE               7 /* default is NCP15WF104F03RC(100K) */

#define FLASHLIGHT_RAP_ADC_CHANNEL             AUX_IN4_NTC /* default is 0 */


#define TYPEC_THERMAL_RAP_PULL_UP_R               100000 /* 100K, pull up resister */

#define TYPEC_THERMAL_TAP_OVER_CRITICAL_LOW       4397119 /* base on 100K NTC temp
                                                 * default value -40 deg
                                                 */

#define TYPEC_THERMAL_RAP_PULL_UP_VOLTAGE         1800 /* 1.8V ,pull up voltage */

#define TYPEC_THERMAL_RAP_NTC_TABLE               7 /* default is NCP15WF104F03RC(100K) */

#define TYPEC_THERMAL_RAP_ADC_CHANNEL             AUX_IN5_NTC

/* Add the NTC of Main Board(RT1707), to detect the temperature of main board skin. */
#define MBTHERM_RAP_PULL_UP_R               100000 /* 100K, pull up resister */

#define MBTHERM_TAP_OVER_CRITICAL_LOW       4397119 /* base on 100K NTC temp
                                                 * default value -40 deg
                                                 */

#define MBTHERM_RAP_PULL_UP_VOLTAGE         1800 /* 1.8V ,pull up voltage */

#define MBTHERM_RAP_NTC_TABLE               7 /* default is NCP15WF104F03RC(100K) */

#define MBTHERM_RAP_ADC_CHANNEL             AUX_IN6_NTC /* default is 0 */
//-songyuanqiao, ADD, 20210108, S98812AA1 add new NTC configs
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);
extern int IMM_IsAdcInitReady(void);

#endif	/* __TMP_BTS_H__ */
