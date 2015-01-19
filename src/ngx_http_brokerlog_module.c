/******************************************************************************
 * Copyright (c) 2014 by SAPO - PT Comunicações
 *
 *****************************************************************************/

/**
 * @file ngx_http_brokerlog_module.c
 * @author Dani Bento <dani@telecom.pt>
 * @date 1 March 2014
 * @brief Brokerlog Module for nginx using ZMQ Message
 *
 * @see http://www.zeromq.org/
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>
#include <stdio.h>

#include "ngx_http_brokerlog_module.h"

/**
 * @brief get default port for the input type of protocol
 *
 * This function is responsable to handle the request and log
 * the data we want to the brokerlog via zmq proccess.
 * It's important to note that if this function fails it should
 * not kill the nginx normal running. After all, this is the log
 * phase.
 *
 * @param kind A ngx_brokerlog_server_kind with the value TCP|IPC|INPROC
 * @return A in_port_t with an integer with the port
 * @note Should we move this to ngx_http_brokerlog_zmq.c ?
 */

static in_port_t __get_default_port(const ngx_brokerlog_server_kind kind){
    static const in_port_t DEFAULT_PORT = 0;

    switch(kind){
        case TCP:
            return 5555;
        case IPC:
        case INPROC:
            return 0;
    }

    return DEFAULT_PORT;
}

