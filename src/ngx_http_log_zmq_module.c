/******************************************************************************
 * Copyright (c) 2014-2015 by SAPO - PT Comunicações
 * Copyright (c) 2016 by Altice Labs
 *
 *****************************************************************************/

/**
 * @file ngx_http_log_zmq_module.c
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

#include "ngx_http_log_zmq_module.h"

/**
 * @brief get default port for the input type of protocol
 *
 * This function is responsable to handle the request and log
 * the data we want to the log_zmq via zmq proccess.
 * It's important to note that if this function fails it should
 * not kill the nginx normal running. After all, this is the log
 * phase.
 *
 * @param kind A ngx_log_zmq_server_kind with the value TCP|IPC|INPROC
 * @return A in_port_t with an integer with the port
 * @note Should we move this to ngx_http_log_zmq.c ?
 */

static in_port_t __get_default_port(const ngx_log_zmq_server_kind kind){
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

static void *ngx_http_log_zmq_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_log_zmq_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_log_zmq_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_log_zmq_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static char *ngx_http_log_zmq_set_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_log_zmq_set_format(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_log_zmq_set_endpoint(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_log_zmq_set_off(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_http_log_zmq_element_conf_t *ngx_http_log_zmq_create_definition(ngx_conf_t *cf, ngx_http_log_zmq_main_conf_t *bkmc, ngx_str_t *name);
static ngx_http_log_zmq_loc_element_conf_t *ngx_http_log_zmq_create_location_element(ngx_conf_t *cf, ngx_http_log_zmq_loc_conf_t *llcf, ngx_str_t *name);

static ngx_int_t ngx_http_log_zmq_postconf(ngx_conf_t *cf);
static void ngx_http_log_zmq_exitmaster(ngx_cycle_t *cycle);

static ngx_command_t  ngx_http_log_zmq_commands[] = {

    { ngx_string("log_zmq_server"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE5,
      ngx_http_log_zmq_set_server,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("log_zmq_format"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_2MORE,
      ngx_http_log_zmq_set_format,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("log_zmq_endpoint"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_log_zmq_set_endpoint,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    { ngx_string("log_zmq_off"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_log_zmq_set_off,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    ngx_null_command
};

static ngx_http_module_t  ngx_http_log_zmq_module_ctx = {
    NULL,                                /* preconfiguration */
    ngx_http_log_zmq_postconf,           /* postconfiguration */
    ngx_http_log_zmq_create_main_conf,   /* create main configuration */
    ngx_http_log_zmq_init_main_conf,     /* init main configuration */
    NULL,                                /* create server configuration */
    NULL,                                /* merge server configuration */
    ngx_http_log_zmq_create_loc_conf,    /* create location configuration */
    ngx_http_log_zmq_merge_loc_conf      /* merge location configuration */
};

extern ngx_module_t  ngx_http_log_module;

ngx_module_t  ngx_http_log_zmq_module = {
    NGX_MODULE_V1,
    &ngx_http_log_zmq_module_ctx,        /* module context */
    ngx_http_log_zmq_commands,           /* module directives */
    NGX_HTTP_MODULE,                     /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    ngx_http_log_zmq_exitmaster,         /* exit master */
    NGX_MODULE_V1_PADDING
};

/**
 * @brief nginx module's handler for logger phase
 *
 * This function is responsable to handle the request and log
 * the data we want to the log_zmq via zmq proccess.
 * It's important to note that if this function fails it should
 * not kill the nginx normal running. After all, this is the log
 * phase.
 *
 * @param r A ngx_http_request_t that represents the current request
 * @return A ngx_int_t which can be NGX_ERROR | NGX_OK
 * @note If NGX_DEBUG is setted than we print some messages to the debug log
 */
ngx_int_t
ngx_http_log_zmq_handler(ngx_http_request_t *r)
{
    ngx_http_log_zmq_loc_conf_t         *lccf;
    ngx_http_log_zmq_element_conf_t     *clecf;
    ngx_http_log_zmq_loc_element_conf_t *lelcf, *clelcf;
    ngx_uint_t                          i;
    ngx_str_t                           data;
    ngx_str_t                           zmq_data;
    ngx_str_t                           endpoint;
    ngx_pool_t                          *pool = r->connection->pool;
    ngx_log_t                           *log = r->connection->log;
    ngx_int_t (*serializer)(ngx_pool_t*, ngx_str_t*, ngx_str_t*, ngx_str_t*) = NULL;
    zmq_msg_t query;
    int rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler()");

    /* get current location configuration */
    lccf = ngx_http_get_module_loc_conf(r, ngx_http_log_zmq_module);

    /* simply return NGX_OK if location logs are off */
    if (lccf->off == 1) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): all logs off");
        return NGX_OK;
    }

    /* location configuration has an ngx_array of log elements, we should iterate
     * by each one
     */
    lelcf = lccf->logs->elts; /* point to the initial position > element 0 */

    /* we use "continue" for each error in the cycle because we do not want the stop
     * the iteration, but continue to the next log */
    for (i = 0; i < lccf->logs->nelts; i++) {

        clelcf = lelcf + i;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): %V, off=%d",
                       clelcf->element->name, clelcf->off);

        if (clelcf->off == 1) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): element off");
            continue;
        }

        clecf = clelcf->element; /* get the i element of the log array */

        /* worst case? we get a null element ?! */
        if (NULL == clecf) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): no element config");
            continue;
        }

        /* we only proceed if all the variables were setted: endpoint, server, format */
        if (clecf->eset == 0 || clecf->fset == 0 || clecf->sset == 0) {
            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): eset=%d, fset=%d, sset=%d",
                                                       clecf->eset, clecf->fset, clecf->sset);
            continue;
        }

        /* our configuration doesn't has a name? some error ocorred */
        if (NULL == clecf->name || 0 == clecf->name->len) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): no element name");
            continue;
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): setting up \"%V\"", clecf->name);
        }

        /* we set the server variable... but we can use it? */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): checking server to log");
        if (NULL == clecf->server) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): no server to log");
            continue;
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): server connection \"%V\"", clecf->server->connection);
        }

        /* we set the data format... but we don't have any content to sent? */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): checking format to log");
        if (NULL == clecf->data_lengths) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): no format to log");
            continue;
        }

        /* we set the endpoint... but we don't have any valid endpoint? */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): checking endpoint to log");
        if (NULL == clecf->endpoint_lengths) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): no endpoint to log");
            continue;
        }

        /* process all data variables and write them back to the data values */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): script data");
        if (NULL == ngx_http_script_run(r, &data, clecf->data_lengths->elts, 0, clecf->data_values->elts)) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): error script data");
            continue;
        }

        /* process all endpoint variables and write them back the the endpoint values */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): script endpoint");
        if (NULL == ngx_http_script_run(r, &endpoint, clecf->endpoint_lengths->elts, 0, clecf->endpoint_values->elts)) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): error script endpoint");
            continue;
        }

        /* yes, we must go on */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): logging to server");

        /* no data */
        if (0 == data.len) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): no message to log");
            continue;
        }

        /* serialize to the final message format */
        serializer = &log_zmq_serialize;

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): serializing message");
        if (NGX_ERROR == (*serializer)(pool, &endpoint, &data, &zmq_data)) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): error serializing message");
            ngx_pfree(pool, zmq_data.data);
            continue;
        }

        /* no context? we dont create any */
        if (NULL == clecf->ctx) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "log_zmq: handler(): no context");
            continue;
        }

        clecf->ctx->log = log;

        rc = 1; /* we should have a rc = 0 after this call */

        /* create zmq context if needed */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): verify ZMQ context");
        if ((NULL == clecf->ctx->zmq_context) && (0 == clecf->ctx->ccreated)) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): creating context");
            rc = zmq_create_ctx(clecf);
            if (rc != 0) {
                ngx_log_error(NGX_LOG_INFO, log, 0, "log_zmq: handler(): error creating context");
                continue;
            }
        }

        /* open zmq socket if needed */
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): verify ZMQ socket");
        if (NULL == clecf->ctx->zmq_socket && 0 == clecf->ctx->screated) {
            ngx_log_debug0(NGX_LOG_INFO, log, 0, "log_zmq: handler(): creating socket");
            rc = zmq_create_socket(pool, clecf);
            if (rc != 0) {
                ngx_log_error(NGX_LOG_INFO, log, 0, "log_zmq: handler(): error creating socket");
                continue;
            }
        }

        /* initialize zmq message */
        zmq_msg_init_size(&query, zmq_data.len);

        ngx_memcpy(zmq_msg_data(&query), zmq_data.data, zmq_data.len);

        if (zmq_msg_send(&query, clecf->ctx->zmq_socket, 0) >= 0) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): message sent: %V", &zmq_data);
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "log_zmq: handler(): message not sent: %V", &zmq_data);
        }

        /* free all for the next iteration */
        zmq_msg_close(&query);

        ngx_pfree(pool, zmq_data.data);
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
ngx_http_log_zmq_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_log_zmq_main_conf_t *bkmc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->cycle->log, 0, "log_zmq: create_main_conf()");

    bkmc = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_zmq_main_conf_t));
    if (bkmc == NULL) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\" error creating main configuration");
        return NULL;
    }

    bkmc->cycle = cf->cycle;
    bkmc->log = cf->log;
    bkmc->logs = ngx_array_create(cf->pool, 4, sizeof(ngx_http_log_zmq_element_conf_t));
    if (bkmc->logs == NULL) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\" error creating main definitions");
        return NULL;
    }
    ngx_memzero(bkmc->logs->elts, bkmc->logs->size);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->cycle->log, 0, "log_zmq: create_main_conf(): return OK");

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
ngx_http_log_zmq_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: init_main_conf()");

    if (conf == NULL) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\" main configuration not defined");
        return NGX_CONF_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: init_main_conf(): return OK");

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
ngx_http_log_zmq_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_log_zmq_loc_conf_t  *conf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_loc_conf()");

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_zmq_loc_conf_t));
    if (conf == NULL) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\" error creating location configuration");
        return NGX_CONF_ERROR;
    }

    conf->off = 0;
    conf->logs = ngx_array_create(cf->pool, 4, sizeof(ngx_http_log_zmq_loc_element_conf_t));
    if (conf->logs == NULL) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\" error creating location elements");
        return NGX_CONF_ERROR;
    }
    ngx_memzero(conf->logs->elts, conf->logs->size);
    conf->logs_definition = NGX_CONF_UNSET_PTR;
    conf->log = cf->log;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_loc_conf(): return OK");

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
ngx_http_log_zmq_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_log_zmq_main_conf_t        *bkmc;
    ngx_http_log_zmq_loc_conf_t         *prev = parent;
    ngx_http_log_zmq_loc_conf_t         *conf = child;
    ngx_http_log_zmq_element_conf_t     *element;
    ngx_http_log_zmq_element_conf_t     *curelement;
    ngx_http_log_zmq_loc_element_conf_t *locelement;
    ngx_uint_t                          i, j, found;

    bkmc = ngx_http_conf_get_module_main_conf(cf, ngx_http_log_zmq_module);

    if (bkmc == NULL) {
        ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\" main configuration not defined");
        return NGX_CONF_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf()");

    if (NULL == conf->log) {
        conf->log = prev->log;
    }

    if (NULL == conf->logs_definition || NGX_CONF_UNSET_PTR == conf->logs_definition) {
        conf->logs_definition = (ngx_array_t *) prev->logs_definition;
    }

    if (prev->logs_definition && NGX_CONF_UNSET_PTR != prev->logs_definition) {
        element = (ngx_http_log_zmq_element_conf_t *) prev->logs_definition->elts;
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): empty configuration");
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): return OK");

        return NGX_CONF_OK;
    }

    if (NULL == conf->logs || NGX_CONF_UNSET_PTR == conf->logs) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): no log reference");
        conf->logs = ngx_array_create(cf->pool, 4, sizeof(ngx_http_log_zmq_loc_element_conf_t));
        if (conf->logs == NULL) {
            ngx_log_error(NGX_LOG_INFO, cf->log, 0, "\"log_zmq\": error creating location logs");
            return NGX_CONF_ERROR;
        }
        ngx_memzero(conf->logs->elts, conf->logs->size);
    }

    for (i = 0; i < prev->logs_definition->nelts; i++) {
        found = 0;
        locelement = conf->logs->elts;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): verify \"%V\"", element[i].name);
        for (j = 0; j < conf->logs->nelts; j++) {
            curelement = locelement[j].element;
            if (element[i].name->len == curelement->name->len
                && ngx_strncmp(element[i].name->data, curelement->name->data, element[i].name->len) == 0) {
                found = 1;
                ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): \"%V\" found, off==%d",
                               element[i].name, locelement[j].off);
            }
        }
        if (found == 0) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): \"%V\" not found", element[i].name);
            locelement = ngx_array_push(conf->logs);
            ngx_memzero(locelement, sizeof(ngx_http_log_zmq_loc_element_conf_t));
            locelement->off = 0;
            locelement->element = element + i;
        }
    }
