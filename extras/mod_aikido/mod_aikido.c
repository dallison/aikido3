/*
 * mod_aikido
 * Author: David Allison (Xsigo Systems Inc.)
 *
 * This code is modeled on the mod_cgi.c code from the apache2 distribution.
 *
 * This module provides a servlet/webapp interface using the Aikido interpreter.  The interpreter is
 * linked into the module.  The servlet code is compiled when the module starts and is executed for
 * each incoming request.  The execution is done in a separate thread so that the bucket can read
 * the data from the pipe while the script is running.  The communication is done through pipes (actually
 * socketpairs, but pipes work too).
 */

#include "apr.h"
#include "apr_strings.h"
#include "apr_optional.h"
#include "apr_buckets.h"
#include "apr_lib.h"
#include "apr_poll.h"

#define APR_WANT_STRFUNC
#include "apr_want.h"
#include <pthread.h>

#define CORE_PRIVATE


#include "util_filter.h"
#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_main.h"
#include "http_log.h"
#include "util_script.h"
#include "ap_mpm.h"
#include "mod_core.h"
#include "mod_aikido.h"
#include <unistd.h>

#include <sys/socket.h>

module AP_MODULE_DECLARE_DATA aikido_module;

/* Read and discard the data in the brigade produced by a CGI script */
static void discard_script_output(apr_bucket_brigade *bb);

/* aikido interpreter init function */
/* these functions are in the aikidomod.cc file */
extern void *aikido_init (const char *zipdir, const char *webappdir);
extern void *aikido_new_request (request_rec *req);
extern void aikido_delete_request (void *req);
extern int aikido_check_uri (void *aikido, void *req);
extern void aikido_run (void *aikido, void *req, int in, int out);
extern int aikido_debug;

/* the aikido object (opaque handle) */
static void *aikido;
static const char *webappdir = "/etc/apache2/extra/mod_aikido";
static const char *aikidozipdir = "/etc/apache2/extra/mod_aikido";


/* commands */
static const char *set_webapp_dir(cmd_parms *cmd, void *dummy, const char *arg) {
    webappdir = arg;
    return NULL;
}

static const char *set_aikido_dir(cmd_parms *cmd, void *dummy, const char *arg) {
    aikidozipdir = arg;
    return NULL;
}

static const char *set_aikido_debug(cmd_parms *cmd, void *dummy, const char *arg) {
    aikido_debug = strcmp (arg, "true") == 0;
    return NULL;
}

static const char *add_aikido_path(cmd_parms *cmd, void *dummy, const char *arg) {
    char buf[1024];
    char *path = getenv ("AIKIDOPATH");
    if (path != NULL) {
        strcpy (buf, path);
        strcat (buf, ":");
        strcat (buf, arg);
    } else {
        strcpy (buf, arg);
    }
    setenv ("AIKIDOPATH", buf, 1);
    return NULL;
}



static const command_rec aikido_cmds[] =
{

AP_INIT_TAKE1("AikidoWebApps", set_webapp_dir, NULL, RSRC_CONF,
     "The path of the Aikido web applications"),

AP_INIT_TAKE1("AikidoRootDir", set_aikido_dir, NULL, RSRC_CONF,
     "The directory containing the aikido.zip file"),

AP_INIT_TAKE1("AikidoAddPath", add_aikido_path, NULL, RSRC_CONF,
     "Add a directory to the AIKIDOPATH environment variable"),

AP_INIT_TAKE1("AikidoDebugMode", set_aikido_debug, NULL, RSRC_CONF,
     "Set the mod_aikido debug mode (true or false)"),

};

/*
 * we have to create our own bucket type for reading from the sockets.  The apr_bucket_pipe_make function
 * seems to only read once and therefore a large amount of data seems to block the write to the pipe
 */

static const apr_bucket_type_t bucket_type_aikido;

struct aikido_bucket_data {
    apr_pollset_t *pollset;
    request_rec *r;
};

