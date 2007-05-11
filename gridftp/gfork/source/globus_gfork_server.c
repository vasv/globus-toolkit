#include "globus_common.h"
#include "globus_i_gfork.h"
#include "errno.h"
#include <sys/types.h>
#include <sys/wait.h>

static globus_mutex_t                   gfork_l_mutex;
static globus_cond_t                    gfork_l_cond;
static globus_hashtable_t               gfork_l_pid_table;
static globus_bool_t                    gfork_l_done = GLOBUS_FALSE;

static globus_list_t *                  gfork_l_pid_list = NULL;

static gfork_i_child_handle_t *         gfork_l_master_child_handle = NULL;
static gfork_i_options_t                gfork_l_options;

static
void
gfork_l_read_header_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
gfork_new_child(
    globus_xio_system_socket_t          socket_handle,
    int                                 read_fd,
    int                                 write_fd);

static
void
gfork_l_write(
    gfork_i_child_handle_t *            to_kid);

void
gfork_log(
    int                                 level,
    char *                              fmt,
    ...)
{
    va_list                             ap;

    if(gfork_l_options.quiet)
    {
        return;
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static
void
gfork_l_stop_posting(
    globus_result_t                     result)
{

    gfork_log(2, "Stopped accepting new clients.\n");
    gfork_log(2, "Server will shut down when all children terminate.\n");
    if(result != GLOBUS_SUCCESS)
    {
        char * tmp_msg = globus_error_print_friendly(
            globus_error_peek(result));
        gfork_log(2, "Error: %s", tmp_msg);
        free(tmp_msg);
    }

    gfork_l_done = GLOBUS_TRUE;
    globus_xio_server_close(gfork_l_options.tcp_server);
    globus_cond_signal(&gfork_l_cond);
}

static
void
gfork_l_kid_read_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{ 
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *) user_arg;

    kid_handle->state = gfork_i_state_next(
        kid_handle->state, GFORK_EVENT_CLOSE_RETURNS);
    assert(kid_handle->state == GFORK_STATE_CLOSED);

    close(kid_handle->write_fd);
    close(kid_handle->read_fd);

    globus_mutex_lock(&gfork_l_mutex);
    {
        globus_list_remove(&gfork_l_pid_list,
            globus_list_search(gfork_l_pid_list, (void *)kid_handle->pid));

        if(!kid_handle->dead)
        {
            kill(kid_handle->pid, SIGKILL);
        }
        globus_free(kid_handle);

        globus_cond_signal(&gfork_l_cond);
    }
    globus_mutex_unlock(&gfork_l_mutex);

}

static
void
gfork_l_kid_write_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{ 
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *) user_arg;

    result = globus_xio_register_close(
        kid_handle->read_xio_handle, NULL,
        gfork_l_kid_read_close_cb,
        kid_handle);
    if(result != GLOBUS_SUCCESS)
    {
        gfork_l_kid_read_close_cb(
            kid_handle->read_xio_handle,
            GLOBUS_SUCCESS,
            kid_handle);
    }
}

static void
gfork_l_write_open_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    int                                 temp_state;
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *) user_arg;

    globus_mutex_lock(&gfork_l_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error_param;
        }

        temp_state =
            gfork_i_state_next(kid_handle->state, GFORK_EVENT_OPEN_RETURNS);
        globus_assert(temp_state != GFORK_STATE_OPEN && "Bad state");
        kid_handle->state = temp_state;
        /* now react to new state */
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;

error_param:

    /* XXX this is a dead master issue */
    globus_mutex_unlock(&gfork_l_mutex);
}

void
gfork_i_write_open(
    gfork_i_child_handle_t *            kid_handle)
{
    globus_result_t                     result;

    kid_handle->header.type = GLOBUS_GFORK_MSG_OPEN;
    kid_handle->header.from_pid = kid_handle->pid;
    kid_handle->header.to_pid = gfork_l_master_child_handle->pid;
    kid_handle->header.size = 0;

    result = globus_xio_register_write(
        gfork_l_master_child_handle->write_xio_handle,
        (globus_byte_t *) &kid_handle->header,
        sizeof(gfork_i_msg_header_t),
        sizeof(gfork_i_msg_header_t),
        NULL,
        gfork_l_write_open_cb,
        kid_handle);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }
    return;