#if (NGX_DEBUG)
    locelement = conf->logs->elts;
    for (i = 0; i < conf->logs->nelts; i++) {
         ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): \"%V\": off==%d",
         locelement[i].element->name, locelement[i].off);
    }
#endif

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: merge_loc_conf(): return OK");

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
 * log_zmq_server 127.0.0.1:5555 zmq 10 10000;
 * @endcode
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param cmd A pointer to ngx_commant_t that defines the configuration line
 * @param conf A pointer to the configuration received
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 * @warning It's important to rearrange this to permit MORE2 arguments and not TAKE4
 */
static char *
ngx_http_log_zmq_set_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_log_zmq_main_conf_t        *bkmc;
    ngx_http_log_zmq_loc_conf_t         *llcf = conf;
    ngx_http_log_zmq_element_conf_t     *lecf;
    ngx_http_log_zmq_loc_element_conf_t *lelcf;
    ngx_str_t                           *value;
    const unsigned char                 *kind;
    ngx_int_t                           iothreads;
    ngx_int_t                           qlen;
    ngx_url_t                           u;
    ngx_log_zmq_server_t                *endpoint;
    char                                *connection;
    size_t                              connlen;
    size_t                              zmq_hdlen;

    bkmc = ngx_http_conf_get_module_main_conf(cf, ngx_http_log_zmq_module);

    if (cf->cmd_type != NGX_HTTP_MAIN_CONF) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the \"log_zmq_server\" directive can only used in \"http\" context");
        return NGX_CONF_ERROR;
    }

    if (bkmc == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no \"log_zmq\" main configuration defined");
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

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): definition \"%V\"", &value[1]);
    lecf = ngx_http_log_zmq_create_definition(cf, bkmc, &value[1]);

    if (NULL == lecf) {
        return NGX_CONF_ERROR;
    }

    /* set the location logs to main configuration logs */
    llcf->logs_definition = (ngx_array_t *) bkmc->logs;

    if (lecf->sset == 1) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": \"%V\" was initializated before", &value[1]);
        return NGX_CONF_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): loc definition \"%V\"", &value[1]);
    lelcf = ngx_http_log_zmq_create_location_element(cf, llcf, &value[1]);

    if (NULL == lelcf) {
        return NGX_CONF_ERROR;
    }

    /* create ZMQ context structure */
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): create context");

    lecf->ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_zmq_ctx_t));
    if (NULL == lecf->ctx) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": error creating context \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    lecf->ctx->log = cf->cycle->log;

    /* update definition name and cycle log*/
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): set definition name");

    lecf->name = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    if (lecf->name == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": error setting name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    lecf->name->data = ngx_palloc(cf->pool, value[1].len);
    lecf->name->len = value[1].len;
    ngx_memcpy(lecf->name->data, value[1].data, value[1].len);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): initialize element \"%V\"", &value[1]);
    lecf->log = cf->cycle->log;
    lecf->off = 0;

    /* set the type of protocol TCP|IPC|INPROC */
    kind = value[3].data;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): server kind \"%V\"", &value[3]);

    endpoint = ngx_pcalloc(cf->pool, sizeof(ngx_log_zmq_server_t));

    if (endpoint == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": error creating endpoint \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (0 == ngx_strcmp(kind, ZMQ_TCP_KEY)) {
        endpoint->kind = TCP;
    } else if (0 == ngx_strcmp(kind, ZMQ_IPC_KEY)) {
        endpoint->kind = IPC;
    } else if (0 == ngx_strcmp(kind, ZMQ_INPROC_KEY)) {
        endpoint->kind = INPROC;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": invalid ZMQ connection type: %s \"%V\"", kind, &value[1]);
        return NGX_CONF_ERROR;
    }

    /* set the number of threads associated with this context */
    iothreads = ngx_atoi(value[4].data, value[4].len);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): iothreads \"%V\"", &value[4]);

    if (iothreads == NGX_ERROR || iothreads <= 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": invalid I/O threads %d \"%V\"", iothreads, &value[1]);
        return NGX_CONF_ERROR;
    }

    lecf->iothreads = iothreads;

    /* set the queue size associated with this context */
    qlen = ngx_atoi(value[5].data, value[5].len);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): queue length \"%V\"", &value[5]);

    if (qlen == NGX_ERROR || qlen < 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": invalid queue size %d \"%V\"", qlen, &value[1]);
    }

    lecf->qlen = qlen;

    /* if the protocol used is TCP, parse it and use nginx parse_url to validate the input */
    if (endpoint->kind == TCP) {
        u.url = value[2];
        u.default_port = __get_default_port(endpoint->kind);
        u.no_resolve = 0;
        u.listen = 1;

        if(ngx_parse_url(cf->pool, &u) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": invalid server: %s \"%V\"", u.err, &value[1]);
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
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": invalid endpoint type \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server(): connection %s", connection);

    if (NULL == connection) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_server\": error creating connection \"%V\"", &value[1]);
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

    /* by default, the configuration for this location is unmuted */
    lelcf->element = (ngx_http_log_zmq_element_conf_t *) lecf;
    lelcf->off = 0;

    /* by default, the configuration is unmuted */
    llcf->off = 0;

    ngx_pfree(cf->pool, connection);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_server() return OK \"%V\"", &value[1]);

    return NGX_CONF_OK;
}

