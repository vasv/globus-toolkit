#include "globus_xio.h"
#include "globus_common.h"
#include "test_common.h"
#include "globus_xio_test_transport.h"

static globus_mutex_t                   globus_l_mutex;
static globus_cond_t                    globus_l_cond;
static globus_bool_t                    globus_l_closed = GLOBUS_FALSE;

#define USEC_THRESHHOLD  300000

static globus_bool_t
result_is_cancel(
    globus_result_t                             res)
{
    if(res == GLOBUS_SUCCESS ||
        (!globus_error_match(
            globus_error_peek(res),
            GLOBUS_XIO_MODULE,
            GLOBUS_XIO_ERROR_CANCELED)) &&
        !globus_error_match(
            globus_error_peek(res),
            GLOBUS_XIO_MODULE,
            GLOBUS_XIO_ERROR_TIMEDOUT))
    {
        return GLOBUS_FALSE;
    }

    return GLOBUS_TRUE;
}

static void
close_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    void *                                      user_arg)
{
    char *                                      timeout_type;

    timeout_type = (char *) user_arg;
    if(strcmp(timeout_type, "C") == 0)
    {
        if(!result_is_cancel(result))
        {
            failed_exit("Close was not canceled.");
        }
    }

    globus_mutex_lock(&globus_l_mutex);
    {
        globus_l_closed = GLOBUS_TRUE;
        globus_cond_signal(&globus_l_cond);
    }
    globus_mutex_unlock(&globus_l_mutex);
}

static void
data_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    globus_byte_t *                             buffer,
    globus_size_t                               len,
    globus_size_t                               nbytes,
    globus_xio_data_descriptor_t                data_desc,
    void *                                      user_arg)
{
    globus_result_t                             res;
    char *                                      timeout_type;

    timeout_type = (char *) user_arg;
    if(strcmp(timeout_type, "D") == 0)
    {
        if(!result_is_cancel(result))
        {
            failed_exit("Read/Write was not canceled.");
        }
    }

    globus_mutex_lock(&globus_l_mutex);
    {
        res = globus_xio_register_close(
                handle,
                NULL,
                close_cb,
                user_arg);
        test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
        if(strcmp(timeout_type, "C") == 0)
        {
            res = globus_xio_handle_cancel_operations(
                    handle,
                    GLOBUS_XIO_CANCEL_CLOSE);
            test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
        }
    }
    globus_mutex_unlock(&globus_l_mutex);
}

static void
open_cb(
    globus_xio_handle_t                         handle,
    globus_result_t                             result,
    void *                                      user_arg)
{
    globus_result_t                             res;
    globus_byte_t *                             buffer;
    globus_size_t                               buffer_length;
    char *                                      timeout_type;
    int                                         mask = 0;

    buffer = globus_l_test_info.buffer;
    buffer_length = globus_l_test_info.buffer_length;

    timeout_type = (char *) user_arg;
    if(strcmp(timeout_type, "O") == 0)
    {
        if(!result_is_cancel(result))
        {
            failed_exit("Open was not canceled.");
        }
        else
        {
            globus_mutex_lock(&globus_l_mutex);
            {
                globus_l_closed = GLOBUS_TRUE;
                globus_cond_signal(&globus_l_cond);
            }
            globus_mutex_unlock(&globus_l_mutex);
        }
    }
    else
    {
        if(globus_l_test_info.write_count > 0)
        {
            res = globus_xio_register_write(
                    handle,
                    buffer,
                    buffer_length,
                    buffer_length,
                    NULL,
                    data_cb,
                    user_arg);
            mask = GLOBUS_XIO_CANCEL_WRITE;
        }
        else
        {
            res = globus_xio_register_read(
                    handle,
                    buffer,
                    buffer_length,
                    buffer_length,
                    NULL,
                    data_cb,
                    user_arg);
            mask = GLOBUS_XIO_CANCEL_READ;
        }
        test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
        if(strcmp(timeout_type, "D") == 0)
        {
            res = globus_xio_handle_cancel_operations(
                    handle,
                    mask);
            test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);
        }
    }
}

int
cancel_main(
    int                                     argc,
    char **                                 argv)
{
    int                                     rc;
    globus_xio_stack_t                      stack;
    globus_xio_handle_t                     handle;
    globus_xio_target_t                     target;
    globus_result_t                         res;
    globus_xio_attr_t                       attr;
    int                                     opt_offset;
    int                                     secs;
    int                                     usecs;

    globus_l_closed = GLOBUS_FALSE;

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    globus_assert(rc == 0);

    res = globus_xio_attr_init(&attr);
    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

    res = globus_xio_stack_init(&stack, NULL);
    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

    opt_offset = parse_parameters(argc, argv, stack, attr);
    if(opt_offset == argc)
    {
        fprintf(stderr, "ERROR: please specify O | D | C.\n");
        return 1;
    }

    GlobusTimeReltimeGet(globus_l_test_info.delay, secs, usecs);

    if(secs == 0 && usecs < USEC_THRESHHOLD)
    {
        fprintf(stderr, "ERROR: delay time must be at least %d usecs.\n",
            USEC_THRESHHOLD);
        return 1;
    }

    globus_mutex_init(&globus_l_mutex, NULL);
    globus_cond_init(&globus_l_cond, NULL);

    res = globus_xio_target_init(&target, NULL, "whatever", stack);
    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

    res = globus_xio_register_open(
            &handle,
            attr,
            target,
            open_cb,
            argv[opt_offset]);
    test_res(GLOBUS_XIO_TEST_FAIL_NONE, res, __LINE__, __FILE__);

    if(strcmp(argv[opt_offset], "O") == 0)
    {
        res = globus_xio_handle_cancel_operations(
                handle,
                GLOBUS_XIO_CANCEL_OPEN);
    }

    globus_mutex_lock(&globus_l_mutex);
    {
        while(!globus_l_closed)
        {
            globus_cond_wait(&globus_l_cond, &globus_l_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_mutex);

    globus_xio_attr_destroy(attr);
    globus_xio_stack_destroy(stack);
 
    test_common_end();

    rc = globus_module_deactivate(GLOBUS_XIO_MODULE);
    globus_assert(rc == 0);

    fprintf(stdout, "Success.\n");

    return 0;
}