error_write:
    /* dead master issue */
    return;
}

static void
gfork_l_write_close_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *) user_arg;

    result = globus_xio_register_close(
        kid_handle->write_xio_handle, NULL,
        gfork_l_kid_write_close_cb,
        kid_handle);
    if(result != GLOBUS_SUCCESS)
    {
        gfork_l_kid_write_close_cb(
            kid_handle->write_xio_handle,
            GLOBUS_SUCCESS,
            kid_handle);
    }
}

void
gfork_i_write_close(
    gfork_i_child_handle_t *            kid_handle)
{   
    globus_result_t                     result;

    kid_handle->header.type = GLOBUS_GFORK_MSG_CLOSE;
    kid_handle->header.from_pid = kid_handle->pid;
    kid_handle->header.to_pid = gfork_l_master_child_handle->pid;
    kid_handle->header.size = 0;

    result = globus_xio_register_write(
        gfork_l_master_child_handle->write_xio_handle,
        (globus_byte_t *)&kid_handle->header,
        sizeof(gfork_i_msg_header_t),
        sizeof(gfork_i_msg_header_t),
        NULL,
        gfork_l_write_close_cb,
        kid_handle);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }
    return;
error_write:
    /* dead master issue */
    return;
}


static
globus_result_t
gfork_l_spawn_master()
{
    pid_t                               pid;
    int                                 infds[2];
    int                                 outfds[2];
    int                                 read_fd;
    int                                 write_fd;
    int                                 rc;
    gfork_i_options_t *                 gfork_h;
    char                                tmp_str[32];
    globus_result_t                     result;
    GForkFuncName(gfork_l_spawn_master);

    if(gfork_l_options.master_program == NULL)
    {
        gfork_log(1, "There is no master program.\n");
        return GLOBUS_SUCCESS;
    }

    rc = pipe(infds);
    if(rc != 0)
    {
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_inpipe;
    }
    rc = pipe(outfds);
    if(rc != 0)
    {
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_outpipe;
    }

    pid = fork();
    if(pid == 0)
    {
        setuid(gfork_l_options.master_user);
        /* child node, set uid and exec */
        close(outfds[1]);
        close(infds[0]);

        read_fd = outfds[0];
        write_fd = infds[1];

        /* set up the state pipe and envs */
        sprintf(tmp_str, "%d", read_fd);
        globus_libc_setenv(GFORK_CHILD_READ_ENV, tmp_str, 1);
        sprintf(tmp_str, "%d", write_fd);
        globus_libc_setenv(GFORK_CHILD_WRITE_ENV, tmp_str, 1);

        rc = execv(gfork_h->master_program[0], gfork_h->master_program);

        /* XXX log error */
        gfork_log(1, "Unable to exec program\n");
        exit(rc);
    }
    else if(pid > 0)
    {
        gfork_l_master_child_handle = (gfork_i_child_handle_t *)
            globus_calloc(1, sizeof(gfork_i_child_handle_t));

        gfork_l_master_child_handle->master = GLOBUS_TRUE;
        gfork_l_master_child_handle->write_fd = outfds[1];
        result = gfork_i_make_xio_handle(
            &gfork_l_master_child_handle->write_xio_handle, outfds[1]);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_write_convert;
        }

        gfork_l_master_child_handle->read_fd = infds[0];
        result = gfork_i_make_xio_handle(
            &gfork_l_master_child_handle->read_xio_handle, infds[0]);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_read_convert;
        }

        /* post a read */
        close(outfds[0]);
        close(infds[1]);
        gfork_l_master_child_handle->pid = pid;

        globus_list_insert(&gfork_l_pid_list, 
            (void *)gfork_l_master_child_handle->pid);
    }
    else
    {
        /* XXX log error */
        result =  GForkErrorErrno(strerror(errno), errno);;
        goto error_fork;
    }

    return GLOBUS_SUCCESS;