/**
 * @brief nginx module's set format
 *
 * This function evaluate the format string introducted in the configuration file.
 * The string is allocated and parsed to evaluate if it has any variables to
 * expand. This is the message sent to the log_zmq.
 *
 * @code{.conf}
 * log_zmq_format definition "put your stuff in here like vars $status"
 * @endcode
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param cmd A pointer to ngx_commant_t that defines the configuration line
 * @param conf A pointer to the configuration received
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 */
static char *
ngx_http_log_zmq_set_format(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_log_zmq_main_conf_t        *bkmc;
    ngx_http_log_zmq_loc_conf_t         *llcf = conf;
    ngx_http_log_zmq_element_conf_t     *lecf;
    ngx_http_log_zmq_loc_element_conf_t *lelcf;
    ngx_str_t                           *log_format, *value;
    ngx_http_script_compile_t           sc;
    size_t                              i, len, log_len;
    u_char                              *p;

    bkmc = ngx_http_conf_get_module_main_conf(cf, ngx_http_log_zmq_module);

    if (cf->cmd_type != NGX_HTTP_MAIN_CONF) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the \"log_zmq_format\" directive can only be used in \"http\" context");
        return NGX_CONF_ERROR;
    }

    if (bkmc == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no \"log_zmq\" main configuration defined");
        return NGX_CONF_ERROR;
    }

    len = 0;
    log_len = 0;

    /* value[0] variable name
     * value[1] definition name
     * value[2] format
     */
    value = cf->args->elts;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format(): definition \"%V\"", &value[1]);
    lecf = ngx_http_log_zmq_create_definition(cf, bkmc, &value[1]);

    if (NULL == lecf) {
        return NGX_CONF_ERROR;
    }

    /* set the location logs to main configuration logs */
    llcf->logs_definition = (ngx_array_t *) bkmc->logs;

    if (lecf->fset == 1) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_format\" %V was initializated before", &value[1]);
        return NGX_CONF_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format(): loc definition \"%V\"", &value[1]);
    lelcf = ngx_http_log_zmq_create_location_element(cf, llcf, &value[1]);

    if (NULL == lelcf) {
        return NGX_CONF_ERROR;
    }

    /* this shoulnd get into this */
    if (lecf->data_lengths != NULL) {
        ngx_pfree(cf->pool, lecf->data_lengths);
        lecf->data_lengths = NULL;
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format(): clean data lengths");
    }
    if (lecf->data_values != NULL) {
        ngx_pfree(cf->pool, lecf->data_values);
        lecf->data_values = NULL;
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format(): clean data values");
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

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format(): value \"%V\"", &value[2]);

    /* recompile all together */
    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.cf = cf;
    sc.source = log_format;
    sc.lengths = &(lecf->data_lengths);
    sc.values = &(lecf->data_values);
    sc.variables = ngx_http_script_variables_count(log_format);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format(): compile");

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_format\": error compiling format \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    /* set the format as done */
    lecf->fset = 1;

    /* by default, this location have all configuration elements unmuted */
    lelcf->element = (ngx_http_log_zmq_element_conf_t *) lecf;
    lelcf->off = 0;

    /* by default, this location is unmuted */
    llcf->off = 0;

    ngx_pfree(cf->pool, log_format);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_format() return OK \"%V\"", &value[1]);

    return NGX_CONF_OK;
}
/**
 * @brief nginx module's set endpoint
 *
 * Like ngx_http_log_zmq_set_format this function is pre-compiled. This is the endpoint
 * for the zmq message. To receive messages we have to be listening this topic. By nature
 * this endpoint can be dynamic in the same way we have variables in the configuration file.
 *
 * @code{.conf}
 * log_zmq_endpoint definition "/servers/nginx/$domain"
 * @endcode
 *
 * @param cf A ngx_conf_t pointer to the main nginx configurion
 * @param cmd A pointer to ngx_commant_t that defines the configuration line
 * @param conf A pointer to the configuration received
 * @return A char pointer which represents the status NGX_CONF_ERROR | NGX_CONF_OK
 * @note XXX it can be refactor (similar to the set_format)
 */
