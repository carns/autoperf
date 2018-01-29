/*
 * Copyright (C) 2018 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#define _GNU_SOURCE
#include "darshan-util-config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "darshan-logutils.h"
#include "darshan-apxc-log-format.h"

/* counter name strings for the BGQ module */
#define X(a) #a,
char *apxc_counter_names[] = {
    APXC_RTR_COUNTERS
};
#undef X

static int darshan_log_get_apxc_rec(darshan_fd fd, void** buf_p);
static int darshan_log_put_apxc_rec(darshan_fd fd, void* buf);
static void darshan_log_print_apxc_rec(void *file_rec,
    char *file_name, char *mnt_pt, char *fs_type);
static void darshan_log_print_apxc_description(int ver);
static void darshan_log_print_apxc_rec_diff(void *file_rec1, char *file_name1,
    void *file_rec2, char *file_name2);
static void darshan_log_agg_apxc_recs(void *rec, void *agg_rec, int init_flag);

struct darshan_mod_logutil_funcs apxc_logutils =
{
    .log_get_record = &darshan_log_get_apxc_rec,
    .log_put_record = &darshan_log_put_apxc_rec,
    .log_print_record = &darshan_log_print_apxc_rec,
    .log_print_description = &darshan_log_print_apxc_description,
    .log_print_diff = &darshan_log_print_apxc_rec_diff,
    .log_agg_records = &darshan_log_agg_apxc_recs
};

static int darshan_log_get_apxc_rec(darshan_fd fd, void** buf_p)
{
    struct darshan_apxc_header_record *hdr_rec;
    struct darshan_apxc_router_record *rtr_rec;
    int rec_len;
    char *buffer, *p;
    int i;
    int ret = -1;
    static int first_rec = 1;

    if(fd->mod_map[DARSHAN_APXC_MOD].len == 0)
        return(0);

    if (!*buf_p)
    {
        /* assume this is the largest possible record size */
        buffer = malloc(sizeof(struct darshan_apxc_router_record));
        if (!buffer)
        {
            return(-1);
        }
    }
    else
    {
        buffer = *buf_p;
    }

    if (fd->mod_ver[DARSHAN_APXC_MOD] == 0)
    {
        printf("Either unknown or debug version: %d\n",
               fd->mod_ver[DARSHAN_APXC_MOD]);
        return(0);
    }

    if ((fd->mod_ver[DARSHAN_APXC_MOD] > 0) &&
        (fd->mod_ver[DARSHAN_APXC_MOD] < DARSHAN_APXC_VER))
    {
        /* perform conversion as needed */
    }

    /* v1, current version */
    if (fd->mod_ver[DARSHAN_APXC_MOD] == DARSHAN_APXC_VER)
    {
        if (first_rec)
        {
            rec_len = sizeof(struct darshan_apxc_header_record);
            first_rec = 0;
        }
        else
            rec_len = sizeof(struct darshan_apxc_router_record);

        ret = darshan_log_get_mod(fd, DARSHAN_APXC_MOD, buffer, rec_len);
    }

    if (ret == rec_len)
    {
        if(fd->swap_flag)
        {
            if (rec_len == sizeof(struct darshan_apxc_header_record))
            {
                hdr_rec = (struct darshan_apxc_header_record*)buffer; 
                /* swap bytes if necessary */
                DARSHAN_BSWAP64(&(hdr_rec->base_rec.id));
                DARSHAN_BSWAP64(&(hdr_rec->base_rec.rank));
                DARSHAN_BSWAP64(&(hdr_rec->nblades));
                DARSHAN_BSWAP64(&(hdr_rec->nchassis));
                DARSHAN_BSWAP64(&(hdr_rec->ngroups));
            }
            else
            {
                rtr_rec = (struct darshan_apxc_router_record*)buffer;
                DARSHAN_BSWAP64(&(hdr_rec->base_rec.id));
                DARSHAN_BSWAP64(&(hdr_rec->base_rec.rank));
                for (i = 0; i < 4; i++)
                {
                    DARSHAN_BSWAP64(&rtr_rec->coord[i]);
                }
                for (i = 0; i < APXC_RTR_NUM_INDICES; i++)
                {
                    DARSHAN_BSWAP64(&rtr_rec->counters[i]);
                }
            }
        }
        *buf_p = buffer;
        return(1);
    }
    else if (ret < 0)
    {
        *buf_p = NULL;
        if (buffer) free(buffer);
        return(-1);
    }
    else
    {
        *buf_p = NULL;
        if (buffer) free(buffer);
        return(0);
    }
}