error_read_convert:
    globus_xio_close(gfork_l_master_child_handle->write_xio_handle, NULL);
error_write_convert:
    globus_free(gfork_l_master_child_handle);
    gfork_l_master_child_handle = NULL;
error_fork:
    close(outfds[1]);
    close(infds[0]);
    close(outfds[0]);
    close(infds[1]);

error_outpipe:
error_inpipe:

    return result;
}

static
void
gfork_l_dead_kid(
    gfork_i_child_handle_t *            kid_handle,
    globus_bool_t                       dead)
{
    gfork_i_state_t                     temp_state;
    gfork_i_options_t *                 gfork_h;

    /* is null if it is already dieing.
       if no master program nothing to do, return
       master pid part is redundant, no possible way for kid to NOT be 
        NULL if there is no maste rprogram */
    if(kid_handle == NULL || gfork_l_master_child_handle == NULL)
    {
        return;
    }
    else
    {
        kid_handle->dead = dead;
        gfork_h = kid_handle->whos_my_daddy;
        globus_hashtable_remove(&gfork_l_pid_table, (void *) kid_handle->pid);

        temp_state = gfork_i_state_next(
            kid_handle->state, GFORK_EVENT_SIGCHILD);
        kid_handle->state = temp_state;

        gfork_i_write_close(kid_handle);
    }
}



/* if the master program dies all associated state dies.
   we dont want to kill the service (ie: this process) or 
    any children.  the children may end up dieing anyway, but
    that is their problem.  here we just make sure we remove
    them all from the comm list and try to restart the server
 */
void
gfork_l_dead_master(
    globus_bool_t                       dead)
{
    globus_list_t *                     list;
    globus_result_t                     result;
    gfork_i_child_handle_t *            kid_handle;

    gfork_log(1, "Master is being executed\n");

    if(gfork_l_master_child_handle == NULL)
    {
        return;
    }

    globus_hashtable_to_list(&gfork_l_pid_table, &list);

    while(!globus_list_empty(list))
    {
        kid_handle = (gfork_i_child_handle_t *)
            globus_list_remove(&list, list);

        gfork_l_dead_kid(kid_handle, GLOBUS_FALSE);
    }

    kid_handle = gfork_l_master_child_handle;
    gfork_l_master_child_handle = NULL;

    result = globus_xio_register_close(
        kid_handle->read_xio_handle,
        NULL,
        gfork_l_kid_read_close_cb,
        kid_handle);
    if(result != GLOBUS_SUCCESS)
    {
        /* XXX kick out a one shot */
        globus_assert(0);
    }
}

void
gfork_i_dead_kid(
    pid_t                               child_pid,
    globus_bool_t                       dead)
{
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *)
        globus_hashtable_remove(
            &gfork_l_pid_table, (void *)child_pid);
    if(kid_handle != NULL)
    {
        gfork_l_dead_kid(kid_handle, dead);
    }
    else
    {
        globus_list_remove(&gfork_l_pid_list,
            globus_list_search(gfork_l_pid_list, (void *)child_pid));

        if(!dead)
        {
            kill(child_pid, SIGKILL);
        }

        gfork_log(2, "Cleaned up child %d, list is at %d\n", 
            child_pid, globus_list_size(gfork_l_pid_list));

        globus_cond_signal(&gfork_l_cond);
    }

}