/* Create a  bucket using pipes from script stdout 'out'
 * and stderr 'err', for request 'r'. */
static apr_bucket *aikido_bucket_create(request_rec *r,
                                     apr_file_t *out,
                                     apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);
    apr_status_t rv;
    apr_pollfd_t fd;
    struct aikido_bucket_data *data = apr_palloc(r->pool, sizeof *data);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    b->type = &bucket_type_aikido;
    b->length = (apr_size_t)(-1);
    b->start = -1;

    /* Create the pollset */
    rv = apr_pollset_create(&data->pollset, 2, r->pool, 0);
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, 
                     "apr_pollset_create(); check system or user limits");
        return NULL;
    }

    fd.desc_type = APR_POLL_FILE;
    fd.reqevents = APR_POLLIN;
    fd.p = r->pool;
    fd.desc.f = out; /* script's stdout */
    fd.client_data = (void *)1;
    rv = apr_pollset_add(data->pollset, &fd);
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, 
                     "apr_pollset_add(); check system or user limits");
        return NULL;
    }

    data->r = r;
    b->data = data;
    return b;
}

/* Create a duplicate CGI bucket using given bucket data */
static apr_bucket *aikido_bucket_dup(struct aikido_bucket_data *data,
                                  apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);
    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    b->type = &bucket_type_aikido;
    b->length = (apr_size_t)(-1);
    b->start = -1;
    b->data = data;
    return b;
}

/* Handle stdout from CGI child.  Duplicate of logic from the _read
 * method of the real APR pipe bucket implementation. */
static apr_status_t aikido_read_stdout(apr_bucket *a, apr_file_t *out,
                                    const char **str, apr_size_t *len)
{
    char *buf;
    apr_status_t rv;

    *str = NULL;
    *len = APR_BUCKET_BUFF_SIZE;
    buf = apr_bucket_alloc(*len, a->list); /* XXX: check for failure? */

    rv = apr_file_read(out, buf, len);

    if (rv != APR_SUCCESS && rv != APR_EOF) {
        apr_bucket_free(buf);
        return rv;
    }

    if (*len > 0) {
        struct aikido_bucket_data *data = a->data;
        apr_bucket_heap *h;

        /* Change the current bucket to refer to what we read */
        a = apr_bucket_heap_make(a, buf, *len, apr_bucket_free);
        h = a->data;
        h->alloc_len = APR_BUCKET_BUFF_SIZE; /* note the real buffer size */
        *str = buf;
        APR_BUCKET_INSERT_AFTER(a, aikido_bucket_dup(data, a->list));
    }
    else {
        apr_bucket_free(buf);
        a = apr_bucket_immortal_make(a, "", 0);
        *str = a->data;
    }
    return rv;
}

/* Read method of CGI bucket: polls on stderr and stdout of the child,
 * sending any stderr output immediately away to the error log. */
static apr_status_t aikido_bucket_read(apr_bucket *b, const char **str,
                                    apr_size_t *len, apr_read_type_e block)
{
    struct aikido_bucket_data *data = b->data;
    apr_interval_time_t timeout;
    apr_status_t rv;
    int gotdata = 0;

    timeout = block == APR_NONBLOCK_READ ? 0 : data->r->server->timeout;

    do {
        const apr_pollfd_t *results;
        apr_int32_t num;

        rv = apr_pollset_poll(data->pollset, timeout, &num, &results);
        if (APR_STATUS_IS_TIMEUP(rv)) {
            if (timeout) {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, data->r, 
                              "Timeout waiting for output from CGI script %s",
                              data->r->filename);
                return rv;
            }
            else {
                return APR_EAGAIN;
            }
        }
        else if (APR_STATUS_IS_EINTR(rv)) {
            continue;
        }
        else if (rv != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, data->r, 
                          "poll failed waiting for CGI child");
            return rv;
        }

        for (; num; num--, results++) {
            rv = aikido_read_stdout(b, results[0].desc.f, str, len);
            if (APR_STATUS_IS_EOF(rv)) {
                rv = APR_SUCCESS;
            }
            gotdata = 1;
        }

    } while (!gotdata);

    return rv;
}

