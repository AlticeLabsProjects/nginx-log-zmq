/******************************************************************************
 * Copyright (c) 2014-2015 by SAPO - PT Comunicações
 * Copyright (c) 2016 by Altice Labs
 *
 *****************************************************************************/

/**
 * @file ngx_http_log_zmq.c
 * @author Dani Bento <dani@telecom.pt>
 * @date 1 March 2014
 * @brief Brokerlog ZMQ
 *
 * @see http://www.zeromq.org/
 */

#include <ngx_times.h>
#include <ngx_core.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <zmq.h>

#include "ngx_http_log_zmq.h"

/**
 * @brief initialize ZMQ context
 *
 * Each location is owner of a ZMQ context. We should see this effected
 * reflected on the number of threads created by each nginx process.
 *
 * @param ctx A ngx_http_log_zmq_ctx_t pointer representing the actual
 *              location context
 * @return An int representing the OK (0) or error status
 * @note We should redefine this to ngx_int_t with NGX_OK | NGX_ERROR
 */
int
zmq_init_ctx(ngx_http_log_zmq_ctx_t *ctx)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0, "ZMQ: zmq_init_ctx()");
    /* each location has it's own context, we need to verify if this is the best
     * solution. We don't want to consume a lot of ZMQ threads to maintain the
     * communication */
    ctx->zmq_context = zmq_init((int) ctx->iothreads);
    if (NULL == ctx->zmq_context) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, ctx->log, 0, "ZMQ: zmq_init(%d) fail", ctx->iothreads);
        return -1;
    }
    ctx->ccreated = 1;
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, ctx->log, 0, "ZMQ: zmq_init(%d) success", ctx->iothreads);
    return 0;
}

/**
 * @brief create ZMQ Context
 *
 * Read the actual configuration, verify if we dont have yet a context and
 * initiate it.
 *
 * @param cf A ngx_http_log_zmq_element_conf_t pointer to the location configuration
 * @return An int representing the OK (0) or error status
 * @note We should redefine this to ngx_int_t with NGX_OK | NGX_ERROR
 */
int
zmq_create_ctx(ngx_http_log_zmq_element_conf_t *cf)
{
    int  rc = 0;

    /* TODO: should we create the context structure here? */
    if (NULL == cf || NULL == cf->ctx) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_ctx() no configuration");
        return 1;
    }
    /* context is already created, return NGX_OK */
    if (1 == cf->ctx->ccreated) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_ctx() already created");
        return 0;
    }

    /* create location context */
    cf->ctx->iothreads = cf->iothreads;
    rc = zmq_init_ctx(cf->ctx);

    if (rc != 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_ctx() error");
        ngx_log_error(NGX_LOG_ERR, cf->ctx->log, 0, "ZMQ: zmq_create_ctx() error");
        return rc;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_ctx() success");
    return 0;
}

/**
 * @brief close ZMQ sockets and term ZMQ context
 *
 * We should close all sockets and term the ZMQ context before we totaly exit
 * nginx.
 *
 * @param ctx A ngx_http_log_zmq_ctx_t pointer to the actual module context
 * @return Nothing
 * @note Should we free the context itself here?
 */
void
zmq_term_ctx(ngx_http_log_zmq_ctx_t *ctx)
{
    /* close and nullify context zmq_socket */
    if (ctx->zmq_socket) {
        zmq_close(ctx->zmq_socket);
        ctx->zmq_socket = NULL;
    }

    /* term and nullify context zmq_context */
    if (ctx->zmq_context) {
        zmq_ctx_destroy(ctx->zmq_context);
        ctx->zmq_context = NULL;
    }

    /* nullify log */
    if (ctx->log) {
        ctx->log = NULL;
    }
    return;
}

/**
 * @brief create a ZMQ Socket
 *
 * Verify if it not exists and create a new socket to be available to write
 * messages.
 *
 * @param cf A ngx_http_log_zmq_element_conf_t pointer to the location configuration
 * @return An int representing the OK (0) or error status
 * @note We should redefine this to ngx_int_t with NGX_OK | NGX_ERROR
 * @warning It's important to look at here and define one socket per worker
 */