static
void
gfork_l_sigchld(
    void *                              user_arg)
{
    int                                 child_pid;
    int                                 child_status;
    int                                 child_rc;
    gfork_i_options_t *                 gfork_h;
    globus_result_t                     res;

    gfork_log(2, "Sigchld\n");
    gfork_h = (gfork_i_options_t *) &gfork_l_options;

    globus_mutex_lock(&gfork_l_mutex);
    {
        while((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0)
        {
            if(WIFEXITED(child_status))
            {
                /* normal exit */
                child_rc = WEXITSTATUS(child_status);
            }
            else if(WIFSIGNALED(child_status))
            {
                /* killed by */
            }

            if(gfork_l_master_child_handle != NULL &&
                child_pid == gfork_l_master_child_handle->pid)
            {
                gfork_l_dead_master(GLOBUS_TRUE);
            }
            else
            {
                gfork_i_dead_kid(child_pid, GLOBUS_TRUE);
            }

            gfork_log(2, "Child %d completed\n", child_pid);
        }

        res = globus_callback_register_signal_handler(
            SIGCHLD,
            GLOBUS_FALSE,
            gfork_l_sigchld,
            &gfork_h);
        globus_assert(res == GLOBUS_SUCCESS);
    }
    globus_mutex_unlock(&gfork_l_mutex);
}

static
void
gfork_l_int(
    void *                              user_arg)
{
    gfork_log(2, "Sigint\n");

    globus_mutex_lock(&gfork_l_mutex);
    {
        gfork_l_dead_master(GLOBUS_FALSE);
        gfork_l_stop_posting(GLOBUS_SUCCESS);
    }
    globus_mutex_unlock(&gfork_l_mutex);
}

static
void
gfork_l_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    pid_t                               pid;
    int                                 infds[2];
    int                                 outfds[2];
    int                                 rc;
    gfork_i_options_t *                 gfork_h;
    globus_xio_system_socket_t          socket_handle;
    gfork_i_child_handle_t *            kid_handle;
    GForkFuncName(gfork_l_server_accept_cb);

    gfork_h = (gfork_i_options_t *) &gfork_l_options;

    globus_mutex_lock(&gfork_l_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error_accept;
        }

        rc = pipe(infds);
        if(rc != 0)
        {
            result = GForkErrorErrno(strerror(errno), errno);
            goto error_inpipe;
        }
        rc = pipe(outfds);
        if(rc != 0)
        {
            result = GForkErrorErrno(strerror(errno), errno);
            goto error_outpipe;
        }

        result = globus_xio_handle_cntl(
            handle,
            gfork_i_tcp_driver,
            GLOBUS_XIO_TCP_GET_HANDLE,
            &socket_handle);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_getsocket;
        }

        pid = fork();
        if(pid < 0)
        {
            /* error */
            result = GForkErrorErrno(strerror(errno), errno);
            goto error_fork;
        }
        else if(pid == 0)
        {
            int                         read_fd;
            int                         write_fd;

            read_fd = outfds[0];
            write_fd = infds[1];
            close(outfds[1]);
            close(infds[0]);
            if(gfork_l_master_child_handle == NULL)
            {
                close(outfds[1]);
                close(infds[0]);

                read_fd = -1;
                write_fd = -1;
            }

            gfork_new_child(socket_handle, read_fd, write_fd);

            /* hsould not return from this, if we do it is an error */
            goto error_fork;
        }
        else
        {
            /* server */
            fcntl(outfds[1], F_SETFD, FD_CLOEXEC);
            fcntl(infds[0], F_SETFD, FD_CLOEXEC);

            close(outfds[0]);
            close(infds[1]);

            close(socket_handle);
            globus_list_insert(&gfork_l_pid_list, (void *)pid);
            gfork_log(2, "Started child %d\n", pid);
        }
        /* i think we dont care when the close happens */
        globus_xio_register_close(handle, NULL, NULL, NULL);

        /* only make a child handle if we have a master */
        if(gfork_l_master_child_handle != NULL)
        {
            kid_handle = (gfork_i_child_handle_t *)
                globus_calloc(1, sizeof(gfork_i_child_handle_t));
            kid_handle->pid = pid;
            kid_handle->whos_my_daddy = gfork_h;
            kid_handle->write_fd = outfds[1];
            kid_handle->read_fd = infds[0];
            kid_handle->state = GFORK_STATE_OPENING;
            kid_handle->state = gfork_i_state_next(
                GFORK_STATE_NONE, GFORK_EVENT_ACCEPT_CB);

            result = gfork_i_make_xio_handle(
                &kid_handle->write_xio_handle, kid_handle->write_fd);
            if(result != GLOBUS_SUCCESS)
            {
                gfork_log(1, "write handle make failed %s\n",
                    globus_error_print_friendly(globus_error_get(result)));
            }
            result = gfork_i_make_xio_handle(
                &kid_handle->read_xio_handle, kid_handle->read_fd);
            if(result != GLOBUS_SUCCESS)
            {
                gfork_log(1, "read handle make failed %s\n",
                    globus_error_print_friendly(globus_error_get(result)));
            }
            globus_hashtable_insert(
                &gfork_l_pid_table,
                (void *)pid,
                kid_handle);
            gfork_i_write_open(kid_handle);
        }
        else
        {
            close(outfds[0]);
            close(infds[1]);
        }

        result = globus_xio_server_register_accept(
            gfork_h->tcp_server,
            gfork_l_server_accept_cb,
            gfork_h);
        if(result != GLOBUS_SUCCESS)
        {
            gfork_l_stop_posting(result);
        }
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;