static char *
ngx_http_log_zmq_set_endpoint(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_log_zmq_main_conf_t        *bkmc;
    ngx_http_log_zmq_loc_conf_t         *llcf = conf;
    ngx_http_log_zmq_element_conf_t     *lecf;
    ngx_http_log_zmq_loc_element_conf_t *lelcf;
    ngx_str_t                           *value;
    ngx_http_script_compile_t           sc;

    bkmc = ngx_http_conf_get_module_main_conf(cf, ngx_http_log_zmq_module);

    if (cf->cmd_type != NGX_HTTP_MAIN_CONF) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the \"log_zmq_endpoint\" directive can only used in \"http\" context");
        return NGX_CONF_ERROR;
    }

    if (bkmc == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no \"log_zmq\" main configuration defined");
        return NGX_CONF_ERROR;
    }

    /* value[0] variable name
     * value[1] definition name
     * value[2] endpoint
     */

    value = cf->args->elts;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_endpoint(): definition \"%V\"", &value[1]);
    lecf = ngx_http_log_zmq_create_definition(cf, bkmc, &value[1]);

    if (NULL == lecf) {
        return NGX_CONF_ERROR;
    }

    /* set the location logs to main configuration logs */
    llcf->logs_definition = (ngx_array_t *) bkmc->logs;

    if (lecf->eset == 1) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_endpoint\" %V was initializated before", &value[1]);
        return NGX_CONF_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_endpoint(): loc definition \"%V\"", &value[1]);
    lelcf = ngx_http_log_zmq_create_location_element(cf, llcf, &value[1]);

    if (NULL == lelcf) {
        return NGX_CONF_ERROR;
    }

    if (lecf->endpoint_lengths != NULL) {
        ngx_pfree(cf->pool, lecf->endpoint_lengths);
        lecf->endpoint_lengths = NULL;
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_endpoint(): clean endpoint lengths");
    }
    if (lecf->endpoint_values != NULL) {
        ngx_pfree(cf->pool, lecf->endpoint_values);
        lecf->endpoint_values = NULL;
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_endpoint(): clean endpoint values");
    }

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

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_endpoint(): compile");

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_endpoint\": error compiling format \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    /* mark the endpoint as setted */
    lecf->eset = 1;

    lelcf->element = (ngx_http_log_zmq_element_conf_t *) lecf;
    lelcf->off = 0;
    llcf->off = 0;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_endpoint() return OK \"%V\"", &value[1]);

    return NGX_CONF_OK;
}

