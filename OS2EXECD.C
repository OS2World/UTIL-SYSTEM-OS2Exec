/* os2execd.c */

/* Written by Eberhard Mattes */

/* Copyright (c) 1992-1993 by Eberhard Mattes */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <process.h>
#define INCL_DOSMISC
#define INCL_DOSNMPIPES
#define INCL_DOSPROCESS
#define INCL_WINWINDOWMGR
#define INCL_WINSWITCHLIST
#include <os2.h>

#define FALSE 0
#define TRUE  1

typedef unsigned char byte;

static byte buf[4096];
static CHAR args[4096+256];
static CHAR prog_buf[256];
static PCHAR org_env;
static PCHAR new_env;
static HPIPE hp;

static USHORT org_env_size;
static USHORT new_env_size;
static USHORT new_env_alloc;

static void send (byte cmd, byte *src, int len);
static void out_of_mem (void);
int main (int argc, char *argv[]);


static void out_of_mem (void)
{
  fprintf (stderr, "Out of memory\n");
  exit (2);
}


static void send (byte cmd, byte *src, int len)
{
  USHORT rc, cb;

  buf[0] = (byte)((len + 1) % 256);
  buf[1] = (byte)((len + 1) / 256);
  buf[2] = cmd;
  memmove (buf+3, src, len);
  rc = DosWrite (hp, buf, len+3, &cb);
  if (rc != 0)
    {
      fprintf (stderr, "DosWrite: rc=%hu\n", rc);
      exit (2);
    }
}