error_fork:
    globus_mutex_lock(&gfork_l_mutex);
    globus_xio_register_close(handle, NULL, NULL, NULL);
    close(socket_handle);
    close(outfds[0]);
    close(outfds[1]);
error_getsocket:
error_outpipe:
    close(infds[0]);
    close(infds[1]);
error_inpipe:
error_accept:
    globus_mutex_unlock(&gfork_l_mutex);

    /* log an error */
    return;
}


static
globus_result_t
gfork_init_server()
{
    gfork_i_options_t *                 gfork_h;
    globus_result_t                     res;
    char *                              contact_string;

    gfork_h = &gfork_l_options;

    /* start the master program */
    res = gfork_l_spawn_master(gfork_h);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_master;
    }

    res = globus_xio_server_create(
        &gfork_h->tcp_server, gfork_i_tcp_attr, gfork_i_tcp_stack);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_create;
    }

    res = globus_xio_server_get_contact_string(
        gfork_h->tcp_server, &contact_string);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_contact;
    }
    gfork_log(1, "Listening on: %s\n", contact_string);
    globus_free(contact_string);

    res = globus_xio_server_register_accept(
        gfork_h->tcp_server,
        gfork_l_server_accept_cb,
        gfork_h);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_register;
    }
    return GLOBUS_SUCCESS;

error_register:
    free(contact_string);
error_contact:
error_create:
error_master:

    return res;
}

/*
 *  post for in child.  Never returns from here 
 */
static
void
gfork_new_child(
    globus_xio_system_socket_t          socket_handle,
    int                                 read_fd,
    int                                 write_fd)
{
    gfork_i_options_t *                 gfork_h;
    globus_result_t                     res;
    int                                 rc = 1;
    char                                tmp_str[32];
    GlobusGForkFuncName(gfork_new_child);

    gfork_h = &gfork_l_options;

    /* set up the state pipe and envs */
    sprintf(tmp_str, "%d", read_fd);
    globus_libc_setenv(GFORK_CHILD_READ_ENV, tmp_str, 1);
    sprintf(tmp_str, "%d", write_fd);
    globus_libc_setenv(GFORK_CHILD_WRITE_ENV, tmp_str, 1);

    /* dup the incoming socket */
    rc = dup2(socket_handle, STDIN_FILENO);
    if(rc < 0)
    {
        res = GForkErrorErrno(strerror, errno);
        goto error_dupin;
    }
    rc = dup2(socket_handle, STDOUT_FILENO);
    if(rc < 0)
    {
        res = GForkErrorErrno(strerror, errno);
        goto error_dupout;
    }
    close(socket_handle);

    /* start it */
    rc = execv(gfork_h->argv[0], gfork_h->argv);
    /* if we get to here ecxec failed, fall through to error handling */

error_dupout:
error_dupin:
    /* log error */
    exit(rc);
}

