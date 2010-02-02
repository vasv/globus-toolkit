/*
 * Copyright 1999-2006 University of Chicago
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_url_sync.h URL Synchronize.
 *
 */
#endif

#ifndef _GLOBUS_URL_SYNC_H
#define	_GLOBUS_URL_SYNC_H

/**
 * @anchor globus_url_sync_api
 * @mainpage Globus URL Synchronize API
 *
 * The Globus URL Synchronize library provides a functions for synchronizing two
 * directories. The functions produce a list of <source, destination> pairs
 * that can be supplied to a transfer tool such as globus-url-copy or rft to
 * perform the actual data copy.
 *
 * @htmlonly
 * <a href="main.html" target="_top">View documentation without frames</a><br>
 * <a href="index.html" target="_top">View documentation with frames</a><br>
 * @endhtmlonly
 */

#include "globus_ftp_client.h"
#include "globus_common_include.h"

#ifndef EXTERN_C_BEGIN
#   ifdef  __cplusplus
#       define EXTERN_C_BEGIN extern "C" {
#       define EXTERN_C_END }
#   else
#       define EXTERN_C_BEGIN
#       define EXTERN_C_END
#   endif
#endif

/**
 * Error types
 */
typedef enum
{
    GLOBUS_URL_SYNC_ERROR_PARAMETER,
    GLOBUS_URL_SYNC_ERROR_MEMORY,
    GLOBUS_URL_SYNC_ERROR_ABORTED,
    GLOBUS_URL_SYNC_ERROR_INTERNAL,
    GLOBUS_URL_SYNC_ERROR_IN_USE,
    GLOBUS_URL_SYNC_ERROR_NOT_IN_USE,
    GLOBUS_URL_SYNC_ERROR_COMPLETED,
    GLOBUS_URL_SYNC_ERROR_REMOTE,
    GLOBUS_URL_SYNC_ERROR_PROTOCOL
} globus_url_sync_error_t;

/**
 * Maximum length supported for a directory entry.
 */
#ifndef GLOBUS_URL_SYNC_DIR_ENTRY_LENGTH_MAX
#   define GLOBUS_URL_SYNC_DIR_ENTRY_LENGTH_MAX     1024
#endif

/**
 * @defgroup globus_url_sync_activation Activation
 *
 * The Globus URL Synchronize library uses the Globus module activation
 * and deactivation API to initialize its state. Activate the module before
 * using any of its functions.
 *
 * @code
 *    globus_module_activate(GLOBUS_URL_SYNC_MODULE);
 * @endcode
 *
 * This function returns GLOBUS_SUCCESS if the FTP library was
 * successfully initialized. This may be called multiple times.
 *
 * When the module is no longer needed, use the following call to deactive it.
 *
 * @code
 *    globus_module_deactivate(GLOBUS_URL_SYNC_MODULE);
 * @endcode
 */

/**
 * Module descriptor
 * @ingroup globus_url_sync_activation
 * @hideinitializer
 */
#define GLOBUS_URL_SYNC_MODULE (&globus_i_url_sync_module)

extern globus_module_descriptor_t globus_i_url_sync_module;

/**
 * @defgroup globus_url_sync_debug Debug
 *
 * The URL Synchronize library supports variable log levels. The log level
 * may be specified during module activation by an environment variable. The
 * least verbose log level is 0.
 *
 * @code
 *    $ export GLOBUS_URL_SYNC_DEBUG_LEVEL=[0..9]
 * @endcode
 *
 * Alternatively, after module activation the debug level may be changed by
 * function call.
 *
 * @code
 *    globus_url_sync_set_loglevel(GLOBUS_URL_SYNC_DEBUG_LEVEL_WARN);
 * @endcode
 *
 * By default the module logs to stderr. The log may be redirected by setting
 * a new log handle.
 *
 * @code
 *    globus_url_sync_set_loghandle(a_syslog_handle);
 * @endcode
 *
 * @see  globus_url_sync_set_loglevel, globus_url_sync_set_loghandle
 */
#define GLOBUS_URL_SYNC_LOGLEVEL "GLOBUS_URL_SYNC_LOGLEVEL"

/**
 * Log level type.
 * @ingroup globus_url_sync_debug
 */
typedef enum
{
    GLOBUS_URL_SYNC_LOG_LEVEL_NONE = 0,
    GLOBUS_URL_SYNC_LOG_LEVEL_CRITICAL,
    GLOBUS_URL_SYNC_LOG_LEVEL_ERROR,
    GLOBUS_URL_SYNC_LOG_LEVEL_WARN,
    GLOBUS_URL_SYNC_LOG_LEVEL_INFO,
    GLOBUS_URL_SYNC_LOG_LEVEL_VERBOSE,
    GLOBUS_URL_SYNC_LOG_LEVEL_DEBUG
} globus_url_sync_log_level_t;