int main (int argc, char *argv[])
{
  USHORT rc, cb;
  byte header[2], result[12], *p, *q;
  int sys_rc, drive, i, len, verbose, sw, quit;
  CHAR failname[256], *shell, *prog;
  RESULTCODES results;
  SEL env_sel;
  USHORT cmd_off;
  HWND hwndActive;

  puts ("os2execd -- Version 1.0a -- Copyright (c) 1992-1993 by Eberhard Mattes");
  verbose = FALSE; sw = FALSE; quit = FALSE;
  for (i = 1; i < argc; ++i)
    if (strcmp (argv[i], "-v") == 0)
      verbose = TRUE;
    else if (strcmp (argv[i], "-s") == 0)
      sw = TRUE;
    else
      {
        fprintf (stderr, "Usage: os2execd [-s] [-v]\n\n");
        fprintf (stderr, "  -s  Switch windows\n");
        fprintf (stderr, "  -v  Verbose\n");
        return (2);
      }
  shell = getenv ("COMSPEC");
  if (shell == NULL)
    shell = "cmd.exe";
  if (DosGetEnv (&env_sel, &cmd_off) != 0)
    {
      fprintf (stderr, "DosGetEnv failed\n");
      return (2);
    }
  org_env = MAKEP (env_sel, 0);
  org_env_size = 0;
  while (org_env[org_env_size++] != 0)
    while (org_env[org_env_size++] != 0)
      ;
  new_env_size = new_env_alloc = org_env_size;
  new_env = malloc (new_env_alloc);
  if (new_env == NULL) out_of_mem ();
  memmove (new_env, org_env, org_env_size);
  rc = DosMakeNmPipe ("/pipe/os2exec.em", &hp,
                      NP_ACCESS_DUPLEX | NP_NOINHERIT,
                      NP_WAIT | NP_READMODE_BYTE | NP_TYPE_BYTE
                      | NP_UNLIMITED_INSTANCES,
                      4096, 4096, 0L);
  if (rc != 0)
    {
      fprintf (stderr, "DosMakeNmPipe: rc=%hu\n", rc);
      return (2);
    }
 new:
  rc = DosConnectNmPipe (hp);
  if (rc != 0)
    {
      fprintf (stderr, "DosConnectNmPipe: rc=%hu\n", rc);
      return (2);
    }
  if (verbose)
    fprintf (stderr, "--- Connected\n");
  for (;;)
    {
      rc = DosRead (hp, header, sizeof (header), &cb);
      if (rc != 0)
        {
          fprintf (stderr, "DosRead: rc=%hu\n", rc);
          return (2);
        }
      if (cb != 2)
        break;
      rc = DosRead (hp, buf, header[0] + 256 * header[1], &cb);
      if (rc != 0)
        {
          fprintf (stderr, "DosRead: rc=%hu\n", rc);
          return (2);
        }
      if (cb == 0)
        break;
      buf[cb] = 0;
      if (verbose)
        {
          fprintf (stderr, "--- Message: <");
          fwrite (buf, cb, 1, stderr);
          fprintf (stderr, ">\n");
        }
      switch (buf[0])
        {
        case 'A':       /* Return code acknowledgement */
          if (verbose)
            fprintf (stderr, "--- Return code acknowledged\n");
          goto done;
        case 'C':       /* Command: CMD.EXE */
        case 'X':       /* Command: DosExecPgm */
          if (verbose)
            fprintf (stderr, "--- Command: %s\n", buf+1);
          if (sw)
            {
              hwndActive = WinQueryActiveWindow (HWND_DESKTOP, FALSE);
              WinSwitchToProgram (WinQuerySwitchHandle (0, getpid ()));
            }
          if (buf[0] == 'C')
            {
              prog = shell;
              p = args;
              strcpy (p, shell); p = strchr (p, 0) + 1;
              strcpy (p, "/c ");
              strcat (p, buf+1); p = strchr (p, 0) + 1;
              *p = 0;
            }
          else
            {
              q = buf + 1;
              while (*q == ' ' || *q == '\t')
                ++q;
              for (i = 0; i < sizeof (prog_buf) - 5; ++i)
                {
                  if (*q == 0 || *q == ' ' || *q == '\t')
                    break;
                  prog_buf[i] = *q;
                  ++q;
                }
              prog_buf[i] = 0;
              prog = prog_buf;
              p = args;
              strcpy (p, prog); p = strchr (p, 0) + 1;
              strcpy (p, q); p = strchr (p, 0) + 1;
              *p = 0;
              i = FALSE;
              for (q = prog_buf; *q != 0; ++q)
                if (*q == ':' || *q == '\\' || *q == ':')
                  i = FALSE;
                else if (*q == '.')
                  i = TRUE;
              if (!i)
                strcat (prog_buf, ".exe");
            }
          if (verbose)
            fprintf (stderr, "--- Program: %s\n", prog);
          rc = DosExecPgm (failname, sizeof (failname),
                           EXEC_SYNC,
                           args, new_env, &results, prog);
          if (rc != 0)
            {
              fprintf (stderr, "--- DosExecPgm failed, rc=%hu\n", rc);
              sys_rc = -1;
            }
          else
            sys_rc = results.codeResult;
          if (sw && hwndActive != NULL)
            WinSwitchToProgram (WinQuerySwitchHandle (hwndActive, 0));
          sprintf (result, "%d", sys_rc);
          send ('R', result, strlen (result));
          fputchar ('\n');
          memmove (new_env, org_env, org_env_size);
          new_env_size = org_env_size;
          break;
        case 'E':
          p = strchr (buf+1, '=');
          if (p != NULL)
            {
              len = (p - (buf+1)) + 1;
              i = 0;
              while (new_env[i] != 0)
                if (memcmp (new_env+i, buf+1, len) == 0)
                  break;
                else
                  i += strlen (new_env+i) + 1;
              if (new_env[i] != 0)
                {
                  len = strlen (new_env+i) + 1;
                  memmove (new_env + i, new_env + i + len,
                           new_env_size - (i + len));
                  new_env_size -= len;
                }
              len = strlen (buf+1) + 1;
              if (new_env_size + len > new_env_alloc)
                {
                  new_env_alloc = new_env_size + len;
                  new_env = realloc (new_env, new_env_alloc);
                  if (new_env == NULL) out_of_mem ();
                }
              memmove (new_env + new_env_size - 1, buf + 1, len);
              new_env_size += len;
              new_env[new_env_size-1] = 0;
            }
          break;
        case 'Q':       /* Quit */
          if (verbose)
            fprintf (stderr, "--- Quit\n");
          send ('A', "", 0);
          quit = TRUE;
          goto done;
        case 'W':       /* Working directory */
          p = buf + 1;
          drive = toupper (p[0]);
          if (drive >= 'A' && drive <= 'Z' && p[1] == ':')
            {
              if (verbose)
                fprintf (stderr, "--- Change drive: %c\n", drive);
              _chdrive (drive - 'A' + 1);
              p += 2;
            }
          if (verbose)
            fprintf (stderr, "--- Change directory: %s\n", p);
          chdir (p);
          break;
        }
    }
done:
  if (verbose)
    fprintf (stderr, "--- Disconnect\n");
  rc = DosDisConnectNmPipe (hp);
  if (rc != 0)
    {
      fprintf (stderr, "DosDisConnectNmPipe: rc=%hu\n", rc);
      return (2);
    }
  if (!quit)
    goto new;
  
  rc = DosClose (hp);
  if (rc != 0)
    {
      fprintf (stderr, "DosClose: rc=%hu\n", rc);
      return (2);
    }
  return (0);
}
