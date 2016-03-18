/******************************************************************************
 * Copyright (c) 2014-2015 by SAPO - PT Comunicações
 * Copyright (c) 2016 by Altice Labs
 *
 *****************************************************************************/

/**
 * @file ngx_http_log_zmq.h
 * @author Dani Bento <dani@telecom.pt>
 * @date 1 March 2014
 * @brief Brokerlog ZMQ Header
 *
 * @see http://www.zeromq.org/
 */

#ifndef NGX_HTTP_BROKERLOG_ZMQ_H

#define NGX_HTTP_BROKERLOG_ZMQ_H 1

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

#include <zmq.h>

#include "ngx_http_log_zmq_module.h"

#ifndef ZMQ_DONTWAIT

#define ZMQ_DONTWAIT ZMQ_NOBLOCK

#endif

#if ZMQ_VERSION_MAJOR < 3

#define zmq_msg_send(msg,sock,opt) zmq_send(sock, msg, opt)
#define zmq_msg_recv(msg,sock,opt) zmq_recv(sock, msg, opt)
#define zmq_ctx_destroy(context)   zmq_term(context)
#define ZMQ_POLL_MSEC	1000
#define ZMQ_SNDHWM		ZMQ_HWM
#define	ZMQ_RCVHWM		ZMQ_HWM
#define zmq_hwm_t uint64_t

#else

#define zmq_hwm_t int
#define ZMQ_POLL_MSEC	1

#endif

#define ZMQ_NGINX_LINGER 0
#define ZMQ_NGINX_QUEUE_LENGTH 100

/* ZMQ makes use of three types of protocols:
 *
 * _TCP_ is used mainly to publish data to another service,
 * on other host.
 *
 * _IPC_ is used to maintain communication between processes
 * inside the same machine
 *
 * _INPROC_ is used to maintain communication inside the same
 * application or process
 */
#define ZMQ_TCP_KEY "tcp"
#define ZMQ_TCP_HANDLER "tcp://"
#define ZMQ_TCP_HLEN 6

#define ZMQ_IPC_KEY "ipc"
#define ZMQ_IPC_HANDLER "ipc://"
#define ZMQ_IPC_HLEN 6

#define ZMQ_INPROC_KEY "inproc"
#define ZMQ_INPROC_HANDLER "inproc://"
#define ZMQ_INPROC_HLEN 9

int zmq_init_ctx(ngx_http_log_zmq_ctx_t *ctx);
void zmq_term_ctx(ngx_http_log_zmq_ctx_t *ctx);
int zmq_create_ctx(ngx_http_log_zmq_element_conf_t *cf);
int zmq_create_socket(ngx_pool_t *pool, ngx_http_log_zmq_element_conf_t *cf);
ngx_int_t log_zmq_serialize(ngx_pool_t *pool, ngx_str_t *endpoint, ngx_str_t *payload, ngx_str_t *output);

#endif
