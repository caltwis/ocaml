/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

#include <mlvalues.h>
#include <memory.h>
#include "unixsupport.h"

extern char ** cstringvect();

CAMLprim value unix_execve_r(CAML_R, value path, value args, value env)
{
  char ** argv;
  char ** envp;
  argv = cstringvect(args);
  envp = cstringvect(env);
  (void) execve(String_val(path), argv, envp);
  stat_free((char *) argv);
  stat_free((char *) envp);
  uerror_r(ctx, "execve", path);
  return Val_unit;                  /* never reached, but suppress warnings */
                                /* from smart compilers */
}