static void *ngx_http_brokerlog_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_brokerlog_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_brokerlog_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_brokerlog_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static char *ngx_http_brokerlog_set_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_brokerlog_set_format(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_brokerlog_set_endpoint(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_brokerlog_set_off(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_brokerlog_postconf(ngx_conf_t *cf);
static void ngx_http_brokerlog_exitmaster(ngx_cycle_t *cycle);

static ngx_command_t  ngx_http_brokerlog_commands[] = {

    { ngx_string("brokerlog_server"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE5,
      ngx_http_brokerlog_set_server,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("brokerlog_format"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_2MORE,
      ngx_http_brokerlog_set_format,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("brokerlog_endpoint"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_brokerlog_set_endpoint,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    { ngx_string("brokerlog_off"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_brokerlog_set_off,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    ngx_null_command
};

static ngx_http_module_t  ngx_http_brokerlog_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_brokerlog_postconf,           /* postconfiguration */
    ngx_http_brokerlog_create_main_conf,   /* create main configuration */
    ngx_http_brokerlog_init_main_conf,     /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_brokerlog_create_loc_conf,    /* create location configuration */
    ngx_http_brokerlog_merge_loc_conf      /* merge location configuration */
};

extern ngx_module_t  ngx_http_log_module;

ngx_module_t  ngx_http_brokerlog_module = {
    NGX_MODULE_V1,
    &ngx_http_brokerlog_module_ctx,        /* module context */
    ngx_http_brokerlog_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                   /* exit process */
    ngx_http_brokerlog_exitmaster,         /* exit master */
    NGX_MODULE_V1_PADDING
};

/**
 * @brief nginx module's handler for logger phase
 *
 * This function is responsable to handle the request and log
 * the data we want to the brokerlog via zmq proccess.
 * It's important to note that if this function fails it should
 * not kill the nginx normal running. After all, this is the log
 * phase.
 *
 * @param r A ngx_http_request_t that represents the current request
 * @return A ngx_int_t which can be NGX_ERROR | NGX_OK
 * @note If NGX_DEBUG is setted than we print some messages to the debug log
 */
ngx_int_t
ngx_http_brokerlog_handler(ngx_http_request_t *r)
{
    ngx_http_brokerlog_loc_conf_t  *lccf;
    ngx_http_brokerlog_element_conf_t *lecf, *clecf;
    ngx_uint_t                  i;
    ngx_str_t                   data;
    ngx_str_t                   broker_data;
    ngx_str_t                   endpoint;
    ngx_pool_t                 *pool = r->connection->pool;
    ngx_int_t (*serializer)(ngx_pool_t*, ngx_str_t*, ngx_str_t*, ngx_str_t*) = NULL;
    ngx_log_t                  *log = r->connection->log;
    zmq_msg_t query;
    int rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler()");

    /* get current location configuration */
    lccf = ngx_http_get_module_loc_conf(r, ngx_http_brokerlog_module);

    /* simply return NGX_OK if location logs are off */
    if (lccf->off == 1) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() all logs off");
        return NGX_OK;
    }

    /* location configuration has an ngx_array of log elements, we should iterate
     * by each one
   */
    lecf = lccf->logs->elts; /* point to the initial position > element 0 */

    /* we use "continue" for each error in the cycle because we do not want the stop
     * the iteration, but continue to the next log */
    for (i = 0; i < lccf->logs->nelts; i++) {

        clecf = &(lecf[i]); /* get the i element of the log array */

        /* worst case? we get a null element ?! */
        if (NULL == clecf) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() no element config");
            continue;
        }

        /* we only proceed if all the variables were setted: endpoint, server, format */
        if (clecf->eset == 0 || clecf->fset == 0 || clecf->sset == 0) {
            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() eset=%d, fset=%d, sset=%d",
                                                       clecf->eset, clecf->fset, clecf->sset);
            continue;
        }

        /* our configuration doesn't has a name? some error ocorred */
        if (NULL == clecf->name) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() no element name");
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() no element name");
            continue;
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() setting up %V", clecf->name);
        }

        /* pass to the next log if this log is set to off */
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() log %V off=%d", clecf->name, clecf->off);
        if (clecf->off == 1) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() log %V disabled", clecf->name);
            continue;
        }


        /* we set the server variable... but we can use it? */
        if(NULL == clecf->server){
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() no server to log");
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() no server to log");
            continue;
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() server connection %V", clecf->server->connection);
        }

        /* we set the data format... but we don't have any content to sent? */
        if(NULL == clecf->data_lengths){
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() no format to log");
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() no format to log");
            continue;
        }

        /* we set the endpoint... but we don't have any valid endpoint? */
        if(NULL == clecf->endpoint_lengths){
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() no endpoint to log");
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() no endpoint to log");
            continue;
        }

        /* process all data variables and write them back to the data values */
        if (NULL == ngx_http_script_run(r, &data, clecf->data_lengths->elts, 0, clecf->data_values->elts)) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() error script data");
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() error script data");
            continue;
        }

        /* process all endpoint variables and write them back the the endpoint values */
        if (NULL == ngx_http_script_run(r, &endpoint, clecf->endpoint_lengths->elts, 0, clecf->endpoint_values->elts)) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() error script endpoint");
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() error script endpoint");
            continue;
        }

        /* yes, we must go on */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() logging to server");

        /* no data */
        if( 0 == data.len ){
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() no data to send");
            continue;
        }

        /* serialize to the final message format */
        serializer = &brokerlog_serialize_zmq;

        if( NGX_ERROR == (*serializer)(pool, &endpoint, &data, &broker_data) ){
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() error serializing notification message");
            ngx_pfree(pool, broker_data.data);
            continue;
        }

        /* no context? we dont create any */
        if (NULL == clecf->ctx) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "brokerlog_zmq: handler() no context");
            continue;
        }

        clecf->ctx->log = log;

        rc = 1; /* we should have a rc = 0 after this call */

        /* create zmq context if needed */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() verify ZMQ context");
        if ((NULL == clecf->ctx->zmq_context) && (0 == clecf->ctx->ccreated)) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() creating context");
            rc = zmq_create_ctx(clecf);
            if (rc != 0) {
                ngx_log_error(NGX_LOG_INFO, log, 0, "brokerlog_zmq: handler() error creating context");
                continue;
            }
        }

        /* open zmq socket if needed */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() verify ZMQ socket");
        if (NULL == clecf->ctx->zmq_socket && 0 == clecf->ctx->screated) {
            ngx_log_debug0(NGX_LOG_INFO, log, 0, "brokerlog_zmq: handler() creating socket");
            rc = zmq_create_socket(pool, clecf);
            if (rc != 0) {
                ngx_log_error(NGX_LOG_INFO, log, 0, "brokerlog_zmq: handler() error creating socket");
                continue;
            }
        }

        /* initialize zmq message */
        zmq_msg_init_size(&query, broker_data.len);

        ngx_memcpy(zmq_msg_data(&query), broker_data.data, broker_data.len);

        if (zmq_send(clecf->ctx->zmq_socket, &query, 0) == 0) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() message sent: %V", &broker_data);
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: handler() message NOT sent: %V", &broker_data);
        }

        /* free all for the next iteration */
        zmq_msg_close(&query);

        ngx_pfree(pool, broker_data.data);
    }

    return NGX_OK;
}

/**
 * @brief nginx module's proccess to create main configuration
 *
 * @param cf A ngx_conf_t pointer to the main configuration cycle
 * @return A pointer with the module configuration that has been created
 * @note We use the configuration memory pool to create it
 */