static const apr_bucket_type_t bucket_type_aikido = {
    "Aikido", 5, APR_BUCKET_DATA,
    apr_bucket_destroy_noop,
    aikido_bucket_read,
    apr_bucket_setaside_notimpl,
    apr_bucket_split_notimpl,
    apr_bucket_copy_notimpl
};




static void discard_script_output(apr_bucket_brigade *bb)
{
    apr_bucket *e;
    const char *buf;
    apr_size_t len;
    apr_status_t rv;
    e = APR_BRIGADE_FIRST(bb);
    while (e != APR_BRIGADE_SENTINEL(bb)) {
        if (APR_BUCKET_IS_EOS(e)) {
            break;
        }
        rv = apr_bucket_read(e, &buf, &len, APR_BLOCK_READ);
        if (rv != APR_SUCCESS) {
            break;
        }
        e = APR_BUCKET_NEXT(e);
    }
}


/*
 * the data to pass to the script thread
 */
typedef struct {
    void *req;      /* aikido request object (opaque) */
    int in;         /* input file descriptor */
    int out;        /* output file descriptor */
} ThreadData;

/*
 * thread to run the script.  Result is meaningless
 */
static void *script_thread (void *d) {
    ThreadData *data = (ThreadData*)d;
    aikido_run (aikido, data->req, data->in, data->out);
    return NULL;
}

