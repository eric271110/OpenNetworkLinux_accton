/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2014 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 * Thermal Sensor Platform Implementation.
 *
 ***********************************************************/
#include <onlplib/file.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_THERMAL(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

static char* devfiles__[] = { /* must map with onlp_thermal_id */
    NULL,
    "/sys/devices/platform/as1813_128o_thermal*temp1_input",
    "/sys/devices/platform/as1813_128o_thermal*temp2_input",
    "/sys/devices/platform/as1813_128o_thermal*temp3_input",
    "/sys/devices/platform/as1813_128o_thermal*temp4_input",
    "/sys/devices/platform/as1813_128o_thermal*temp5_input",
    "/sys/devices/platform/as1813_128o_thermal*temp6_input",
    "/sys/devices/platform/as1813_128o_thermal*temp7_input",
    "/sys/devices/platform/as1813_128o_thermal*temp8_input",
    "/sys/devices/platform/as1813_128o_thermal*temp9_input",
    "/sys/devices/platform/as1813_128o_thermal*temp10_input",
    "/sys/devices/platform/as1813_128o_thermal*temp11_input",
    "/sys/devices/platform/as1813_128o_thermal*temp12_input",
    "/sys/devices/platform/as1813_128o_thermal*temp13_input",
    "/sys/devices/platform/as1813_128o_thermal*temp14_input",
    "/sys/devices/platform/as1813_128o_thermal*temp15_input",
    "/sys/devices/platform/as1813_128o_psu.0*psu1_temp1_input",
    "/sys/devices/platform/as1813_128o_psu.0*psu1_temp2_input",
    "/sys/devices/platform/as1813_128o_psu.0*psu1_temp3_input",
    "/sys/devices/platform/as1813_128o_psu.1*psu2_temp1_input",
    "/sys/devices/platform/as1813_128o_psu.1*psu2_temp2_input",
    "/sys/devices/platform/as1813_128o_psu.1*psu2_temp3_input",
    "/sys/devices/platform/as1813_128o_psu.2*psu3_temp1_input",
    "/sys/devices/platform/as1813_128o_psu.2*psu3_temp2_input",
    "/sys/devices/platform/as1813_128o_psu.2*psu3_temp3_input",
    "/sys/devices/platform/as1813_128o_psu.3*psu4_temp1_input",
    "/sys/devices/platform/as1813_128o_psu.3*psu4_temp2_input",
    "/sys/devices/platform/as1813_128o_psu.3*psu4_temp3_input"
};

/* Static values */
static onlp_thermal_info_t tinfo[] = {
    { }, /* Not used */
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BROAD), "CB_FrontRight_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BROAD), "MB_RearLeft_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BROAD), "MB_FrontRight_temp(0x49)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_4_ON_MAIN_BROAD), "MB_RearRight_temp(0x4A)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_5_ON_MAIN_BROAD), "MB_RearCenter_temp(0x4B)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_6_ON_MAIN_BROAD), "MB_FrontCenter_temp(0x4C)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_7_ON_MAIN_BROAD), "MB_RearCenter_temp(0x4D)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_8_ON_MAIN_BROAD), "MZB_FrontRight_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_9_ON_MAIN_BROAD), "MZB_FrontLeft_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_10_ON_MAIN_BROAD), "MZB_FrontRight_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_11_ON_MAIN_BROAD), "MZB_FrontLeft_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_12_ON_MAIN_BROAD), "FCM0_temp(0x4D)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_13_ON_MAIN_BROAD), "FCM0_temp(0x4E)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_14_ON_MAIN_BROAD), "FCM1_temp(0x4D)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_15_ON_MAIN_BROAD), "FCM1_temp(0x4E)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU1), "PSU-1 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU1_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU1), "PSU-1 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU1_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU1), "PSU-1 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU1_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU2), "PSU-2 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU2_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU2), "PSU-2 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU2_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU2), "PSU-2 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU2_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU3), "PSU-3 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU3_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU3), "PSU-3 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU3_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU3), "PSU-3 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU3_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU4), "PSU-4 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU4_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU4), "PSU-4 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU4_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU4), "PSU-4 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU4_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
    }
};

/*
 * This will be called to intiialize the thermali subsystem.
 */
int
onlp_thermali_init(void)
{
    return ONLP_STATUS_OK;
}

/*
 * Retrieve the information structure for the given thermal OID.
 *
 * If the OID is invalid, return ONLP_E_STATUS_INVALID.
 * If an unexpected error occurs, return ONLP_E_STATUS_INTERNAL.
 * Otherwise, return ONLP_STATUS_OK with the OID's information.
 *
 * Note -- it is expected that you fill out the information
 * structure even if the sensor described by the OID is not present.
 */
int
onlp_thermali_info_get(onlp_oid_t id, onlp_thermal_info_t* info)
{
    int tid;
    VALIDATE(id);

    tid = ONLP_OID_ID_GET(id);
    *info = tinfo[tid];

    return onlp_file_read_int(&info->mcelsius, devfiles__[tid]);
}
