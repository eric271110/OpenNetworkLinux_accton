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
 *
 *
 ***********************************************************/
#include <onlp/onlp.h>
#include <onlplib/file.h>
#include <stdarg.h>
#include "platform_lib.h"

#define MAX_HWMON_IDX 20

static int
find_hwmon_idx(const char* base_fmt, ...)
{
    char* file = NULL;
    char path[128];
    va_list ap;
    char base[96];
    int hwmon_idx;

    va_start(ap, base_fmt);
    vsnprintf(base, sizeof(base), base_fmt, ap);
    va_end(ap);

    for (hwmon_idx = 0; hwmon_idx <= MAX_HWMON_IDX; hwmon_idx++) {
        snprintf(path, sizeof(path), "%s/hwmon/hwmon%d/", base, hwmon_idx);

        int ret = onlp_file_find(path, "name", &file);
        AIM_FREE_IF_PTR(file);

        if (ONLP_STATUS_OK == ret)
            return hwmon_idx;
    }

    return -1;
}

enum onlp_fan_dir onlp_get_fan_dir(int fid)
{
    int len = 0;
    int i = 0;
    int hwmon_idx;
    char *str = NULL;
    char *dirs[FAN_DIR_COUNT] = { "F2B", "B2F" };
    char file[32];
    enum onlp_fan_dir dir = FAN_DIR_F2B;

    hwmon_idx = onlp_get_fan_hwmon_idx();
    if (hwmon_idx >= 0) {
        snprintf(file, sizeof(file), "fan%d_dir", fid);
        len = onlp_file_read_str(&str, FAN_SYSFS_FORMAT_1, hwmon_idx, file);

        if (!str || len < 3) {
            AIM_FREE_IF_PTR(str);
            return dir;
        }

        for (i = 0; i < AIM_ARRAYSIZE(dirs); i++) {
            if (strncmp(str, dirs[i], strlen(dirs[i])) == 0) {
                dir = (enum onlp_fan_dir)i;
                break;
            }
        }

        AIM_FREE_IF_PTR(str);
    }

    return dir;
}

int onlp_get_psu_hwmon_idx(int pid)
{
    return find_hwmon_idx("/sys/devices/platform/as1813_128o_psu.%d", pid - 1);
}

int onlp_get_fan_hwmon_idx(void)
{
    return find_hwmon_idx("/sys/devices/platform/as1813_128o_fan");
}