static char *
ngx_http_log_zmq_set_off(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_log_zmq_main_conf_t        *bkmc;
    ngx_http_log_zmq_loc_conf_t         *llcf = conf;
    ngx_http_log_zmq_element_conf_t     *lecf;
    ngx_http_log_zmq_loc_element_conf_t *lelcf;
    ngx_str_t                           *value;
    ngx_uint_t                          i, found = 0;

    bkmc = ngx_http_conf_get_module_main_conf(cf, ngx_http_log_zmq_module);

    if (NULL == bkmc) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no \"log_zmq\" main configuration defined");
        return NGX_CONF_ERROR;
    }

    if (NULL == bkmc->logs || NGX_CONF_UNSET_PTR == bkmc->logs) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq\" doesn't have any log defined");
        return NGX_CONF_ERROR;
    }

    llcf->logs_definition = (ngx_array_t *) bkmc->logs;

    /* value[0] variable name
     * value[1] definition name
     */
    value = cf->args->elts;

    if ((value[1].len == 3) && (ngx_strncmp(value[1].data, "all", value[1].len) == 0)) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_off(): all");
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_off(): return OK");
        llcf->off = 1;
        return NGX_CONF_OK;
    }

    /* let's verify if we are muting an existent definition */
    lecf = bkmc->logs->elts;
    for (i = 0; i < bkmc->logs->nelts; i++) {
        if (lecf[i].name->len == value[1].len
            && ngx_strncmp(lecf[i].name->data, value[1].data, lecf[i].name->len) == 0) {
            lecf = lecf + i;
            found = 1;
            break;
        }
    }

    if (!found) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq_off\": \"%V\" definition not found", &value[1]);
        return NGX_CONF_ERROR;
    }

    llcf->off = 0;
    found = 0;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_off(): loc definition \"%V\"", &value[1]);
    lelcf = ngx_http_log_zmq_create_location_element(cf, llcf, &value[1]);

    if (NULL == lelcf) {
        return NGX_CONF_ERROR;
    }

    lelcf->off = 1;
    lelcf->element = (ngx_http_log_zmq_element_conf_t *) lecf;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_off(): \"%V\", off=%d (\"%V\")",
    lelcf->element->name, lelcf->off, &value[1]);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: set_off(): return OK \"%V\"", &value[1]);

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
ngx_http_log_zmq_postconf(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_handler_pt       *h;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->cycle->log, 0, "log_zmq: postconf(): error pushing handler");
        return NGX_ERROR;
    }

    *h = ngx_http_log_zmq_handler;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->cycle->log, 0, "log_zmq: postconf(): return OK");

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
ngx_http_log_zmq_exitmaster(ngx_cycle_t *cycle)
{
}
static ngx_http_log_zmq_element_conf_t *
ngx_http_log_zmq_create_definition(ngx_conf_t *cf, ngx_http_log_zmq_main_conf_t *bkmc, ngx_str_t *name)
{
    ngx_http_log_zmq_element_conf_t *lecf = NULL;
    ngx_uint_t                      i, found;

    found = 0;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_definition(): \"%V\"", name);

    if (bkmc->logs && bkmc->logs != NGX_CONF_UNSET_PTR) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_definition(): search \"%V\"", name);
        lecf = bkmc->logs->elts;
        for (i = 0; i < bkmc->logs->nelts; i++) {
            if (lecf[i].name->len == name->len
                && ngx_strncmp(lecf[i].name->data, name->data, lecf[i].name->len) == 0) {
                lecf = lecf + i;
                found = 1;
                break;
            }
        }
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_definition(): empty definitions");
        bkmc->logs = ngx_array_create(cf->pool, 4, sizeof(ngx_http_log_zmq_element_conf_t));
        if (bkmc->logs == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq\": error creating space for definitions \"%V\"", name);
            return NULL;
        }
        ngx_memzero(bkmc->logs->elts, bkmc->logs->size);
    }
    if (!found) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_definition(): create definition \"%V\"", name);
        lecf = ngx_array_push(bkmc->logs);

        if (NULL == lecf) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq\": error creating definitions \"%V\"", name);
            return NULL;
        }
        ngx_memzero(lecf, sizeof(ngx_http_log_zmq_element_conf_t));
    }

    return lecf;
}