static int aikido_handler(request_rec *r)
{
    apr_bucket_brigade *bb;
    apr_bucket *b;
    int seen_eos, child_stopped_reading;
    apr_pool_t *p;
    apr_status_t rv;
    conn_rec *c = r->connection;
    void *aikidoreq;
    int inpipes[2];
    int outpipes[2];
    apr_file_t *infile;
    apr_file_t *outfile;
    int e;
    pthread_t tid;
    pthread_attr_t attrs;
    ThreadData tdata;
    void *tresult;

    if (aikido == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "mod_aikido failed to initalize");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    p = r->main ? r->main->pool : r->pool;

    if (r->method_number == M_OPTIONS) {
        r->allowed |= (AP_METHOD_BIT << M_GET);
        r->allowed |= (AP_METHOD_BIT << M_POST);
        return DECLINED;
    }

    // create the request object
    aikidoreq = aikido_new_request (r);

    if (!aikido_check_uri(aikido, aikidoreq)) {
        aikido_delete_request (aikidoreq);
        return DECLINED;
    }

    e = socketpair (AF_UNIX, SOCK_STREAM, 0, inpipes);
    e |= socketpair (AF_UNIX, SOCK_STREAM, 0, outpipes);
    if (e != 0) {
        perror ("socketpair");
        aikido_delete_request (aikidoreq);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "Failed to create pipes");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // redirect the output from the webapp to our outfile
    apr_os_file_put (&outfile, &outpipes[0], O_RDONLY, r->pool);   // open for reading from outpipes pipe

    // redirect the input to the webapp from out infile
    apr_os_file_put (&infile, &inpipes[1], O_CREAT | O_WRONLY, r->pool);        // open for writing to inpipes pipe

    ap_add_common_vars(r);
    ap_add_cgi_vars(r);


    bb = apr_brigade_create(r->pool, c->bucket_alloc);
    seen_eos = 0;
    child_stopped_reading = 0;
    do {
        apr_bucket *bucket;

        rv = ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES,
                            APR_BLOCK_READ, HUGE_STRING_LEN);
       
        if (rv != APR_SUCCESS) {
            close (inpipes[0]);
            close (inpipes[1]);
            close (outpipes[0]);
            close (outpipes[1]);
            aikido_delete_request (aikidoreq);
            return rv;
        }

        bucket = APR_BRIGADE_FIRST(bb);
        while (bucket != APR_BRIGADE_SENTINEL(bb)) {
            const char *data;
            apr_size_t len;

            if (APR_BUCKET_IS_EOS(bucket)) {
                seen_eos = 1;
                break;
            }

            /* We can't do much with this. */
            if (APR_BUCKET_IS_FLUSH(bucket)) {
                continue;
            }

            /* If the child stopped, we still must read to EOS. */
            if (child_stopped_reading) {
                continue;
            } 

            /* read */
            apr_bucket_read(bucket, &data, &len, APR_BLOCK_READ);
            
            /* Keep writing data to the child until done or too much time
             * elapses with no progress or an error occurs.
             */
            rv = apr_file_write_full(infile, data, len, NULL);

            if (rv != APR_SUCCESS) {
                /* silly script stopped reading, soak up remaining message */
                child_stopped_reading = 1;
            }
            bucket = APR_BUCKET_NEXT(bucket);
        }
        apr_brigade_cleanup(bb);
    }
    while (!seen_eos);

    /* Is this flush really needed? */
    apr_file_flush(infile);
    apr_file_close(infile);

    apr_brigade_cleanup(bb);

    /* start the reader thread */
    e = pthread_attr_init (&attrs);
    if (e != 0) {
        close (inpipes[0]);
        close (inpipes[1]);
        close (outpipes[0]);
        close (outpipes[1]);
        aikido_delete_request (aikidoreq);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "Failed to allocate script thread attributes");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    e = pthread_attr_setdetachstate (&attrs, PTHREAD_CREATE_JOINABLE) ;
    if (e != 0) {
        close (inpipes[0]);
        close (inpipes[1]);
        close (outpipes[0]);
        close (outpipes[1]);
        aikido_delete_request (aikidoreq);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "Failed to set script thread attributes");
        return HTTP_INTERNAL_SERVER_ERROR;
    }


    tdata.req = aikidoreq;
    tdata.in = inpipes[0];
    tdata.out = outpipes[1];

    e = pthread_create (&tid, NULL, script_thread, &tdata) ;
    if (e != 0) {
        close (inpipes[0]);
        close (inpipes[1]);
        close (outpipes[0]);
        close (outpipes[1]);
        aikido_delete_request (aikidoreq);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
                      "Failed to run script thread");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* script thread is now executing script with i/o redirected to the pipes */


    /* read the script output from the pipe*/
    apr_file_pipe_timeout_set(outfile, 0);

    b = aikido_bucket_create(r, outfile, c->bucket_alloc);
    if (b == NULL) {
        pthread_join (tid, &tresult);       /* wait for script to complete */
        close (inpipes[0]);
        close (inpipes[1]);
        close (outpipes[0]);
        close (outpipes[1]);
        aikido_delete_request (aikidoreq);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    APR_BRIGADE_INSERT_TAIL(bb, b);
    b = apr_bucket_eos_create(c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);

    ap_pass_brigade(r->output_filters, bb);

    /* wait for the script thread to complete */
    pthread_join (tid, &tresult);
    
    /* terminate script output */
    close (inpipes[0]);
    close (inpipes[1]);
    close (outpipes[0]);
    close (outpipes[1]);

    /* delete the aikido request objects */
    aikido_delete_request (aikidoreq);

    return OK;
}


static void aikido_child_init(apr_pool_t *p, server_rec *s)
{
    // initialize Aikido here
    aikido = aikido_init (aikidozipdir, webappdir);
}

static void register_hooks(apr_pool_t *p)
{
    ap_hook_handler(aikido_handler, NULL, NULL, APR_HOOK_FIRST);
    ap_hook_child_init(aikido_child_init, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA aikido_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,                        /* dir config creater */
    NULL,                        /* dir merger --- default is to override */
    NULL, //create_aikido_config,           /* server config */
    NULL, //merge_aikido_config,            /* merge server config */
    aikido_cmds,                    /* command apr_table_t */
    register_hooks               /* register hooks */
};