static void *
ngx_http_brokerlog_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_brokerlog_main_conf_t *bkmc;

    ngx_log_error(NGX_LOG_INFO, cf->cycle->log, 0, "brokerlog_zmq: create_main_conf()");

    bkmc = ngx_pcalloc(cf->pool, sizeof(ngx_http_brokerlog_main_conf_t));
    if (bkmc == NULL) {
        return NULL;
    }

    bkmc->cycle = cf->cycle;
    bkmc->log = cf->log;

    return bkmc;
}

/**
 * @brief nginx module's proccess to init main configuration
 *
 * @param cf A ngx_conf_t pointer to the main configuration cycle
 * @param conf A pointer to the module's main configuration
 * @return NGX_CONF_OK
 */

static char *
ngx_http_brokerlog_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_brokerlog_main_conf_t *bkmc = conf;

    ngx_log_error(NGX_LOG_INFO, cf->cycle->log, 0, "brokerlog_zmq: init_main_conf()");

    bkmc->cycle = cf->cycle;
    bkmc->log = cf->log;

    return NGX_CONF_OK;
}

/**
 * @brief nginx module's proccess to create location configuration
 *
 * After the nginx start it is necessary to create all location configuration.
 * For each location, we can have different configs.
 *
 * @param cf A ngx_conf_t pointer to the main nginx configuration
 * @return A pointer with the module configuration that has been created
 * @note We use the configuration memory pool to create it
 */
static void *
ngx_http_brokerlog_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_brokerlog_loc_conf_t  *conf;

    ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: create_loc_conf()");

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_brokerlog_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->off = NGX_CONF_UNSET;
    conf->logs = NGX_CONF_UNSET_PTR;
    conf->log = cf->log;

    return conf;
}

/**
 * @brief nginx module's proccess to merge all location configuration
 *
 * To avoid a lot of memory allocation and to be possible to have similar
 * configurations, this proccess merge all data with a logical tree.
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param parent A pointer to the parent configuration relatively to this
 * @param child A pointer to the child configuration relatively to this
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 * @warning It's important to ensure that all variables are passed
 *          between parents and childs
 */
static char *
ngx_http_brokerlog_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_brokerlog_loc_conf_t *prev = parent;
    ngx_http_brokerlog_loc_conf_t *conf = child;
    ngx_http_brokerlog_element_conf_t *eleprev;
    ngx_http_brokerlog_element_conf_t *eleconf;
    ngx_http_brokerlog_element_conf_t *element;
    ngx_uint_t                       i, j, found;

    ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf()");

    if (conf->off == NGX_CONF_UNSET) {
        conf->off = prev->off;
    }

    if (NULL == conf->log) {
        conf->log = prev->log;
    }
    if (NGX_CONF_UNSET_PTR == conf->logs || NULL == conf->logs) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() merge logs");
        conf->logs = prev->logs;
#if (NGX_DEBUG)
        for (i = 0; i < conf->logs->nelts; i++) {
            element = conf->logs->elts + i;
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf(): UNSET %V", element->name);
        }
#endif
    }
    if (0 == conf->logs->nelts) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() destroy conf logs");
        ngx_array_destroy(conf->logs);
        conf->logs = prev->logs;
    } else {
        eleconf = conf->logs->elts;
        eleprev = prev->logs->elts;
#if (NGX_DEBUG)
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf(): CHILD Elem %d", conf->logs->nelts);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf(): PREV  Elem %d", prev->logs->nelts);
        for (i = 0; i < conf->logs->nelts; i++) {
            element = eleconf + i;
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf(): MERGE CHILD %V", element->name);
        }
        for (i = 0; i < prev->logs->nelts; i++) {
            element = eleprev + i;
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf(): MERGE PREV %V", element->name);
        }
