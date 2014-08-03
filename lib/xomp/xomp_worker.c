/*
 * Copyright (c) 2014 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */
#include <barrelfish/barrelfish.h>
#include <xeon_phi/xeon_phi.h>
#include <xeon_phi/xeon_phi_client.h>
#include <xomp/xomp.h>
#include <xomp/xomp_worker.h>

#include <if/xomp_defs.h>

#include "xomp_debug.h"

static struct xomp_binding *xbinding;

static volatile uint8_t is_bound = 0x0;

static struct capref msgframe;

static void *msgbuf;

static xomp_wid_t worker_id;

/*
 * ----------------------------------------------------------------------------
 * XOMP channel send handlers
 * ----------------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------------
 * XOMP channel receive handlers
 * ----------------------------------------------------------------------------
 */

static struct xomp_rx_vtbl rx_vtbl = {
    .do_work_response = NULL
};

/*
 * ----------------------------------------------------------------------------
 * XOMP channel connect handler
 * ----------------------------------------------------------------------------
 */

/**
 * \brief XOMP channel connect callback called by the Flounder backend
 *
 * \param st    Supplied worker state
 * \param err   outcome of the connect attempt
 * \param xb    XOMP Flounder binding
 */
static void master_bind_cb(void *st,
                           errval_t err,
                           struct xomp_binding *xb)
{
    is_bound = 0x1;

    XWI_DEBUG("bound to master: %s\n", err_getstring(err));

    xb->rx_vtbl = rx_vtbl;
    xbinding = xb;
}

/*
 * ============================================================================
 * Public Interface
 * ============================================================================
 */

/**
 * \brief parses the command line arguments to extract
 *
 * \param argc  argument count
 * \param argv  argument values
 * \param wid   returns the XOMP worker ID
 *
 * \returns SYS_ERR_OK iff the command line arguments were parsed succecssfully
 *          XOMP_ERR_INVALID_WORKER_ARGS if there were no XOMP worker argumetnts
 *          errval on error
 *
 */
errval_t xomp_worker_parse_cmdline(uint8_t argc,
                                   char *argv[],
                                   xomp_wid_t *wid)
{
    XWI_DEBUG("xomp_worker_parse_cmdline\n");

    xomp_wid_t retwid = 0;
    uint8_t parsed = 0;
    for (uint32_t i = 1; argv[i] != NULL; ++i) {
        if (strcmp("-xompworker", argv[i]) == 0) {
            parsed++;
        } else if (strncmp("-wid=", argv[i], 5) == 0) {
            retwid = strtol(argv[i]+5, NULL, 16);
            parsed++;
        }
    }

    if (parsed != 2) {
        return XOMP_ERR_INVALID_WORKER_ARGS;
    }

    if (wid) {
        *wid = retwid;
    }

    return SYS_ERR_OK;
}

/**
 * \brief initializes the XOMP worker library
 *
 * \param wid   Xomp worker id
 *
 * \returns SYS_ERR_OK on success
 *          errval on failure
 */
errval_t xomp_worker_init(xomp_wid_t wid)
{
    errval_t err;

    worker_id = wid;

    XWI_DEBUG("initializing worker {%016lx}\n", worker_id);

    struct capref frame = {
        .cnode = cnode_root,
        .slot = ROOTCN_SLOT_ARGCN
    };

    struct frame_identity id;
    err = invoke_frame_identify(frame, &id);
    if (err_is_fail(err)) {
        return err_push(err, XOMP_ERR_INVALID_MSG_FRAME);
        return err;
    }

    if ((1UL << id.bits) < XOMP_MSG_FRAME_SIZE) {
        return XOMP_ERR_INVALID_MSG_FRAME;
    }

    msgframe = frame;

    err = vspace_map_one_frame(&msgbuf, XOMP_MSG_FRAME_SIZE, frame, NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    XWI_DEBUG("messaging frame mapped: [%016lx] @ [%016lx]\n", id.base,
              (lvaddr_t )msgbuf);

#ifdef __k1om__
    err = xeon_phi_client_init(disp_xeon_phi_id());
    if (err_is_fail(err)) {
        return err;
    }
#else
    USER_PANIC("workers should be run on the xeon phi\n");
#endif

    struct xomp_frameinfo fi = {
        .sendbase = id.base,
        .inbuf = ((uint8_t *) msgbuf) + XOMP_MSG_CHAN_SIZE,
        .inbufsize = XOMP_MSG_CHAN_SIZE,
        .outbuf = ((uint8_t *) msgbuf),
        .outbufsize = XOMP_MSG_CHAN_SIZE
    };

    struct waitset *ws = get_default_waitset();
    err = xomp_connect(&fi, master_bind_cb, NULL, ws, IDC_EXPORT_FLAGS_DEFAULT);
    if (err_is_fail(err)) {
        /* TODO: Clean up */
        return err;
    }

    XWI_DEBUG("Waiting until bound to master...\n");

    while (!is_bound) {
        messages_wait_and_handle_next();
    }

    if (xbinding == NULL) {
        /* TODO: error code */
        return -1;
    }

    return SYS_ERR_OK;

}