static
globus_result_t
gfork_i_opts_unknown(
   globus_options_handle_t              opts_handle,
    void *                              unknown_arg,
    int                                 argc,
    char **                             argv)
{
    return globus_error_put(globus_error_construct_error(
        NULL,
        NULL,
        2,
        __FILE__,
        "gfork_i_opts_unknown",
        __LINE__,
        "Unknown parameter: %s",
        unknown_arg));
}

/******************** IO functions ****************************/
static
void 
gfork_l_read_body_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            from_kid;
    gfork_i_child_handle_t *            to_kid;
    gfork_i_msg_t *                     msg;
    globus_list_t *                     list = NULL;

    /* now we have the message, gotta decide where it goes */
    msg = (gfork_i_msg_t *) user_arg;

    globus_mutex_lock(&gfork_l_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error_incoming;
        }

        from_kid = msg->kid;

        if(msg->header.from_pid != from_kid->pid)
        {
            /* just clean up their mess */
            msg->header.from_pid = from_kid->pid;
        }

        /* if it has a specific to_pid just add it to the guys write queue */
        if(msg->header.to_pid > 0)
        {
            to_kid = (gfork_i_child_handle_t *) globus_hashtable_lookup(
                &gfork_l_pid_table, (void *) msg->header.to_pid);
            if(to_kid == NULL)
            {
                /* just cleat in up and  repost header */
                globus_free(msg->data);
                /* assume a bad message, report header */
                result = globus_xio_register_read(
                    handle,
                    (globus_byte_t *)&msg->header,
                    sizeof(msg->header),
                    sizeof(msg->header),
                    NULL,
                    gfork_l_read_header_cb,
                    msg);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_post;
                }
            }
            else
            {
                msg->ref++;
                globus_fifo_enqueue(&to_kid->write_q, msg);
                gfork_l_write(to_kid);
            }
        }
        /* if no specific to_pid behavior depends on master status.
            if negative and not the master it is just a broadcast.
             */
        else if(from_kid->master)
        {
            /* if master sends a negitive pid we need to broadcast it */
            globus_hashtable_to_list(&gfork_l_pid_table, &list);

            while(!globus_list_empty(list))
            {
                to_kid = (gfork_i_child_handle_t *)
                    globus_list_remove(&list, list);

                if(msg->header.to_pid != -to_kid->pid)
                {
                    msg->ref++;
                    globus_fifo_enqueue(&to_kid->write_q, msg);
                    gfork_l_write(to_kid);
                }
            }
        }
        else
        {
            /* just have 1 message case, forward to the master */
            msg->ref++;
            globus_fifo_enqueue(&gfork_l_master_child_handle->write_q, msg);
            gfork_l_write(gfork_l_master_child_handle);
        }
    
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;

error_post:
error_incoming:
    return;
}

static
void
gfork_l_read_header_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_msg_t *                     msg;

    msg = (gfork_i_msg_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        goto error_incoming;
    }

    switch(msg->header.type)
    {
        case GLOBUS_GFORK_MSG_DATA:
            if(msg->header.size <= 0)
            {
                /* assume a bad message, report header */
                result = globus_xio_register_read(
                    handle,
                    (globus_byte_t *)&msg->header,
                    sizeof(msg->header),
                    sizeof(msg->header),
                    NULL,
                    gfork_l_read_header_cb,
                    msg);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_post;
                }
            }
            else
            {
                msg->data = globus_malloc(msg->header.size);

                result = globus_xio_register_read(
                    handle,
                    msg->data,
                    msg->header.size,
                    msg->header.size,
                    NULL,
                    gfork_l_read_body_cb,
                    msg);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_post;
                }
            }
            break;

        /* any of these we consider garbage */
        case GLOBUS_GFORK_MSG_OPEN:
        case GLOBUS_GFORK_MSG_CLOSE:
        default:
            result = globus_xio_register_read(
                handle,
                (globus_byte_t *)&msg->header,
                sizeof(gfork_i_msg_header_t),
                sizeof(gfork_i_msg_header_t),
                NULL,
                gfork_l_read_header_cb,
                msg);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_post;
            }
            break;
    }
    return;