#endif
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() search to merge");
        for (j = 0; j < prev->logs->nelts; j++) {
            found = 0;
            ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() search %V on %d elements",
                    eleprev[j].name, conf->logs->nelts);
            for (i = 0; i < conf->logs->nelts; i++) {
                ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() search %V match", eleconf[i].name);
                if (eleprev[j].name && eleconf[i].name && eleprev[j].name->len == eleconf[i].name->len
                        && ngx_strncmp(eleprev[j].name->data, eleconf[i].name->data, eleprev[j].name->len) == 0)
                {
                   found = 1;
                   ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() %V found", eleprev[j].name);
                   if (eleconf[i].server == NGX_CONF_UNSET_PTR) {
                       ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() set server");
                       eleconf[i].server = eleprev[j].server;
                   }
                   if (eleconf[i].fset == NGX_CONF_UNSET &&
                       eleprev[i].fset == 1)
                   {
                       ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() set data len");
                       eleconf[i].data_lengths = eleprev[j].data_lengths;
                       ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() set data value");
                       eleconf[i].data_values = eleprev[j].data_values;
                   }
                   if (eleconf[i].eset == NGX_CONF_UNSET &&
                       eleprev[i].eset == 1) {
                       ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() set endpoint len");
                       eleconf[i].endpoint_lengths = eleprev[j].endpoint_lengths;
                       ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() set endpoint value");
                       eleconf[i].endpoint_values = eleprev[j].endpoint_values;
                   }
                   if (eleconf[i].ctx == NGX_CONF_UNSET_PTR) {
                       ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() set context");
                       eleconf[i].ctx = eleprev[j].ctx;
                   }
                   eleconf[i].sset = eleprev[j].sset;
                   eleconf[i].fset = eleprev[j].fset;
                   eleconf[i].eset = eleprev[j].eset;
                   if (eleprev[i].off != NGX_CONF_UNSET_PTR) {
                       eleconf[i].off = eleprev[j].off;
                   }
                   break;
                }
            }
            if (found == 0) {
                ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() %V not found", eleprev[j].name);
                element = ngx_array_push(conf->logs);
                if (NULL == element) {
                    ngx_log_error(NGX_LOG_INFO, cf->log, 0, "brokerlog_zmq: merge_loc_conf() element null %V", eleprev[j].name);
                }
                element = eleprev + j;
                element->off = eleprev[j].off;
            }
        }
    }
#if (NGX_DEBUG)
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "brokerlog_zmq: merge_loc_conf() end");
#endif

    return NGX_CONF_OK;
}

/**
 * @brief nginx module's set server
 *
 * We set the server configuration here. We should introduce a valid url, to the
 * connection (it will be prepended with tcp://), the type of connection (zmq).
 * After this, we have two numbers, the first is the number of threads created by
 * the ZMQ context and the last one is the queue limit for this setting.
 *
 * @code{.conf}
 * brokerlog_server 127.0.0.1:5555 zmq 10 10000;
 * @endcode
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param cmd A pointer to ngx_commant_t that defines the configuration line
 * @param conf A pointer to the configuration received
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 * @warning It's important to rearrange this to permit MORE2 arguments and not TAKE4
 */
static char *
ngx_http_brokerlog_set_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_brokerlog_loc_conf_t     *llcf = conf;
    ngx_http_brokerlog_element_conf_t *lecf;
    ngx_str_t                         *value;
    const unsigned char               *kind;
    ngx_int_t                         iothreads;
    ngx_int_t                         qlen;
    ngx_url_t                         u;
    ngx_brokerlog_server_t            *endpoint;
    char                              *connection;
    size_t                            connlen;
    size_t                            zmq_hdlen;
    ngx_uint_t                        i;
#if (NGX_DEBUG)
    ngx_log_t                         *log = cf->log;
