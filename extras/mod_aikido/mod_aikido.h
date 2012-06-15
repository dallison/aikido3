#ifndef _MOD_AIKIDO_H
#define _MOD_AIKIDO_H 1

#include "mod_include.h"

typedef enum {RUN_AS_SSI, RUN_AS_CGI} prog_types;

typedef struct {
    apr_int32_t          in_pipe;
    apr_int32_t          out_pipe;
    apr_int32_t          err_pipe;
    int                  process_cgi;
    apr_cmdtype_e        cmd_type;
    apr_int32_t          detached;
    prog_types           prog_type;
    apr_bucket_brigade **bb;
    include_ctx_t       *ctx;
    ap_filter_t         *next;
    apr_int32_t          addrspace;
} cgi_exec_info_t;

/**
 * Registerable optional function to override CGI behavior;
 * Reprocess the command and arguments to execute the given CGI script.
 * @param cmd Pointer to the command to execute (may be overridden)
 * @param argv Pointer to the arguments to pass (may be overridden)
 * @param r The current request
 * @param p The pool to allocate correct cmd/argv elements within.
 * @param process_cgi Set true if processing r->filename and r->args
 *                    as a CGI invocation, otherwise false
 * @param type Set to APR_SHELLCMD or APR_PROGRAM on entry, may be
 *             changed to invoke the program with alternate semantics.
 * @param detach Should the child start in detached state?  Default is no. 
 * @remark This callback may be registered by the os-specific module 
 * to correct the command and arguments for apr_proc_create invocation
 * on a given os.  mod_cgi will call the function if registered.
 */
APR_DECLARE_OPTIONAL_FN(apr_status_t, ap_cgi_build_command, 
                        (const char **cmd, const char ***argv,
                         request_rec *r, apr_pool_t *p, 
                         cgi_exec_info_t *e_info));

#endif /* _MOD_AIKIDO_H */
/** @} */


