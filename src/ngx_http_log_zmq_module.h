/******************************************************************************
 * Copyright (c) 2014-2015 by SAPO - PT Comunicações
 * Copyright (c) 2014 by Altice Labs
 *
 *****************************************************************************/

/**
 * @file ngx_http_log_zmq_module.h
 * @author Dani Bento <dani@telecom.pt>
 * @date 1 March 2014
 * @brief Brokerlog Module Header
 *
 * @see http://www.zeromq.org/
 */

#ifndef NGX_HTTP_BROKERLOG_H

#define NGX_HTTP_BROKERLOG_H 1

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

/**
 * @brief define a type to use as an address structure
 *
 * This type is used to evaluate the url configuration
 */
typedef ngx_addr_t ngx_log_zmq_addr_t;

/**
 * @brief ZMQ protocols
 *
 */
typedef enum{
    TCP = 0,
    IPC,
    INPROC
} ngx_log_zmq_server_kind;

/**
 * @brief representation of a zmq server
 *
 * We have the address, the type of the connection and the final
 * string with the connection name (prepended with tcp://)
 */
typedef struct {
    ngx_log_zmq_addr_t       peer_addr;    /**< Address URL */
    ngx_log_zmq_server_kind  kind;         /**< Type of server (TCP|IPC|INPROC) */
    ngx_str_t               *connection;   /**< Final connection string
                                                   tcp://<ip>:<port>
                                                   ipc://<endpoint>
                                                   inproc://<endpoint> */
} ngx_log_zmq_server_t;

/**
 * @brief module's context
 *
 * Define essencial variables to maintain the context of the ZMQ conection
 * during all module phases and nginx requests
 */
typedef struct {
    ngx_log_t *log;           /**< Pointer to the logger */
    ngx_int_t iothreads;      /**< Number of threads to create */
    void *zmq_context;        /**< The ZMQ Context Initiator */
    void *zmq_socket;         /**< The ZMQ Socket to use */
    int     ccreated;         /**< Was the context created? */
    int  screated;            /**< Was the socket created? */
} ngx_http_log_zmq_ctx_t;

/**
 * @brief element log configuration
 *
 * @note nginx has a ngx flag type, we should change sset/fset/eset to that type
 */
typedef struct {
    ngx_log_zmq_server_t   *server;              /**< Configuration server */
    ngx_int_t               iothreads;           /**< Configuration number of threads */
    ngx_int_t               qlen;                /**< Configuration queue length */
    ngx_array_t            *data_lengths;        /**< Data length after format and compiling */
    ngx_array_t            *data_values;         /**< Data values */
    ngx_array_t            *endpoint_lengths;    /**< Endpoint length after format and compiling */
    ngx_array_t            *endpoint_values;     /**< Endpoint values */
    ngx_cycle_t            *cycle;               /**< Current configuration cycle */
    ngx_http_log_zmq_ctx_t *ctx;                 /**< Current module context */
    ngx_str_t              *name;                /**< Configuration name */
    ngx_log_t              *log;                 /**< Pointer to the logger */
    ngx_uint_t              sset;                /**< Was the server setted? */
    ngx_uint_t              fset;                /**< Was the format setted? */
    ngx_uint_t              eset;                /**< Was the endpoint setted? */
    ngx_uint_t              off;                 /**< Is this element deactivated? */
} ngx_http_log_zmq_element_conf_t;

/**
 * @brief location log configuration
 */
typedef struct {
    ngx_uint_t                       off;      /**< Is this element deactivated? */
    ngx_http_log_zmq_element_conf_t *element;  /**< Pointer to the log definition */
} ngx_http_log_zmq_loc_element_conf_t;

/**
 * @brief location configuration
 *
 * @note nginx as ngx flag type, we should change off to that type
 */
typedef struct {
    ngx_array_t              *logs;              /**< Array of logs to handle in this location */
    ngx_uint_t                off;               /**< Should we off all the logs in this location? */
    ngx_log_t                *log;               /**< Pointer to the logger */
    ngx_array_t				 *logs_definition;   /**< Pointer to the main conf logs definition */
} ngx_http_log_zmq_loc_conf_t;

/**
 * @brief module main configuration
 *
 * @note for now, this configuration doesn't has any use, but we should mantain it for futher requests
 */
typedef struct {
    ngx_cycle_t             *cycle;              /**< Pointer to the current nginx cycle */
    ngx_log_t               *log;                /**< Pointer to the logger */
    ngx_array_t				*logs;               /**< Array of logs definitions */
} ngx_http_log_zmq_main_conf_t;

#include "ngx_http_log_zmq.h"

#endif