#endif

    if (cf->cmd_type != NGX_HTTP_MAIN_CONF) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "the \"brokerlog_server\" directive may be only used "
                "only on \"http\" level");
        return NGX_CONF_ERROR;
    }

    /* value[0] variable name
     * value[1] definition name
     * value[2] server/target
     * value[3] protocol type
     * value[4] number of threads
     * value[5] queue len
   */
    value = cf->args->elts;

    /* if location configuration has logs, iterate them and search for the input definition,
     * if found, do not do anything, if not, create a new log entry */
    if (NULL != llcf && llcf->logs != NGX_CONF_UNSET_PTR) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() searching... %V", &value[1]);
        lecf = llcf->logs->elts;
        for (i = 0; i < llcf->logs->nelts; i++) {
            if (lecf[i].name->len == value[1].len
                    && ngx_strncmp(lecf[i].name->data, value[1].data, lecf[i].name->len) == 0) {
               ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_server: target repeated");
               return NGX_CONF_ERROR;
            }
        }
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() not found");
        lecf = ngx_array_push(llcf->logs);
    } else {
        /* location has no logs, create a new array structure to use */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() creating logs");
        llcf->logs = ngx_array_create(cf->pool, 1, sizeof(ngx_http_brokerlog_element_conf_t));
        if (llcf->logs == NULL) {
            return NGX_CONF_ERROR;
        }
        llcf->logs->nelts = 1;
        lecf = (ngx_http_brokerlog_element_conf_t *) llcf->logs->elts;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() lecf");

    /* definition not found or first log, create basic data */
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() name not found");
    if (lecf == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() lecf == NULL");
        return NGX_CONF_ERROR;
    }

    /* create ZMQ context structure */
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() create lecf ctx");

    lecf->ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_brokerlog_ctx_t));
    if (NULL == lecf->ctx) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() lecf->ctx == NULL");
        return NGX_CONF_ERROR;
    }

    lecf->ctx->log = cf->cycle->log;

    /* update definition name and cycle log*/
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() create lecf name");

    lecf->name = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    if (lecf->name == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() lecf->name == NULL");
        return NGX_CONF_ERROR;
    }
    lecf->name->data = ngx_palloc(cf->pool, value[1].len);
    lecf->name->len = value[1].len;
    ngx_memcpy(lecf->name->data, value[1].data, value[1].len);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() initialize element conf %V", &value[1]);
    lecf->log = cf->cycle->log;
    lecf->off = 0;

    /* set the type of protocol TCP|IPC|INPROC */
    kind = value[3].data;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() kind %V", &value[3]);

    endpoint = ngx_pcalloc(cf->pool, sizeof(ngx_brokerlog_server_t));

    if (endpoint == NULL) {
        return NGX_CONF_ERROR;
    }

    if (0 == ngx_strcmp(kind, ZMQ_TCP_KEY)) {
        endpoint->kind = TCP;
    } else if (0 == ngx_strcmp(kind, ZMQ_IPC_KEY)) {
        endpoint->kind = IPC;
    } else if (0 == ngx_strcmp(kind, ZMQ_INPROC_KEY)) {
        endpoint->kind = INPROC;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() invalid ZMQ connection type: %s", kind);
        return NGX_CONF_ERROR;
    }

    /* set the number of threads associated with this context */
    iothreads = ngx_atoi(value[4].data, value[4].len);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() iothreads %V", &value[4]);

    if (iothreads == NGX_ERROR || iothreads <= 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() invalid I/O threads %d", iothreads);
        return NGX_CONF_ERROR;
    }

    lecf->iothreads = iothreads;

    /* set the queue size associated with this context */
    qlen = ngx_atoi(value[5].data, value[5].len);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() queue len %V", &value[5]);

    if (qlen == NGX_ERROR || qlen < 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() invalid queue size %d", qlen);
    }

    lecf->qlen = qlen;

    /* if the protocol used is TCP, parse it and use nginx parse_url to validate the input */
    if (endpoint->kind == TCP) {
        u.url = value[2];
        u.default_port = __get_default_port(endpoint->kind);
        u.no_resolve = 0;
        u.listen = 1;

        if(ngx_parse_url(cf->pool, &u) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() invalid server: %s", u.err);
            return NGX_CONF_ERROR;
        }
        endpoint->peer_addr = u.addrs[0];
    } else {
        u.url = value[2];
    }

    /* create a connection based on the protocol type */
    switch (endpoint->kind) {
        case TCP:
            zmq_hdlen = ZMQ_TCP_HLEN;
            connlen = u.url.len + zmq_hdlen;
            connection = (char *) ngx_pcalloc(cf->pool, connlen + 1);
            ngx_memcpy(connection, ZMQ_TCP_HANDLER, zmq_hdlen);
            ngx_memcpy(&connection[zmq_hdlen], u.url.data, u.url.len);
            break;
        case IPC:
            zmq_hdlen = ZMQ_IPC_HLEN;
            connlen = u.url.len + zmq_hdlen;
            connection = (char *) ngx_pcalloc(cf->pool, connlen + 1);
            ngx_memcpy(connection, ZMQ_IPC_HANDLER, zmq_hdlen);
            ngx_memcpy(&connection[zmq_hdlen], u.url.data, u.url.len);
            break;
        case INPROC:
            zmq_hdlen = ZMQ_INPROC_HLEN;
            connlen = u.url.len + zmq_hdlen;
            connection = (char *) ngx_pcalloc(cf->pool, connlen + 1);
            ngx_memcpy(connection, ZMQ_INPROC_HANDLER, zmq_hdlen);
            ngx_memcpy(&connection[zmq_hdlen], u.url.data, u.url.len);
            break;
        default:
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_server() invalid endpoint type");
            return NGX_CONF_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() connection %s", connection);

    if (NULL == connection) {
        return NGX_CONF_ERROR;
    }

    /* create the final connection endpoint to be used on socket connection */
    endpoint->connection = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    endpoint->connection->data = ngx_palloc(cf->pool, connlen);
    endpoint->connection->len = connlen;
    ngx_memcpy(endpoint->connection->data, connection, connlen);
    lecf->server = endpoint;

    /* set the server as done */
    lecf->sset = 1;

    ngx_pfree(cf->pool, connection);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_server() end");

    return NGX_CONF_OK;
}