/**
 * URL Sync Handle.
 * @ingroup globus_url_sync_handle
 *
 * An URL sync handle is used to associate the state of the synchronization
 * operations. The @link globus_url_sync_operations operations @endlink take a
 * handle pointer as a parameter.
 *
 * @see globus_url_sync_handle_init(), globus_url_sync_handle_destroy()
 */
typedef struct globus_l_url_sync_handle_s * globus_url_sync_handle_t;

/**
 * File type.
 * @ingroup globus_url_sync_handle
 */
typedef enum
{
    globus_url_sync_endpoint_type_unknown = 0,
    globus_url_sync_endpoint_type_file,
    globus_url_sync_endpoint_type_dir,
} globus_url_sync_endpoint_type_t;

/**
 * Endpoint statistics. File oriented statistics for the endpoint.
 *
 * @ingroup globus_url_sync_handle
 */
typedef struct globus_url_sync_endpoint_stats_s
{
    globus_bool_t                           exists;
    globus_url_sync_endpoint_type_t         type;
    unsigned long                           size;
    struct tm                               modify_tm;
} globus_url_sync_endpoint_stats_t;

/**
 * Endpoint descriptor. Used to describe the source or the destination of a
 * synchronize operation.
 *
 * @ingroup globus_url_sync_handle
 */
typedef struct globus_url_sync_endpoint_s
{
    char *                                  url;
    globus_ftp_client_handle_t *            ftp_handle;
    globus_url_sync_endpoint_stats_t        stats;
} globus_url_sync_endpoint_t;

/**
 * @defgroup globus_url_sync_comparators Synchronize Comparators
 *
 * Synchronization operations depend on file status comparators to determine
 * the synchronization state of the source and destination files.
 */

/**
 * Comparison result type.
 * @ingroup globus_url_sync_comparators
 */
typedef enum
{
    GLOBUS_URL_SYNC_COMPARISON_LEFT_OUT_OF_SYNC     = -1,
    GLOBUS_URL_SYNC_COMPARISON_SYNCHRONIZED         = 0,
    GLOBUS_URL_SYNC_COMPARISON_RIGHT_OUT_OF_SYNC    = 1,
} globus_url_sync_comparison_result_t;

/**
 * Callback for the synchronization comparison function. This function is called
 * by the comparator module compare function and gives the result of the
 * comparison.
 *
 * @ingroup globus_url_sync_comparators
 *
 * @param arg
 *        The user callback argument.
 * @param source
 *        The source endpoint
 * @param destination
 *        The destination endpoint
 * @param result
 *        The comparison result.
 * @param error
 *        An error object if the operation failed or NULL if the operation
 *        performed successfully.
 */
typedef void
(*globus_url_sync_compare_func_cb_t) (
    void *                                      arg,
    globus_url_sync_endpoint_t *                source,
    globus_url_sync_endpoint_t *                destination,
    globus_url_sync_comparison_result_t         result,
    globus_object_t *                           error);

/**
 * Performs a file synchronization comparison.
 *
 * @ingroup globus_url_sync_comparators
 *
 * @param comparator_arg
 *        An argument for the comparator, usually containing internal state
 *        used to facilitate the comparison
 * @param source
 *        The source endpoint
 * @param destination
 *        The destination endpoint
 * @param callback_func
 *        The callback function which will be called by the compare function in
 *        order to return the results of the comparison.
 * @param callback_arg
 *        The user callback argument.
 * @retval GLOBUS_SUCCESS
 *        The operation has started successfully. If the compare function does
 *        not return GLOBUS_SUCCESS, the function must set an error object
 *        describing the error. If the compare function returns GLOBUS_SUCCESS,
 *        then the compare_result will be set.
 */
typedef globus_result_t
(*globus_url_sync_compare_func_t) (
    void *                                      comparator_arg,
    globus_url_sync_endpoint_t *                source,
    globus_url_sync_endpoint_t *                destination,
    globus_url_sync_compare_func_cb_t           callback_func,
    void *                                      callback_arg);

/**
 * The synchronization comparator. Synchronization functions use the comparator
 * to determine the synchronization status between files and directories. The
 * structure includes a user argument (comparator_arg) that the calling
 * function passes to the comparator functions (e.g., compare_func).
 *
 * @ingroup globus_url_sync_comparators
 */
typedef struct globus_url_sync_comparator_s
{
    void *                                      comparator_arg;
    globus_url_sync_compare_func_t              compare_func;
} globus_url_sync_comparator_t;

