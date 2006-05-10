/*
 * Portions of this file Copyright 1999-2005 University of Chicago
 * Portions of this file Copyright 1999-2005 The University of Southern California.
 *
 * This file or a portion of this file is licensed under the
 * terms of the Globus Toolkit Public License, found at
 * http://www.globus.org/toolkit/download/license.html.
 * If you redistribute this file, with or without
 * modifications, you must include this notice in the file.
 */

/******-*- C -*-**************************************************************
globus_common.h

Description:
  Headers common to all of Globus

CVS Information:

  $Source$
  $Date$
  $State$
  $Author$
******************************************************************************/

#if !defined(GLOBUS_INCLUDE_GLOBUS_COMMON_H)
#define GLOBUS_INCLUDE_GLOBUS_COMMON_H 1

#ifndef EXTERN_C_BEGIN
#    ifdef __cplusplus
#        define EXTERN_C_BEGIN extern "C" {
#        define EXTERN_C_END }
#    else
#        define EXTERN_C_BEGIN
#        define EXTERN_C_END
#    endif
#endif

EXTERN_C_BEGIN

/******************************************************************************
			       Type definitions
******************************************************************************/
/* ToDo: These definitions should be picked up from the icu4c include files
         but for some reason are not. Fix this!!
*/
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

/* No strncasecmp, strcasecmp in windows, but _strnicmp,stricmp are equivalent */
#define strncasecmp _strnicmp
#define strcasecmp _stricmp

/******************************************************************************
		      Include globus_common header files
******************************************************************************/
/* Common */
#include "globus_common_include.h"
#include "globus_module.h"
#include "globus_url.h"
#include "globus_list.h"
#include "globus_hashtable.h"
#include "globus_fifo.h"
#include "globus_symboltable.h"
#include "globus_object.h"
#include "globus_object_hierarchy.h"
#include "globus_error.h"
#include "globus_error_hierarchy.h"
#include GLOBUS_THREAD_INCLUDE
#include "globus_time.h"
#include "globus_thread_pool.h"
#include "globus_handle_table.h"
#include "globus_callback.h"
#include "globus_logging.h"
#include "globus_memory.h"
#include "globus_print.h"
#include "globus_tilde_expand.h"
#include "globus_libc.h"
#include "globus_priority_q.h"
#include "globus_range_list.h"
#include "globus_debug.h"
#include "globus_args.h"
#include "globus_strptime.h"
#include "globus_thread_common.h" 
#include "globus_thread_rw_mutex.h"
#include "globus_thread_rmutex.h"
#include "globus_error_errno.h"
#include "globus_error_generic.h"
#include "globus_extension.h"
#include "globus_uuid.h"
#include "globus_options.h"

# if !defined(alloca)
/* AIX requires this to be the first thing in the file.  */
#ifdef __GNUC__
# define alloca __builtin_alloca
#else
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
#     ifndef _CRAYT3E
char *alloca ();
#     endif
#   endif
#  endif
# endif
#endif
#endif

#if !defined(MAXPATHLEN) 
#   include <sys/param.h>
#   define MAXPATHLEN PATH_MAX
#endif
EXTERN_C_BEGIN

/* most network-related functions (getpeername, getsockname,...) have
   an int* as argument, except AIX which uses size_t*. This will
   masquerade the difference. */
#define globus_netlen_t int

/*
 * globus_barrier_t
 *
 * A generic barrier structure */
typedef struct globus_barrier_s
{
    globus_mutex_t      mutex;
    globus_cond_t       cond;
    int                 count;
} globus_barrier_t;

/* Windows replacement for unix getopt() */
int getopt(int argc, char * const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;


/******************************************************************************
			       Define constants
******************************************************************************/
 
/******************************************************************************
			  Module activation structure
******************************************************************************/
extern globus_module_descriptor_t	globus_i_common_module;

#define GLOBUS_COMMON_MODULE (&globus_i_common_module)

/******************************************************************************
		  		i18n 
******************************************************************************/

extern globus_extension_registry_t i18n_registry;
#define I18N_REGISTRY &i18n_registry

char *
globus_common_i18n_get_string_by_key(
    const char *                        locale,
    const char *                        resource_name,
    const char *                        key);

char *
globus_common_i18n_get_string(
    globus_module_descriptor_t *        module,
    const char *                        key);

/******************************************************************************
		   Install path discovery functions
******************************************************************************/

globus_result_t
globus_location (  char **   bufp );

/* returns value of GLOBUS_LOCATION in the deploy dir config file */
globus_result_t
globus_common_get_attribute_from_config_file( 
	char *                                         deploy_path,
    char *                                         file_location,
	char *                                         attribute,
	char **                                        value);

EXTERN_C_END

#endif /* GLOBUS_INCLUDE_GLOBUS_COMMON_H */