static ngx_http_log_zmq_loc_element_conf_t *
ngx_http_log_zmq_create_location_element(ngx_conf_t *cf, ngx_http_log_zmq_loc_conf_t *llcf, ngx_str_t *name)
{
    ngx_http_log_zmq_loc_element_conf_t *lelcf = NULL;
    ngx_uint_t                          i, found;

    found = 0;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_location_element(): \"%V\"", name);

    if (llcf->logs && llcf->logs != NGX_CONF_UNSET_PTR) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_location_element(): search \"%V\"", name);
        lelcf = llcf->logs->elts;
        for (i = 0; i < llcf->logs->nelts; i++) {
            if (lelcf[i].element->name->len == name->len
                && ngx_strncmp(lelcf[i].element->name->data, name->data, lelcf[i].element->name->len) == 0) {
                lelcf = lelcf + i;
                found = 1;
                break;
            }
        }
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_location_element(): empty location definitions");
        llcf->logs = ngx_array_create(cf->pool, 4, sizeof(ngx_http_log_zmq_loc_element_conf_t));
        if (llcf->logs == NULL) {
           ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq\": error creating location log \"%V\"", name);
           return NULL;
        }
    }

    if (!found) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "log_zmq: create_location_element(): create location definition \"%V\"", name);
        lelcf = ngx_array_push(llcf->logs);
        if (NULL == lelcf) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"log_zmq\": error creating location log \"%V\"", name);
            return NULL;
        }
        ngx_memzero(lelcf, sizeof(ngx_http_log_zmq_loc_element_conf_t));
    }

    return lelcf;
}