/**
 * @brief nginx module's set format
 *
 * This function evaluate the format string introducted in the configuration file.
 * The string is allocated and parsed to evaluate if it has any variables to
 * expand. This is the message sent to the brokerlog.
 *
 * @code{.conf}
 * brokerlog_format definition "put your stuff in here like vars $status"
 * @endcode
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param cmd A pointer to ngx_commant_t that defines the configuration line
 * @param conf A pointer to the configuration received
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 */
static char *
ngx_http_brokerlog_set_format(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_brokerlog_loc_conf_t     *llcf = conf;
    ngx_http_brokerlog_element_conf_t *lecf, *clecf;
    ngx_str_t                         *log_format, *value;
    ngx_http_script_compile_t         sc;
    size_t                            i, len, log_len;
    u_char                            *p;
    ngx_uint_t                        found = 0;
#if (NGX_DEBUG)
    ngx_log_t                         *log = cf->log;
#endif

    len = 0;
    log_len = 0;

    /* value[0] variable name
     * value[1] definition name
     * value[2] format
   */
    value = cf->args->elts;

    /* if this location has logs, look for the input definition and if found update his data,
     * if not, create a new entry */
    if (NULL != llcf && llcf->logs != NGX_CONF_UNSET_PTR) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() searching... %V", &value[1]);
        for (i = 0; i < llcf->logs->nelts; i++) {
            clecf = ((ngx_http_brokerlog_element_conf_t *) llcf->logs->elts) + i;
            if ((clecf->name != NULL) && (clecf->name->len == value[1].len)
                    && (ngx_strncmp(clecf->name->data, value[1].data, clecf->name->len) == 0)) {
               found = 1;
               lecf = clecf;
               ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() found %V", &value[1]);
               /* we cannot have a lecf == NULL here! */
               break;
            }
        }
        if (found == 0) {
            lecf = ngx_array_push(llcf->logs);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() not found %V", &value[1]);
            if (lecf == NULL) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "error creating location config");
                return NGX_CONF_ERROR;
            }
        }
    } else {
        /* this location has no logs defined, create a new array structure for it */
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() create first log %V", &value[1]);
        llcf->logs = ngx_array_create(cf->pool, 1, sizeof(ngx_http_brokerlog_element_conf_t));
        if (llcf->logs == NULL) {
            return NGX_CONF_ERROR;
        }
        llcf->logs->nelts = 1;
        lecf = (ngx_http_brokerlog_element_conf_t *) llcf->logs->elts;
        if (lecf == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "error creating location configs");
            return NGX_CONF_ERROR;
        }
    }

    /* in both cases, where we cannot find the definition or when we doesn't have any log, set the basic
     * information, name and cycle log
     *
     * if the definition exists, destroy old data arrays which will be replaced with the new info
     */
    if (found == 0) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() create lecf %V", &value[1]);
        lecf->log = cf->cycle->log;
        lecf->off = 0;
        lecf->name = ngx_palloc(cf->pool, sizeof(ngx_str_t));
        if (lecf->name == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_format() lecf->name == NULL");
            return NGX_CONF_ERROR;
        }
        lecf->name->data = ngx_palloc(cf->pool, value[1].len);
        lecf->name->len = value[1].len;
        ngx_memcpy(lecf->name->data, value[1].data, value[1].len);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() init element conf %V", &value[1]);
        lecf->server = NGX_CONF_UNSET_PTR;
        lecf->ctx = NGX_CONF_UNSET_PTR;
    } else {
        if (lecf->data_lengths != NULL && lecf->data_lengths != NGX_CONF_UNSET_PTR) {
            ngx_array_destroy(lecf->data_lengths);
        }
        if (lecf->data_values != NULL && lecf->data_values != NGX_CONF_UNSET_PTR) {
            ngx_array_destroy(lecf->data_values);
        }
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() clean data %V", &value[1]);
    }

    /* we support multiline logs format */

    /* get the size of the log len (because multiline formats) */
    for (i = 2; i < cf->args->nelts; i++) {
        log_len = log_len + value[i].len;
    }

    log_format = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    log_format->len = log_len;
    log_format->data = ngx_palloc(cf->pool, log_len + 1);

    p = &(log_format->data[0]);

    for (i = 2; i < cf->args->nelts; i++) {
        len = value[i].len;
        ngx_memcpy(p, value[i].data, len);
        p = p + len;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() value: \"%V\"", &value[2]);

    /* recompile all together */
    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.cf = cf;
    sc.source = log_format;
    sc.lengths = &(lecf->data_lengths);
    sc.values = &(lecf->data_values);
    sc.variables = ngx_http_script_variables_count(log_format);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() compile");

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "error compiling brokerlog format");
        return NGX_CONF_ERROR;
    }

    /* set the format as done */
    lecf->fset = 1;

    ngx_pfree(cf->pool, log_format);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_format() end %V", &value[1]);

    return NGX_CONF_OK;
}
/**
 * @brief nginx module's set endpoint
 *
 * Like ngx_http_brokerlog_set_format this function is pre-compiled. This is the endpoint
 * for the broker message. To receive messages we have to be listening this topic. By nature
 * this endpoint can be dynamic in the same way we have variables in the configuration file.
 *
 * @code{.conf}
 * brokerlog_endpoint definition "/servers/nginx/$domain"
 * @endcode
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param cmd A pointer to ngx_commant_t that defines the configuration line
 * @param conf A pointer to the configuration received
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 * @note XXX it can be refactor (similar to the set_format)
 */
