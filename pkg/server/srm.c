/*
 * Copyright (c) 2020, VeriSilicon Holdings Co., Ltd. All rights SRM_RESERVED
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>

#include "srm.h"

#define DEV_NAME_PREFIX "transcoder"
#define INFO_PATH_PREFIX "/sys/class/misc/transcoder"
#define DEC_UTIL "dec_util"
#define ENC_UTIL "enc_util"
#define DEC_CORE_STATUS "dec_core_status"
#define ENC_CORE_STATUS "enc_core_status"
#define MEM_USAGE "mem_info"
#define POWER_STATUS "power_state"
#define DRVER_INDEX_LOW_POWER 99
#define DRVER_INDEX_BALABCE 100
#define MAX_DEVICES 12
#define MEMORY_AVAILABLE 4 * 1024 //4G
#define MEM_FACTOR_4K_HEVC_DEC 13
#define MEM_FACTOR_2K_HEVC_DEC 25
#define MEM_FACTOR_HEVC_ENC 2

typedef enum
{
    SRM_IDLE = 0,
    SRM_RESERVED = 1,
} SrmCoreStatus;

typedef struct SrmTotalSource
{
    int res_480p30;
    int res_720p30;
    int res_1080p30;
    int res_2160p30;
} SrmTotalSource;

typedef struct SrmDecCoreStatus
{
    int core0;
    int core1;
    int core2;
    int core3;
} SrmDecCoreStatus;

typedef struct DriverStatus
{
    int device_id;
    int dec_usage;
    int enc_usage;
    int mem_usage;
    int used_mem;
    int free_mem;
    int power_state;
    SrmDecCoreStatus dec_core;
    SrmDecCoreStatus enc_core;
    SrmTotalSource comp_res;
} SrmDriverStatus;

typedef struct SrmContext
{
    SrmDriverStatus *driver_status;
    int driver_nums;
    SrmTotalSource total_res;
} SrmContext;

SrmContext *gsrm;

#define STATUSSTR(status) ((status == SRM_IDLE) ? "IDLE" : "RESERVED")
#define MB(x) (x / 1024 / 1024)

static char *mode_name(SrmMode mode)
{
    if (mode == SRM_LOW_POWER)
        return "Low Power Mode";
    if (mode == SRM_BALANCE)
        return "Balance Mode";
    return NULL;
}

static int get_device_numbers(void)
{
    struct dirent **namelist = NULL;
    int count = 0;
    int n;

    n = scandir("/dev/", &namelist, 0, alphasort);
    while (n--)
    {
        if (strncmp(namelist[n]->d_name, DEV_NAME_PREFIX,
                    strlen(DEV_NAME_PREFIX)) == 0)
        {
            printf("Scanned device '%s'\n", namelist[n]->d_name);
            count++;
        }
        free(namelist[n]);
    }

    free(namelist);
    printf("SRM found %d devices\n", count);
    return count;
}

static int get_power_state(int device_id, SrmDriverStatus *status)
{
    char file[255];
    FILE *fp;

    sprintf(file, "%s%d/%s", INFO_PATH_PREFIX, device_id, POWER_STATUS);
    fp = fopen(file, "r");
    if (fp == NULL)
    {
        printf("get_power_state can't open file %s\n", file);
        return -1;
    }
    fscanf(fp, "%d", &status->power_state);
    fclose(fp);

    return 0;
}

static int get_dec_core_status(int device_id, SrmDriverStatus *status)
{
    char file[255];
    char s0[255];
    char s1[255];
    FILE *fp;

    sprintf(file, "%s%d/%s", INFO_PATH_PREFIX, device_id, DEC_CORE_STATUS);
    fp = fopen(file, "r");
    if (fp == NULL)
    {
        printf("get_dec_core_status can't open file %s\n", file);
        return -1;
    }

    fscanf(fp, "%s  %s", s0, s1);
    if (strstr(s0, "core:0"))
    {
        if (strstr(s1, "idle"))
            status->dec_core.core0 = SRM_IDLE;
        else
            status->dec_core.core0 = SRM_RESERVED;
    }

    fscanf(fp, "%s  %s", s0, s1);
    if (strstr(s0, "core:1"))
    {
        if (strstr(s1, "idle"))
            status->dec_core.core1 = SRM_IDLE;
        else
            status->dec_core.core1 = SRM_RESERVED;
    }

    fscanf(fp, "%s  %s", s0, s1);
    if (strstr(s0, "core:2"))
    {
        if (strstr(s1, "idle"))
            status->dec_core.core2 = SRM_IDLE;
        else
            status->dec_core.core2 = SRM_RESERVED;
    }

    fscanf(fp, "%s  %s", s0, s1);
    if (strstr(s0, "core:3"))
    {
        if (strstr(s1, "idle"))
            status->dec_core.core3 = SRM_IDLE;
        else
            status->dec_core.core3 = SRM_RESERVED;
    }

    fclose(fp);

    return 0;
}

static int get_enc_core_status(int device_id, SrmDriverStatus *status)
{
    char file[255];
    char s0[255];
    char s1[255];
    FILE *fp;

    sprintf(file, "%s%d/%s", INFO_PATH_PREFIX, device_id, ENC_CORE_STATUS);
    fp = fopen(file, "r");
    if (fp == NULL)
    {
        printf("get_dec_core_status can't open file %s\n", file);
        return -1;
    }

    fscanf(fp, "%s  %s", s0, s1);
    if (strstr(s0, "core:0"))
    {
        if (strstr(s1, "idle"))
            status->enc_core.core0 = SRM_IDLE;
        else
            status->enc_core.core0 = SRM_RESERVED;
    }

    fscanf(fp, "%s  %s", s0, s1);
    if (strstr(s0, "core:1"))
    {
        if (strstr(s1, "idle"))
            status->enc_core.core1 = SRM_IDLE;
        else
            status->enc_core.core1 = SRM_RESERVED;
    }

    fclose(fp);

    return 0;
}

static int get_dec_usage(int device_id, SrmDriverStatus *status)
{
    char file[255];
    FILE *fp;

    sprintf(file, "%s%d/%s", INFO_PATH_PREFIX, device_id, DEC_UTIL);
    fp = fopen(file, "r");
    if (fp == NULL)
    {
        printf("get_dec_core_status can't open file %s\n", file);
        return -1;
    }
    fscanf(fp, "%d", &status->dec_usage);
    fclose(fp);

    return 0;
}

static int get_enc_usage(int device_id, SrmDriverStatus *status)
{
    char file[255];
    FILE *fp;

    sprintf(file, "%s%d/%s", INFO_PATH_PREFIX, device_id, ENC_UTIL);
    fp = fopen(file, "r");
    if (fp == NULL)
    {
        printf("get_dec_core_status can't open file %s\n", file);
        return -1;
    }
    fscanf(fp, "%d", &status->enc_usage);
    fclose(fp);

    return 0;
}

static int get_mem_usage(int device_id, SrmDriverStatus *status)
{
    char file[255];
    char s0[255];
    char s1[255];
    FILE *fp;
    int used_s0 = 0, used_s1 = 0, free_s0 = 0, free_s1 = 0;

    sprintf(file, "%s%d/%s", INFO_PATH_PREFIX, device_id, MEM_USAGE);
    fp = fopen(file, "r");
    if (fp == NULL)
    {
        printf("get_dec_core_status can't open file %s\n", file);
        return -1;
    }
    fscanf(fp, "%s%d%*s%*s%d%*c%*s%*s%*d%*c%*s%*s", s0, &used_s0, &free_s0);
    fscanf(fp, "%s%d%*s%*s%d%*c%*s%*s%*d%*c%*s%*s", s1, &used_s1, &free_s1);

    if (strncmp(s0, "S0:", 3) == 0 && strncmp(s1, "S1:", 3) == 0)
    {
        status->free_mem = free_s0 + free_s1;
        status->used_mem = used_s0 + used_s1;
    }
    else
    {
        printf("Memory usage file %s format is wrong, s0=%s, free_s0=%d\n", file, s0, free_s0);
        return -1;
    }
    fclose(fp);

    return 0;
}

static int read_driver_status(SrmContext *srm)
{
    int i = 0;
    int ret = 0;
    int num = srm->driver_nums;

    for (i = 0; i < num; i++)
    {
        srm->driver_status[i].device_id = i;
        ret = get_power_state(i, &srm->driver_status[i]);
        if (ret != 0)
        {
            printf("get_power_state %d failed\n", i);
            return -1;
        }
        ret = get_dec_core_status(i, &srm->driver_status[i]);
        if (ret != 0)
        {
            printf("get_dec_core_status %d failed\n", i);
            return -1;
        }
        ret = get_enc_core_status(i, &srm->driver_status[i]);
        if (ret != 0)
        {
            printf("get_enc_core_status %d failed\n", i);
            return -1;
        }
        ret = get_dec_usage(i, &srm->driver_status[i]);
        if (ret != 0)
        {
            printf("get_dec_usage %d failed\n", i);
            return -1;
        }
        ret = get_enc_usage(i, &srm->driver_status[i]);
        if (ret != 0)
        {
            printf("get_enc_usage %d failed\n", i);
            return -1;
        }
        ret = get_mem_usage(i, &srm->driver_status[i]);
        if (ret != 0)
        {
            printf("get_mem_usage %d failed\n", i);
            return -1;
        }
    }
    return 0;
}

void srm_dump_resource(void)
{
    int i = 0;
    SrmDriverStatus *status = NULL;
    SrmContext *srm = gsrm;

    for (i = 0; i < srm->driver_nums; i++)
    {
        status = &srm->driver_status[i];
        printf("dev/%s%d Power=%d, dec=%02d%, enc=%02d%, mem used=%04dMB, free=%04dMB, 480p=%d, 720p=%d, 1080p=%d, 2160p=%d\n",
               DEV_NAME_PREFIX, status->device_id,
               status->power_state,
               status->dec_usage,
               status->enc_usage,
               status->used_mem,
               status->free_mem,
               status->comp_res.res_480p30,
               status->comp_res.res_720p30,
               status->comp_res.res_1080p30,
               status->comp_res.res_2160p30);
    }
    printf("Total: res480p=%d,720p=%d, 1080p=%d, 2160p=%d\n",
           srm->total_res.res_480p30,
           srm->total_res.res_720p30,
           srm->total_res.res_1080p30,
           srm->total_res.res_2160p30);
}

// return srm handle
void srm_init(void)
{
    int ret = 0;

    gsrm = malloc(sizeof(SrmContext));
    if (!gsrm)
    {
        printf("Unable to create SrmContext\n");
        return;
    }

    gsrm->driver_nums = get_device_numbers();
    if (gsrm->driver_nums <= 0)
    {
        printf("No transcoder device was found!\n");
        return;
    }

    gsrm->driver_status = malloc(sizeof(SrmDriverStatus) * gsrm->driver_nums);
    if (!gsrm->driver_status)
    {
        printf("Malloc driver_status failed!\n");
        return;
    }
}

void srm_close(void)
{
    int ret = 0;
    SrmContext *srm = gsrm;

    free(srm->driver_status);
    free(srm);
}

int srm_get_total_resource(int type)
{
    int i = 0;
    long avg_dec[12] = {0};
    long avg_enc[12] = {0};
    long avg_dec_pre[12] = {0};
    long avg_enc_pre[12] = {0};
    int count = 1;
    SrmContext *srm_avg = gsrm;

    while( count < 400){
        read_driver_status(srm_avg);
        memset(&srm_avg->total_res, 0, sizeof(SrmTotalSource));

        if (count % 400 == 0)
        {
            count = 1;
            memset(avg_dec, 0, sizeof(avg_dec));
            memset(avg_enc, 0, sizeof(avg_enc));
        }

        for (i = 0; i < srm_avg->driver_nums; i++)
        {
            SrmDriverStatus *status = &srm_avg->driver_status[i];

            avg_dec[i] += status->dec_usage;
            avg_enc[i] += status->enc_usage;
            if (count % 200 == 0)
            {
                avg_dec[i] /= count;
                avg_enc[i] /= count;
                status->dec_usage = avg_dec[i];
                status->enc_usage = avg_enc[i];
                avg_dec_pre[i] = avg_dec[i];
                avg_enc_pre[i] = avg_enc[i];
            }
            else
            {
                status->dec_usage = avg_dec_pre[i];
                status->enc_usage = avg_enc_pre[i];
            }

            //calculate total
            status->comp_res.res_480p30 = 96 - 108 * status->enc_usage / 100;
            status->comp_res.res_720p30 = 36 - 36 * status->enc_usage / 100;
            status->comp_res.res_1080p30 = 16 - 16 * status->enc_usage / 100;
            status->comp_res.res_2160p30 = 4 - 4 * status->enc_usage / 100;

            srm_avg->total_res.res_480p30 += status->comp_res.res_480p30;
            srm_avg->total_res.res_720p30 += status->comp_res.res_720p30;
            srm_avg->total_res.res_1080p30 += status->comp_res.res_1080p30;
            srm_avg->total_res.res_2160p30 += status->comp_res.res_2160p30;
        }
        count++;
        usleep(5000);
    }
    if( type == 0) return srm_avg->total_res.res_480p30;
    if( type == 1) return srm_avg->total_res.res_720p30;
    if( type == 2) return srm_avg->total_res.res_1080p30;
    if( type == 3) return srm_avg->total_res.res_2160p30;
}

int srm_allocate_resource(SrmMode mode, int req480p, int req720p, int req1080p, int req2160p)
{
    SrmContext *srm = gsrm;
    SrmDriverStatus *driver_status = srm->driver_status;
    int driver_nums = srm->driver_nums;
    int i = 0;
    int total_req = 0, available[MAX_DEVICES] = {0};
    int delta = 100;
    int selected = -1;

    if (mode == SRM_BALANCE){
        delta = 0;
    }

    total_req = req480p +
                req720p * 3 +
                req1080p * 6 +
                req2160p * 24;

    printf("total_req = %d\n", total_req);
    for (i = 0; i < srm->driver_nums; i++) {
        available[i] = srm->driver_status[i].comp_res.res_480p30;
    }

    for (i = 0; i < srm->driver_nums; i++) {
        SrmTotalSource *driver_res = &srm->driver_status[i].comp_res;
        printf("available[%d]=%d", i, available[i]);
        if (req480p > driver_res->res_480p30 ||
            req720p > driver_res->res_720p30 ||
            req1080p > driver_res->res_1080p30 ||
            req2160p > driver_res->res_2160p30)
        {
            printf(" [out of resources]\n");
            continue;
        } else {
            printf("\n");
        }

        if (available[i] > total_req)
        {
            if (mode == SRM_BALANCE)
            {
                //find the maximum delta for BALABCE mode
                if (delta < available[i] - total_req)
                {
                    delta = available[i] - total_req;
                    selected = i;
                }
            }
            else if (mode == SRM_LOW_POWER)
            {
                //find the minimal delta for BALABCE mode
                if (delta > available[i] - total_req)
                {
                    delta = available[i] - total_req;
                    selected = i;
                }
            }
        }
    }

    return selected;
}