error_incoming:
error_post:

    return;
}


static
void
gfork_l_writev_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            to_kid;
    gfork_i_msg_t *                     msg;

    msg = (gfork_i_msg_t *) user_arg;

    globus_mutex_lock(&gfork_l_mutex);
    {
        to_kid = msg->kid;
        to_kid->writting = GLOBUS_FALSE;

        if(result != GLOBUS_SUCCESS)
        {
            goto error_incoming;
        }

        gfork_l_write(to_kid);
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;
error_incoming:
    return;
}

static
void
gfork_l_write(
    gfork_i_child_handle_t *            to_kid)
{
    gfork_i_msg_t *                     msg;
    globus_result_t                     result;

    if(!to_kid->writting && !globus_fifo_empty(&to_kid->write_q))
    {
        msg = (gfork_i_msg_t *) globus_fifo_dequeue(&to_kid->write_q);

        msg->iov[0].iov_base = &msg->header;
        msg->iov[0].iov_len = sizeof(gfork_i_msg_header_t);
        msg->iov[1].iov_base = msg->data;
        msg->iov[1].iov_len = msg->header.size;
        result = globus_xio_register_writev(
            to_kid->write_xio_handle,
            msg->iov,
            2,
            msg->header.size + sizeof(gfork_i_msg_header_t),
            NULL,
            gfork_l_writev_cb,
            msg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_register;
        }
        to_kid->writting = GLOBUS_TRUE;
    }

    return;

error_register:

    return;    
}
    




/******************** main ****************************/
int
main(
    int                                 argc,
    char **                             argv)
{
    int                                 rc;
    globus_options_handle_t             opt_h;
    globus_result_t                     res;

    rc = globus_module_activate(GLOBUS_GFORK_PARENT_MODULE);
    if(rc != 0)
    {
        gfork_l_options.quiet = 0;
        gfork_log(1, "Activation error\n");
        goto error_act;
    }

    globus_hashtable_init(
        &gfork_l_pid_table,
        1024,
        globus_hashtable_int_hash,
        globus_hashtable_int_keyeq);
    globus_mutex_init(&gfork_l_mutex, NULL);
    globus_cond_init(&gfork_l_cond, NULL);

    globus_options_init(
        &opt_h, gfork_i_opts_unknown, &gfork_l_options);
    globus_options_add_table(opt_h, gfork_l_opts_table, &gfork_l_options);

    res = globus_options_command_line_process(opt_h, argc, argv);
    if(res != GLOBUS_SUCCESS)
    {
        gfork_log(1, "Bad command line options\n");
        goto error_opts;
    }
    gfork_log(1, "port %d\n", gfork_l_options.port);

    /* verify options */
    if(gfork_l_options.argv == NULL)
    {
        gfork_log(1, "You must specify a a program to run\n");
        goto error_opts;
    }

    globus_mutex_lock(&gfork_l_mutex);
    {
        res = globus_callback_register_signal_handler(
            SIGINT,
            GLOBUS_FALSE,
            gfork_l_int,
            &gfork_l_options);
        res = globus_callback_register_signal_handler(
            SIGCHLD,
            GLOBUS_FALSE,
            gfork_l_sigchld,
            &gfork_l_options);
        if(res != GLOBUS_SUCCESS)
        {
            gfork_log(1, "Failed to register signal handler\n");
            goto error_signal;
        }

        res = gfork_init_server();
        if(res != GLOBUS_SUCCESS)
        {
            gfork_log(1, "Failed to init server\n");
            goto error_server;
        }

        while(!gfork_l_done ||
            !globus_list_empty(gfork_l_pid_list))
        {
            globus_cond_wait(&gfork_l_cond, &gfork_l_mutex);
        }
    }
    globus_mutex_unlock(&gfork_l_mutex);

    gfork_log(1, "Server Done\n");
    return 0;
error_signal:
error_server:
error_opts:
error_act:

    gfork_log(1, "Error\n");
    return 1;
}