static char *
ngx_http_brokerlog_set_endpoint(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_brokerlog_loc_conf_t     *llcf = conf;
    ngx_http_brokerlog_element_conf_t *lecf, *clecf;
    ngx_str_t                         *value;
    ngx_http_script_compile_t         sc;
    ngx_uint_t                        i, found = 0;
#if (NGX_DEBUG)
    ngx_log_t                         *log = cf->log;
#endif

    /* value[0] variable name
     * value[1] definition name
     * value[2] endpoint
   */
    value = cf->args->elts;

    /* if this location has defined logs, search for the definition passed in the
     * endpoint. We should create a new log if not found or, update data if a definition
     * was found */
    if (llcf->logs != NULL && llcf->logs != NGX_CONF_UNSET_PTR) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() searching... %V", &value[1]);
        for (i = 0; i < llcf->logs->nelts; i++) {
            clecf = ((ngx_http_brokerlog_element_conf_t *) llcf->logs->elts) + i;
            /* we are literally searching for the name */
            if ((clecf->name != NULL) && (clecf->name->len == value[1].len)
                    && (ngx_strncmp(clecf->name->data, value[1].data, clecf->name->len) == 0))
            {
          found = 1;
          lecf = clecf;
          break;
      }
        }
        if (found == 0) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() not found");
            lecf = ngx_array_push(llcf->logs);
        }

    } else {
        /* logs are not defined, we create a new ngx_array structure */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() creating logs");
        llcf->logs = ngx_array_create(cf->pool, 1, sizeof(ngx_http_brokerlog_element_conf_t));
        if (llcf->logs == NULL) {
            return NGX_CONF_ERROR;
        }
        llcf->logs->nelts = 1;
        lecf = (ngx_http_brokerlog_element_conf_t *) llcf->logs->elts;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() lecf");

    /* both cases, where no definition found or no logs were setted before, we do some basic initialization
     * like the log name and cycle logger */
    if (found == 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() name not found");
        if (lecf == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_endpoint() lecf == NULL");
            return NGX_CONF_ERROR;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() create lecf name");

        lecf->name = ngx_palloc(cf->pool, sizeof(ngx_str_t));
        if (lecf->name == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_endpoint() lecf->name == NULL");
            return NGX_CONF_ERROR;
        }
        lecf->name->data = ngx_palloc(cf->pool, value[1].len);
        ngx_memcpy(lecf->name->data, value[1].data, value[1].len);
        lecf->name->len = value[1].len;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() initialize element conf %V", &value[1]);
        lecf->log = cf->cycle->log;
        lecf->off = 0;
        lecf->server = NGX_CONF_UNSET_PTR;
        lecf->ctx = NGX_CONF_UNSET_PTR;
    } else {
        if (lecf->endpoint_lengths != NULL && lecf->endpoint_lengths != NGX_CONF_UNSET_PTR) {
            ngx_array_destroy(lecf->endpoint_lengths);
        }
        if (lecf->endpoint_values != NULL && lecf->endpoint_values != NGX_CONF_UNSET_PTR) {
            ngx_array_destroy(lecf->endpoint_values);
        }
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() clean data %V", &value[1]);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() value: \"%V\"", &value[2]);

    /* the endpoint is a string where we can place some nginx environment variables which are compiled
     * each time we process them. here we evaluate this set of data and prepare them to be used */
    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.cf = cf;
    sc.source = &value[2];
    sc.lengths = &(lecf->endpoint_lengths);
    sc.values = &(lecf->endpoint_values);
    sc.variables = ngx_http_script_variables_count(&value[2]);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() compile");

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "error compiling brokerlog endpoint");
        return NGX_CONF_ERROR;
    }

    /* mark the endpoint as setted */
    lecf->eset = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_endpoint() end");

    return NGX_CONF_OK;
}

