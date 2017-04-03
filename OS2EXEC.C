/* os2exec.c */

/* Copyright (c) 1992-1993 by Eberhard Mattes */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef unsigned char byte;

static int fh;
static byte buf[4096+2];

#define MAKEP(seg,off) ((void far *)(((unsigned long)(seg)<<16)|(unsigned long)(off)))
#define WHITE(c) ((c)==' ' || (c)=='\t')

static void send (byte cmd, byte *src, int len);
int main (int argc, char *argv[], char *envp[]);

static void send (byte cmd, byte *src, int len)
{
  buf[0] = (byte)((len + 1) % 256);
  buf[1] = (byte)((len + 1) / 256);
  buf[2] = cmd;
  memmove (buf+3, src, len);
  if (write (fh, buf, len+3) != len+3)
    {
      fprintf (stderr, "Cannot write to pipe\n");
      exit (2);
    }
}


int main (int argc, char *argv[], char *envp[])
{
  byte far *unfmt, far *p;
  byte cmd_line[257];
  byte cwd[_MAX_PATH];
  byte header[2], cmd_char;
  int i;
  long n;

  unfmt = MAKEP (_psp, 0x80);
  unfmt[unfmt[0]+1] = 0;
  p = unfmt + 1;
  while (WHITE (*p)) ++p;
  cmd_char = 'C';
  if (p[0] == '-' && p[1] == 'q' && p[2] == 0)
    cmd_char = 'Q';
  else if (p[0] == '-' && p[1] == 'x' && WHITE (p[2]))
    {
      cmd_char = 'X';
      p += 3;
      while (WHITE (*p)) ++p;
    }
  _fstrcpy (cmd_line, p);
  fh = open ("/pipe/os2exec.em", O_RDWR | O_BINARY, S_IREAD | S_IWRITE);
  if (fh < 0)
    {
      fprintf (stderr, "os2execd not running\n");
      return (2);
    }
  if (cmd_char == 'Q')
    send ('Q', "", 0);
  else
    {
      getcwd (cwd, sizeof (cwd));
      send ('W', cwd, strlen (cwd));
      for (i = 0; envp[i] != NULL; ++i)
        if (memcmp (envp[i], "COMSPEC=", 8) != 0
            && memcmp (envp[i], "PROMPT=", 7) != 0
            && memcmp (envp[i], "PATH=", 5) != 0)
          send ('E', envp[i], strlen (envp[i]));
      send (cmd_char, cmd_line, strlen (cmd_line));
    }
  i = read (fh, header, 2);
  if (i < 0)
    {
      perror ("read1");
      return (2);
    }
  if (i != 2)
    {
      fprintf (stderr, "Return code not available --- invalid message size\n");
      return (2);
    }
  i = header[0] + 256 * header[1];
  if (read (fh, buf, i) != i)
    perror ("read2");
  if (cmd_char == 'Q')
    {
      if (buf[0] != 'A')
        {
          fprintf (stderr, "Quit not acknowledged\n");
          return (2);
        }
      n = 0;
    }
  else
    {
      if (buf[0] != 'R')
        {
          fprintf (stderr, "Return code not available --- "
                   "invalid message type\n");
          return (2);
        }
      buf[i] = 0;
      n = strtol (buf+1, NULL, 10);
      send ('A', " ", 1);
    }
  close (fh);
  return ((int)n);
}