int
zmq_create_socket(ngx_pool_t *pool, ngx_http_log_zmq_element_conf_t *cf)
{
    int linger = ZMQ_NGINX_LINGER, rc = 0;
    zmq_hwm_t qlen = cf->qlen < 0 ? ZMQ_NGINX_QUEUE_LENGTH : cf->qlen;
    char *connection;

    /* verify if we have a context created */
    if (NULL == cf->ctx->zmq_context) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() context is NULL");
        return -1;
    }

    /* verify if we have already a socket associated */
    if (0 == cf->ctx->screated) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() create socket");
        cf->ctx->zmq_socket = zmq_socket(cf->ctx->zmq_context, ZMQ_PUB);
        /* verify if it was created */
        if (NULL == cf->ctx->zmq_socket) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() socket not created");
            ngx_log_error(NGX_LOG_ERR, cf->ctx->log, 0, "ZMQ socket not created: %s", strerror(errno));
            return -1;
        }
        cf->ctx->screated = 1;
    }

    /* set socket option ZMQ_SNDHWM (Must be done before ZMQ_LINGER or it fails, why?) */
    rc = zmq_setsockopt(cf->ctx->zmq_socket, ZMQ_SNDHWM, &qlen, sizeof(qlen));
    if (rc != 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() error setting ZMQ_SNDHWM");
        ngx_log_error(NGX_LOG_ERR, cf->ctx->log, 0, "ZMQ error setting option ZMQ_SNDHWM: %s", strerror(errno));
        return -1;
    }

    /* set socket option ZMQ_LINGER */
    rc = zmq_setsockopt(cf->ctx->zmq_socket, ZMQ_LINGER, &linger, sizeof(linger));
    if (rc != 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() error setting ZMQ_LINGER");
        ngx_log_error(NGX_LOG_ERR, cf->ctx->log, 0, "ZMQ error setting option ZMQ_LINGER: %s", strerror(errno));
        return -1;
    }

    /* create a simple char * to the connection name */
    connection = ngx_pcalloc(pool, cf->server->connection->len + 1);
    ngx_memcpy(connection, cf->server->connection->data, cf->server->connection->len);

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() connect to %s", connection);

    /* open zmq connection to */
    rc = zmq_connect(cf->ctx->zmq_socket, connection);
    if (rc != 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() error connecting");
        ngx_log_error(NGX_LOG_ERR, cf->ctx->log, 0, "ZMQ error connecting: %s", strerror(errno));
        ngx_pfree(pool, connection);
        return -1;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->ctx->log, 0, "ZMQ: zmq_create_socket() connected");
    ngx_pfree(pool, connection);

    /* if all was OK, we should return 0 */
    return rc;
}

/**
 * @brief serialize a ZMQ message
 *
 * Process all input data and the endpoint and build a message to be sent by ZMQ
 *
 * @param pool A ngx_pool_t pointer to the nginx memory manager
 * @param endpoint A ngx_str_t pointer to the current endpoint configured to use
 * @param data A ngx_str_t pointer with the message compiled to be sent
 * @param output A ngx_str_t pointer which will point to the final message to be sent
 * @return An ngx_int_t with NGX_OK | NGX_ERROR
 */
ngx_int_t
log_zmq_serialize(ngx_pool_t *pool, ngx_str_t *endpoint, ngx_str_t *data, ngx_str_t *output) {
    /* the final message sent to zmq is composed by endpoint+data
     * eg: endpoint = /stratus/, data = {'num':1}
     * final message /stratus/{'num':1}
     */
    output->len = endpoint->len + data->len;
    output->data = ngx_palloc(pool, output->len);

    if (NULL == output->data) {
        output->len = 0;
        return NGX_ERROR;
    }

    ngx_memcpy(output->data, (const char *) endpoint->data, endpoint->len);
    ngx_memcpy(output->data + endpoint->len, (const char *) data->data, data->len);

    return NGX_OK;
}