static int darshan_log_put_apxc_rec(darshan_fd fd, void* buf)
{
    int ret;
    int rec_len;
    static int first_rec = 1;

    if (first_rec)
    {
        rec_len = sizeof(struct darshan_apxc_header_record);
        first_rec = 0;
    }
    else
        rec_len = sizeof(struct darshan_apxc_router_record);
    
    ret = darshan_log_put_mod(fd, DARSHAN_APXC_MOD, buf,
                              rec_len, DARSHAN_APXC_VER);
    if(ret < 0)
        return(-1);

    return(0);
}

static void darshan_log_print_apxc_rec(void *rec, char *file_name,
    char *mnt_pt, char *fs_type)
{
    int i;
    static int first_rec = 1;
    struct darshan_apxc_header_record *hdr_rec;
    struct darshan_apxc_router_record *rtr_rec;

    if (first_rec)
    { 
        hdr_rec = rec;
        DARSHAN_COUNTER_PRINT(darshan_module_names[DARSHAN_APXC_MOD],
            hdr_rec->base_rec.rank, hdr_rec->base_rec.id,
            "groups", hdr_rec->ngroups, file_name, "", "");
        DARSHAN_COUNTER_PRINT(darshan_module_names[DARSHAN_APXC_MOD],
            hdr_rec->base_rec.rank, hdr_rec->base_rec.id,
            "chassis", hdr_rec->nchassis, file_name, "", "");
        DARSHAN_COUNTER_PRINT(darshan_module_names[DARSHAN_APXC_MOD],
            hdr_rec->base_rec.rank, hdr_rec->base_rec.id,
            "blades", hdr_rec->nblades, file_name, "", "");
        first_rec = 0;
    }
    else
    {
        rtr_rec = rec;
        char *coord_name[] = {"group", "chassis", "blade", "node"};

        for (i = 0; i < 4; i++)
        {
            DARSHAN_COUNTER_PRINT(darshan_module_names[DARSHAN_APXC_MOD],
                rtr_rec->base_rec.rank, rtr_rec->base_rec.id,
                coord_name[i], rtr_rec->coord[i],
                file_name, "", "");
        }

        for(i = 0; i < APXC_RTR_NUM_INDICES; i++)
        {
            DARSHAN_COUNTER_PRINT(darshan_module_names[DARSHAN_APXC_MOD],
                rtr_rec->base_rec.rank, rtr_rec->base_rec.id,
                apxc_counter_names[i], rtr_rec->counters[i],
                file_name, "", "");
        }
    }

    return;
}

static void darshan_log_print_apxc_description(int ver)
{
    printf("\n# description of APXC counters: %d\n", ver);
    printf("#   groups: total number of groups.\n");
    printf("#   chassis: total number of chassis.\n");
    printf("#   blades: total number of blades.\n");
    printf("#   router:\n");
    printf("#     group:   group this router is in.\n");
    printf("#     chassis: chassies this router is in.\n");
    printf("#     blade:   blade this router is in.\n");
    printf("#     node:    node connected to this router.\n");
    printf("#     AR_RTR_x_y_INQ_PRF_INCOMING_FLIT_VC[0-7]: flits on VCz\n");
    printf("#     AR_RTR_x_y_INQ_PRF_ROWBUS_STALL_CNT: stalls on x y tile\n");

    return;
}

static void darshan_log_print_apxc_rec_diff(void *file_rec1, char *file_name1,
    void *file_rec2, char *file_name2)
{
    return;
}


static void darshan_log_agg_apxc_recs(void *rec, void *agg_rec, int init_flag)
{
    int i;

    if(init_flag)
    {
        /* when initializing, just copy over the first record */
        memcpy(agg_rec, rec, sizeof(struct darshan_apxc_header_record));
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
