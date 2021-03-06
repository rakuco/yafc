/* makepath.c -- Ensure that a directory path exists.
   Copyright (C) 1990 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by David MacKenzie <djm@gnu.ai.mit.edu> and Jim Meyering.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if STAT_MACROS_BROKEN
# undef S_ISDIR
#endif

#if !defined(S_ISDIR) && defined(S_IFDIR)
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#if STDC_HEADERS
# include <stdlib.h>
#endif

#include <errno.h>

#ifdef STDC_HEADERS /* HAVE_STRING_H */
# include <string.h>
#else
# include <strings.h>
# ifndef strchr
#  define strchr index
# endif
#endif

#ifdef __MSDOS__
typedef int uid_t;
typedef int gid_t;
#endif

#include "makepath.h"

/* in ../src/ftp/ftp.c */
void ftp_err(const char *fmt, ...);

struct ptr_list
{
  char *dirname_end;
  struct ptr_list *next;
};

static void free_ptr_list(struct ptr_list* ptr)
{
  if (!ptr)
    return;

  struct ptr_list* next = ptr->next;
  free(ptr);
  free_ptr_list(next);
}


/* Ensure that the directory ARGPATH exists.
   Remove any trailing slashes from ARGPATH before calling this function.

   Create any leading directories that don't already exist, with
   permissions PARENT_MODE.
   If the last element of ARGPATH does not exist, create it as
   a new directory with permissions MODE.
   If OWNER and GROUP are non-negative, use them to set the UID and GID of
   any created directories.
   If VERBOSE_FMT_STRING is nonzero, use it as a printf format
   string for printing a message after successfully making a directory,
   with the name of the directory that was just made as an argument.
   If PRESERVE_EXISTING is non-zero and ARGPATH is an existing directory,
   then do not attempt to set its permissions and ownership.

   Return 0 if ARGPATH exists as a directory with the proper
   ownership and permissions when done, otherwise 1.  */

int
make_path (const char *argpath,
     int mode,
     int parent_mode,
     uid_t owner,
     gid_t group,
     int preserve_existing,
     const char *verbose_fmt_string)
{
  char *dirpath;    /* A copy we can scribble NULs on.  */
  struct stat stats;
  int retval = 0;
  int oldmask = umask (0);

  /* FIXME: move this xmalloc and strcpy into the if-block.
     Set dirpath to argpath in the else-block.  */
  dirpath = (char *) malloc (strlen (argpath) + 1);
  strcpy (dirpath, argpath);

  if (stat (dirpath, &stats))
  {
    char *slash;
    int tmp_mode;   /* Initial perms for leading dirs.  */
    int re_protect;   /* Should leading dirs be unwritable? */
    struct ptr_list *p, *leading_dirs = NULL;

      /* If leading directories shouldn't be writable or executable,
   or should have set[ug]id or sticky bits set and we are setting
   their owners, we need to fix their permissions after making them.  */
    if (((parent_mode & 0300) != 0300)
    || (owner != (uid_t) -1 && group != (gid_t) -1
        && (parent_mode & 07000) != 0))
    {
      tmp_mode = 0700;
      re_protect = 1;
    }
    else
    {
      tmp_mode = parent_mode;
      re_protect = 0;
    }

    slash = dirpath;
    while (*slash == '/')
      slash++;
    while ((slash = strchr (slash, '/')))
    {
      *slash = '\0';
      if (stat (dirpath, &stats))
      {
        if (mkdir (dirpath, tmp_mode))
        {
          ftp_err ("cannot create directory `%s': %s\n",
            dirpath, strerror(errno));
          umask (oldmask);
          free(dirpath);
          free_ptr_list(leading_dirs);
          return 1;
        }
        else
        {
          if (verbose_fmt_string != NULL)
            ftp_err (verbose_fmt_string, dirpath);

          if (owner != (uid_t) -1 && group != (gid_t) -1
            && chown (dirpath, owner, group)
#if defined(AFS) && defined (EPERM)
            && errno != EPERM
#endif
            )
          {
            ftp_err ("%s: %s\n", dirpath, strerror(errno));
            retval = 1;
          }
          if (re_protect)
          {
            struct ptr_list *new = (struct ptr_list *)
              malloc (sizeof (struct ptr_list));
              new->dirname_end = slash;
              new->next = leading_dirs;
              leading_dirs = new;
          }
        }
      }
      else if (!S_ISDIR (stats.st_mode))
      {
        ftp_err ("`%s' exists but is not a directory\n", dirpath);
        umask (oldmask);
        free(dirpath);
        free_ptr_list(leading_dirs);
        return 1;
      }

      *slash++ = '/';

    /* Avoid unnecessary calls to `stat' when given
       pathnames containing multiple adjacent slashes.  */
      while (*slash == '/')
        slash++;
    }

      /* We're done making leading directories.
   Create the final component of the path.  */

      /* The path could end in "/." or contain "/..", so test
   if we really have to create the directory.  */

    if (stat (dirpath, &stats) && mkdir (dirpath, mode))
    {
      ftp_err ("cannot create directory `%s': %s\n",
        dirpath, strerror(errno));
      umask (oldmask);
      free_ptr_list(leading_dirs);
      free(dirpath);
      return 1;
    }
    if (verbose_fmt_string != NULL)
      ftp_err ( verbose_fmt_string, dirpath);

    if (owner != (uid_t) -1 && group != (gid_t) -1)
    {
      if (chown (dirpath, owner, group)
#ifdef AFS
        && errno != EPERM
#endif
        )
      {
        ftp_err ("%s: %s\n", dirpath, strerror(errno));
        retval = 1;
      }
      /* chown may have turned off some permission bits we wanted.  */
      if ((mode & 07000) != 0 && chmod (dirpath, mode))
      {
        ftp_err ("%s: %s\n", dirpath, strerror(errno));
        retval = 1;
      }
    }

      /* If the mode for leading directories didn't include owner "wx"
   privileges, we have to reset their protections to the correct
   value.  */
    for (p = leading_dirs; p != NULL; p = p->next)
    {
      *(p->dirname_end) = '\0';
      if (chmod (dirpath, parent_mode))
      {
        ftp_err ("%s: %s\n", dirpath, strerror(errno));
        retval = 1;
      }
    }
    free_ptr_list(leading_dirs);
  }
  else
  {
    /* We get here if the entire path already exists.  */

    if (!S_ISDIR (stats.st_mode))
    {
      ftp_err ("`%s' exists but is not a directory\n", dirpath);
      umask (oldmask);
      free(dirpath);
      return 1;
    }

    if (!preserve_existing)
    {
    /* chown must precede chmod because on some systems,
       chown clears the set[ug]id bits for non-superusers,
       resulting in incorrect permissions.
       On System V, users can give away files with chown and then not
       be able to chmod them.  So don't give files away.  */

      if (owner != (uid_t) -1 && group != (gid_t) -1
        && chown (dirpath, owner, group)
#ifdef AFS
        && errno != EPERM
#endif
        )
      {
        ftp_err ("%s: %s\n", dirpath, strerror(errno));
        retval = 1;
      }
      if (chmod (dirpath, mode))
      {
        ftp_err ("%s: %s\n", dirpath, strerror(errno));
        retval = 1;
      }
    }
  }

  free(dirpath);
  umask (oldmask);
  return retval;
}