static char *
ngx_http_brokerlog_set_off(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_brokerlog_loc_conf_t        *llcf = conf;
    ngx_http_brokerlog_element_conf_t    *lecf, *clecf;
    ngx_str_t                            *value;
    ngx_uint_t                           i, found = 0;
#if (NGX_DEBUG)
    ngx_log_t                            *log = cf->log;
#endif

    /* value[0] variable name
     * value[1] definition name
   */
    value = cf->args->elts;

    if ((value[1].len == 3) && (ngx_strncmp(value[1].data, "all", value[1].len) == 0)) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() ... all");
        llcf->off = 1;
        return NGX_CONF_OK;
    }
    /* if this location has defined logs, search for the definition passed in the
     * endpoint. We should create a new log if not found or, update data if a definition
     * was found */
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() init searching... %V", &value[1]);
    if (llcf->logs != NULL && llcf->logs != NGX_CONF_UNSET_PTR) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() searching... %V", &value[1]);
        for (i = 0; i < llcf->logs->nelts; i++) {
            clecf = ((ngx_http_brokerlog_element_conf_t *) llcf->logs->elts) + i;
            /* we are literally searching for the name */
            if ((clecf->name != NULL) && (clecf->name->len == value[1].len)
                    && (ngx_strncmp(clecf->name->data, value[1].data, clecf->name->len) == 0)) {
               found = 1;
               lecf = clecf;
               ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() ... %V", &value[1]);
               break;
            }
        }
        if (found == 0) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() not found");
            lecf = ngx_array_push(llcf->logs);
        }
    } else {
        /* location has no logs, create a new array structure to use */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() creating logs");
        llcf->logs = ngx_array_create(cf->pool, 1, sizeof(ngx_http_brokerlog_element_conf_t));
        if (llcf->logs == NULL) {
            return NGX_CONF_ERROR;
        }
        llcf->logs->nelts = 1;
        lecf = (ngx_http_brokerlog_element_conf_t *) llcf->logs->elts;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() lecf");

    if (found == 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() name not found");
        if (lecf == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_off() lecf == NULL");
            return NGX_CONF_ERROR;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() create lecf name");
        lecf->name = ngx_palloc(cf->pool, sizeof(ngx_str_t));
        if (lecf->name == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "brokerlog_zmq: set_off() lecf->name == NULL");
            return NGX_CONF_ERROR;
        }
        lecf->name->data = ngx_palloc(cf->pool, value[1].len);
        lecf->name->len = value[1].len;
        ngx_memcpy(lecf->name->data, value[1].data, value[1].len);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() initialize element conf %V", &value[1]);
        lecf->log = cf->cycle->log;
        lecf->data_lengths = NGX_CONF_UNSET_PTR;
        lecf->data_values = NGX_CONF_UNSET_PTR;
        lecf->endpoint_lengths = NGX_CONF_UNSET_PTR;
        lecf->endpoint_values = NGX_CONF_UNSET_PTR;
        lecf->server = NGX_CONF_UNSET_PTR;
        lecf->ctx = NGX_CONF_UNSET_PTR;
    }

    lecf->off = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "brokerlog_zmq: set_off() end");
    return NGX_CONF_OK;
}

/**
 * @brief nginx module after the configuration was submited
 *
 * In this phase of the module initiation we push our handler to the
 * core handler list to be proccessed each request
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @return A ngx_int_t which can be NGX_ERROR | NGX_OK
 */
static ngx_int_t
ngx_http_brokerlog_postconf(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t            *cmcf;
    ngx_http_handler_pt                    *h;
#if (NGX_DEBUG)
    ngx_log_t                            *log = cf->cycle->log;
#endif

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "NGINX ZMQ MODULE: postconf() ERROR");
        return NGX_ERROR;
    }

    *h = ngx_http_brokerlog_handler;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "NGINX ZMQ MODULE: postconf() OK");

    return NGX_OK;
}

/**
 * @brief nginx module after exit the master proccess
 *
 * We are exiting nginx and, to avoid memory problems, we clean all
 * stuff we created before.
 *
 * @param cycle A ngx_cycle_t pointer to the current nginx cycle
 * @return Nothing
 */
static void
ngx_http_brokerlog_exitmaster(ngx_cycle_t *cycle)
{
}