/**
 * Operation result callback.
 * @ingroup globus_url_sync_operations
 *
 * The synchronization operation calls this callback function to return the
 * results for each URL source and destination compared.
 *
 * @param user_arg
 *        The user_arg parameter passed to the operation.
 * @param handle
 *        The handle on which the operation was done.
 * @param error
 *        A Globus error object indicating any problem which occurred,
 *        or GLOBUS_SUCCESS, if the operation completed successfully.
 * @param source
 *        The source endpoint
 * @param destination
 *        The destination endpoint
 * @param result
 *        The comparison result.
 */
typedef void (*globus_url_sync_result_callback_t) (
    void *					user_arg,
    globus_url_sync_handle_t                    handle,
    globus_object_t *				error,
    globus_url_sync_endpoint_t *                source,
    globus_url_sync_endpoint_t *                destination,
    globus_url_sync_comparison_result_t         result);

/**
 * Operation complete callback.
 * @ingroup globus_url_sync_operations
 *
 * The URL sync operation is
 * asynchronous. A callback of this type is passed to each of the
 * operation function calls to let the user know when the operation is
 * complete.  The completion callback is called only once per
 * operation.
 *
 * @param user_arg
 *        The user_arg parameter passed to the operation.
 * @param handle
 *        The handle on which the operation was done.
 * @param error
 *        A Globus error object indicating any problem which occurred,
 *        or GLOBUS_SUCCESS, if the operation completed successfully.
 */
typedef void (*globus_url_sync_complete_callback_t) (
    void *					user_arg,
    globus_url_sync_handle_t                    handle,
    globus_object_t *				error);

/**
 * Sets the log level for the module.
 * @ingroup globus_url_sync_debug
 *
 * @param level
 *        The log level for the module.
 * @see globus_url_sync_loglevel_t
 */
void
globus_url_sync_log_set_level(
    globus_url_sync_log_level_t             level);

/**
 * Sets the log handle for the module.
 * @ingroup globus_url_sync_debug
 *
 * @param log_handle
 *        The log handle for the module.
 */
void
globus_url_sync_log_set_handle(
    globus_logging_handle_t                 log_handle);

/**
 * @defgroup globus_url_sync_handle Handle Management
 *
 * Create and Destroy an URL sync handle.
 *
 * Handles are used by URL sync operations to maintain state between calls and
 * while operations are in-progress. Currently, only one URL sync operation
 * may be in progress at a time per URL sync handle.
 *
 * This section defines operations to create and destroy handles.
 */

/**
 * Initializes the globus_url_sync Handle.
 *
 * @ingroup globus_url_sync_handle
 *
 * @param   handle
 *          Pointer to an uninitialized handle.
 * @param   comparator
 *          Pointer to a comparator. The comparator will be used for all file
 *          comparisons for all operations performed with the returned handle.
 * @retval  GLOBUS_SUCCESS
 *          The handle is initialized and ready for use.
 */
globus_result_t
globus_url_sync_handle_init(
    globus_url_sync_handle_t *              handle,
    globus_url_sync_comparator_t *          comparator);

/**
 * Destroys the globus_url_sync Handle.
 *
 * @ingroup globus_url_sync_handle
 *
 * @param   handle
 *          Pointer to an initialized handle.
 * @retval  GLOBUS_SUCCESS
 *          The handle has been deactivated and destroyed.
 */
globus_result_t
globus_url_sync_handle_destroy(
    globus_url_sync_handle_t *              handle);

/**
 * @defgroup globus_url_sync_operations Synchronize Operations
 *
 * Synchronization operations are the primary operations of this module.
 */

/**
 * Finds files and directories that require Synchronize based on the input
 * source and destination URLs.
 * @ingroup globus_url_sync_operations
 *
 * @param handle
 *        Handle for the synchronize operation
 * @param source_url
 *        Source URL
 * @param destination_url
 *        Destination URL
 * @param complete_callback
 *        User callback when operation complete
 * @param result_callback
 *        User callback for each sychronization comparison operation
 * @param callback_arg
 *        User argument for callbacks
 * @retval GLOBUS_SUCCESS
 *         The operation has started successfully.
 */
globus_result_t
globus_url_sync(
    globus_url_sync_handle_t                handle,
    globus_url_t *                          source_url,
    globus_url_t *                          destination_url,
    globus_url_sync_complete_callback_t     complete_callback,
    globus_url_sync_result_callback_t       result_callback,
    void *                                  callback_arg);

/**
 * Comparator for existence checks.
 * @ingroup globus_url_sync_comparators
 */
extern globus_url_sync_comparator_t globus_url_sync_comparator_exists;

EXTERN_C_END

#endif	/* _GLOBUS_URL_SYNC_H */