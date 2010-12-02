/* A program to put stress on a POSIX system (sccstress).
 *
 * Copyright (C) 2010 
 * Jack Peirce <jpeirce@sourcecode.com>
 *
 * Copyright (C) 2001,2002,2003,2004,2005,2006,2007,2008,2009,2010
 * Amos Waterland <apw@rossby.metr.ou.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>

/* By default, print all messages of severity info and above.  */
static int global_debug = 2;

/* Name of this program */
static char *global_progname = PACKAGE;

/* Implemention of runtime-selectable severity message printing.  */
#define dbg(OUT, STR, ARGS...) if (global_debug >= 3) \
	fprintf (stdout, "%s: dbug: [%lli] ", \
		global_progname, (long long)getpid()), \
		fprintf(OUT, STR, ##ARGS), fflush(OUT)
#define out(OUT, STR, ARGS...) if (global_debug >= 2) \
	fprintf (stdout, "%s: info: [%lli] ", \
		global_progname, (long long)getpid()), \
		fprintf(OUT, STR, ##ARGS), fflush(OUT)
#define wrn(OUT, STR, ARGS...) if (global_debug >= 1) \
	fprintf (stderr, "%s: WARN: [%lli] (%d) ", \
		global_progname, (long long)getpid(), __LINE__), \
		fprintf(OUT, STR, ##ARGS), fflush(OUT)
#define err(OUT, STR, ARGS...) if (global_debug >= 0) \
	fprintf (stderr, "%s: FAIL: [%lli] (%d) ", \
		global_progname, (long long)getpid(), __LINE__), \
		fprintf(OUT, STR, ##ARGS), fflush(OUT)

/* Implementation of check for option argument correctness.  */
#define assert_arg(A) \
          if (++i == argc || ((arg = argv[i])[0] == '-' && \
              !isdigit ((int)arg[1]) )) \
            { \
              err (stderr, "missing argument to option '%s'\n", A); \
              exit (1); \
            }

#define BUFSIZE 80
#define MAX 100
char line[BUFSIZE], fullline[BUFSIZE];
/* Prototypes for utility functions.  */
int usage (int status);
int version (int status);
long long atoll_s (const char *nptr);
long long atoll_b (const char *nptr);
long long do_hdd_target = 0;

/* Prototypes for worker functions.  */
int hogcpu (int cpu_pipe[], long long timeout);
int hogio (int io_pipe[], long long timeout);
int hogvm (long long bytes, long long stride, long long hang, int keep, int malloc_pipe[], long long timeout);
int hoghdd (long long bytes, int hdd_pipe[], long long timeout, char *target, int targetnum);

int alarmed = 0;
void sigh(int signum) {
  alarmed = 1;
}

int
main (int argc, char **argv)
{
  struct sysinfo sys_info;
  if(sysinfo(&sys_info) != 0)
    perror("sysinfo");
  int i, z, pid, children = 0, retval = 0;
  long starttime, stoptime, runtime, forks;
  long long totalcpu = 0, currentcpu = 0, cputest = 0;
  long long totalio = 0, currentio = 0, iotest = 0;
  long long totalmalloc = 0, currentmalloc = 0, malloctest = 0;
  long long totalhdd = 0, currenthdd = 0, hddtest = 0;
  char *hddresult, *hdddelims = ",";
  long long hdd_results[MAX] = { 0 }, resulttarget, hddint;
  long long x = 0, cpupersec = 0, cpupercore = 0, bytespersec = 0, allocatedbytes = 0;
  long long writtenbytes = 0, writepersec = 0, iopersec = 0;
  int cpu_pipe[2], io_pipe[2], malloc_pipe[2], hdd_pipe[2];
  int numprocs = get_nprocs(); 
  long long gbram = ((sys_info.totalram / 1000) / 1000) / 1024; //use mixed math to get right result
  out (stdout, "%i total processors\n", numprocs);
  out (stdout, "%iGB of RAM\n", gbram);
  /* Variables that indicate which options have been selected.  */
  int do_dryrun = 0;
  long long do_backoff = 3000;
  long long do_timeout = 0;
  long long do_cpu = 0;
  long long do_io = 0;
  long long do_vm = 0;
  unsigned long long do_vm_bytes = 256 * 1024 * 1024;
  long long do_vm_stride = 4096;
  long long do_vm_hang = -1;
  int do_vm_keep = 0;
  long long do_hdd = 0;
  long long do_hdd_bytes = 1024 * 1024 * 1024;
  long long hdd_targets = 1;
  char *targetlist[24];

  /* Record our start time.  */
  if ((starttime = time (NULL)) == -1)
    {
      err (stderr, "failed to acquire current time: %s\n", strerror (errno));
      exit (1);
    }

  /* SuSv3 does not define any error conditions for this function.  */
  global_progname = basename (argv[0]);

  /* For portability, parse command line options without getopt_long.  */
  for (i = 1; i < argc; i++)
    {
      char *arg = argv[i];

      if (strcmp (arg, "--help") == 0 || strcmp (arg, "-?") == 0)
        {
          usage (0);
        }
      else if (strcmp (arg, "--version") == 0)
        {
          version (0);
        }
      else if (strcmp (arg, "--verbose") == 0 || strcmp (arg, "-v") == 0)
        {
          global_debug = 3;
        }
      else if (strcmp (arg, "--quiet") == 0 || strcmp (arg, "-q") == 0)
        {
          global_debug = 0;
        }
      else if (strcmp (arg, "--dry-run") == 0 || strcmp (arg, "-n") == 0)
        {
          do_dryrun = 1;
        }
      else if (strcmp (arg, "--backoff") == 0)
        {
          assert_arg ("--backoff");
          if (sscanf (arg, "%lli", &do_backoff) != 1)
            {
              err (stderr, "invalid number: %s\n", arg);
              exit (1);
            }
          if (do_backoff < 0)
            {
              err (stderr, "invalid backoff factor: %lli\n", do_backoff);
              exit (1);
            }
          dbg (stdout, "setting backoff coeffient to %llius\n", do_backoff);
        }
      else if (strcmp (arg, "--timeout") == 0 || strcmp (arg, "-t") == 0)
        {
          assert_arg ("--timeout");
          do_timeout = atoll_s (arg);
          if (do_timeout <= 0)
            {
              err (stderr, "invalid timeout value: %llis\n", do_timeout);
              exit (1);
            }
        }
      else if (strcmp (arg, "--cpu") == 0 || strcmp (arg, "-c") == 0)
        {
          assert_arg ("--cpu");
          if (strcmp(arg, "a") == 0) {
            do_cpu = numprocs;
	  }
          else {
            do_cpu = atoll_b (arg);
            if (do_cpu <= 0)
              {
                err (stderr, "invalid number of cpu hogs: %lli\n", do_cpu);
                exit (1);
              }
          }
        }
      else if (strcmp (arg, "--io") == 0 || strcmp (arg, "-i") == 0)
        {
          assert_arg ("--io");
          do_io = atoll_b (arg);
          if (do_io <= 0)
            {
              err (stderr, "invalid number of io hogs: %lli\n", do_io);
              exit (1);
            }
        }
      else if (strcmp (arg, "--vm") == 0 || strcmp (arg, "-m") == 0)
        {
          assert_arg ("--vm");
	  if (strcmp(arg, "a") == 0) {
            do_vm = (gbram / 2) - 1; 
	    do_vm_bytes = 2048ul * 1024ul * 1024ul;
            if (do_vm == 0) {
		do_vm = 1;
		do_vm_bytes = 1024ul * 1024ul * 1024ul;
 	    }
	  }
	  else {
            do_vm = atoll_b (arg);
            if (do_vm <= 0)
              {
                err (stderr, "invalid number of vm hogs: %lli\n", do_vm);
                exit (1);
              }
          }
 	}
      else if (strcmp (arg, "--vm-bytes") == 0)
        {
          assert_arg ("--vm-bytes");
          do_vm_bytes = atoll_b (arg);
          if (do_vm_bytes <= 0)
            {
              err (stderr, "invalid vm byte value: %lli\n", do_vm_bytes);
              exit (1);
            }
        }
      else if (strcmp (arg, "--vm-stride") == 0)
        {
          assert_arg ("--vm-stride");
          do_vm_stride = atoll_b (arg);
          if (do_vm_stride <= 0)
            {
              err (stderr, "invalid stride value: %lli\n", do_vm_stride);
              exit (1);
            }
        }
      else if (strcmp (arg, "--vm-hang") == 0)
        {
          assert_arg ("--vm-hang");
          do_vm_hang = atoll_b (arg);
          if (do_vm_hang < 0)
            {
              err (stderr, "invalid value: %lli\n", do_vm_hang);
              exit (1);
            }
        }
      else if (strcmp (arg, "--vm-keep") == 0)
        {
          do_vm_keep = 1;
        }
      else if (strcmp (arg, "--hdd") == 0 || strcmp (arg, "-d") == 0)
        {
          assert_arg ("--hdd");
          do_hdd = atoll_b (arg);
          if (do_hdd <= 0)
            {
              err (stderr, "invalid number of hdd hogs: %lli\n", do_hdd);
              exit (1);
            }
        }
      else if (strcmp (arg, "--hdd-bytes") == 0)
        {
          assert_arg ("--hdd-bytes");
          do_hdd_bytes = atoll_b (arg);
          if (do_hdd_bytes <= 0)
            {
              err (stderr, "invalid hdd byte value: %lli\n", do_hdd_bytes);
              exit (1);
            }
        }
      else if (strcmp (arg, "--hdd-targets") == 0)
        {
          assert_arg ("--hdd-targets");
          do_hdd_target = 1;
          hdd_targets = atoll_b (arg);
          for (z = 0; z < hdd_targets; z++)
	    {
	      targetlist[z] = malloc(24 * sizeof(char));
              strcpy(targetlist[z],argv[i+z+1]);
	    }
	    i+=z;
	}     
      else if (strcmp (arg, "--auto") == 0)
	{
	  do_vm = (gbram / 2) - 1;
          do_vm_bytes = 2048ul * 1024ul * 1024ul;
          if (do_vm == 0) {
            do_vm = 1;
            do_vm_bytes = 1024ul * 1024ul * 1024ul;
          }
	  do_cpu = numprocs; 
	  do_io = 2; 
 	  do_timeout = 30;
	/* This sets the timeout for 30seconds, change to 720m when ready for production 
	  do_timeout=43200
	*/
	}  	
      else
        {
          err (stderr, "unrecognized option: %s\n", arg);
          exit (1);
        }
    }

  /* Print startup message if we have work to do, bail otherwise.  */
  if (do_cpu + do_io + do_vm + do_hdd)
    {
      out (stdout, "dispatching hogs: %lli cpu, %lli io, %lli vm, %lli hdd\n",
           do_cpu, do_io, do_vm, do_hdd);
    }
  else
    usage (0);
  if (do_cpu) {
    cputest = do_cpu;
    pipe(cpu_pipe);
  }
  if (do_io) {
    iotest = do_io;
    pipe(io_pipe);
  }
  if (do_vm) {
    malloctest = do_vm;
    pipe(malloc_pipe);
  }
  if (do_hdd) {
    hddtest = do_hdd;
    pipe(hdd_pipe);
  }
  /* Round robin dispatch our worker processes.  */
  while ((forks = (do_cpu + do_io + do_vm + do_hdd)))
    {
      long long backoff, timeout = 0;

      /* Calculate the backoff value so we get good fork throughput.  */
      backoff = do_backoff * forks;
      dbg (stdout, "using backoff sleep of %llius\n", backoff);

      /* If we are supposed to respect a timeout, calculate it.  */
      if (do_timeout)
        {
          long long currenttime;

          /* Acquire current time.  */
          if ((currenttime = time (NULL)) == -1)
            {
              perror ("error acquiring current time");
              exit (1);
            }

          /* Calculate timeout based on current time.  */
          timeout = do_timeout - (currenttime - starttime);

          if (timeout > 0)
            {
              dbg (stdout, "setting timeout to %llis\n", timeout);
            }
          else
            {
              wrn (stderr, "used up time before all workers dispatched\n");
              break;
            }
        }

      if (do_cpu)
        { 
	  switch (pid = fork ())
            {
            case 0:            /* child */
              alarm (timeout);
              usleep (backoff);
              if (do_dryrun)
                exit (0);
              exit (hogcpu(cpu_pipe, timeout));
            case -1:           /* error */
              err (stderr, "fork failed: %s\n", strerror (errno));
              break;
            default:           /* parent */
              dbg (stdout, "--> hogcpu worker %lli [%i] forked\n",
                   do_cpu, pid);
              ++children;
            }
          --do_cpu;
        }

      if (do_io)
        {
          switch (pid = fork ())
            {
            case 0:            /* child */
              alarm (timeout);
              usleep (backoff);
              if (do_dryrun)
                exit (0);
              exit (hogio (io_pipe, timeout));
            case -1:           /* error */
              err (stderr, "fork failed: %s\n", strerror (errno));
              break;
            default:           /* parent */
              dbg (stdout, "--> hogio worker %lli [%i] forked\n", do_io, pid);
              ++children;
            }
          --do_io;
        }

      if (do_vm)
        {
          switch (pid = fork ())
            {
            case 0:            /* child */
              alarm (timeout);
              usleep (backoff);
              if (do_dryrun)
                exit (0);
              exit (hogvm
                    (do_vm_bytes, do_vm_stride, do_vm_hang, do_vm_keep, malloc_pipe, timeout));
            case -1:           /* error */
              err (stderr, "fork failed: %s\n", strerror (errno));
              break;
            default:           /* parent */
              dbg (stdout, "--> hogvm worker %lli [%i] forked\n", do_vm, pid);
              ++children;
            }
          --do_vm;
        }

      if (do_hdd)
        {
	  for (z = 0; z < hdd_targets; z++)
  	    {
              switch (pid = fork ())
                {
                case 0:            /* child */
                  alarm (timeout);
                  usleep (backoff);
                  if (do_dryrun)
                    exit (0);
                  exit (hoghdd (do_hdd_bytes, hdd_pipe, timeout, targetlist[z], z));
                case -1:           /* error */
                  err (stderr, "fork failed: %s\n", strerror (errno));
                  break;
                default:           /* parent */
                  dbg (stdout, "--> hoghdd worker %lli [%i] forked\n",
                    do_hdd, pid);
                  ++children;
                }
              --do_hdd;
            }
        }
    }
  close(cpu_pipe[1]);
  close(io_pipe[1]);
  close(malloc_pipe[1]);
  close(hdd_pipe[1]);

  /* Wait for our children to exit.  */
  while (children)
    {
      int status, ret;

      if ((pid = wait (&status)) > 0)
        {
          --children;

          if (WIFEXITED (status))
            {
              if ((ret = WEXITSTATUS (status)) == 0)
                {
                  dbg (stdout, "<-- worker %i returned normally\n", pid);
                }
              else
                {
                  err (stderr, "<-- worker %i returned error %i\n", pid, ret);
                  ++retval;
                  wrn (stderr, "now reaping child worker processes\n");
                  if (signal (SIGUSR1, SIG_IGN) == SIG_ERR)
                    err (stderr, "handler error: %s\n", strerror (errno));
                  if (kill (-1 * getpid (), SIGUSR1) == -1)
                    err (stderr, "kill error: %s\n", strerror (errno));
                }
            }
          else if (WIFSIGNALED (status))
            {
              if ((ret = WTERMSIG (status)) == SIGALRM)
                {
                  dbg (stdout, "<-- worker %i signalled normally\n", pid);
                }
              else if ((ret = WTERMSIG (status)) == SIGUSR1)
                {
                  dbg (stdout, "<-- worker %i reaped\n", pid);
                }
              else
                {
                  err (stderr, "<-- worker %i got signal %i\n", pid, ret);
                  ++retval;
                  wrn (stderr, "now reaping child worker processes\n");
                  if (signal (SIGUSR1, SIG_IGN) == SIG_ERR)
                    err (stderr, "handler error: %s\n", strerror (errno));
                  if (kill (-1 * getpid (), SIGUSR1) == -1)
                    err (stderr, "kill error: %s\n", strerror (errno));
                }
            }
          else
            {
              err (stderr, "<-- worker %i exited abnormally\n", pid);
              ++retval;
            }
        }
      else
        {
          err (stderr, "error waiting for worker: %s\n", strerror (errno));
          ++retval;
          break;
        }
    }

  /* Record our stop time.  */
  if ((stoptime = time (NULL)) == -1)
    {
      err (stderr, "failed to acquire current time\n");
      exit (1);
    }

  /* Calculate our runtime.  */
  runtime = stoptime - starttime;

  /* Calculate total CPU cycles from cpu_pipe */
  if ((cputest > 0) && (!do_dryrun))  {
    for (x=0; x < cputest; x++) {
      read(cpu_pipe[0], line, BUFSIZE);
      sscanf(line,"%lli",&currentcpu);
      totalcpu += currentcpu; 
    }
    out (stdout, "%lli total cpu cycles run\n", totalcpu);
    close(cpu_pipe[0]);
  }
  else { 
    out (stdout, "cputest not run\n");
  }
  
  /* Calculate total SYNC cycles from io_pipe */
  if ((iotest > 0) && (!do_dryrun)) {
    for (x=0; x < iotest; x++) {
      read(io_pipe[0], line, BUFSIZE);
      sscanf(line,"%lli",&currentio);
      totalio += currentio;
    }
    out (stdout, "%lli total io cycles run\n", totalio);
    close(io_pipe[0]);  
  }
  else {
    out (stdout, "iotest not run\n");
  }

  /* Calculate total malloc/free cycles */
  if ((malloctest > 0) && (!do_dryrun)) {
    for (x=0; x < malloctest; x++) {
      read(malloc_pipe[0], line, BUFSIZE);
      sscanf(line,"%lli",&currentmalloc);
      totalmalloc += currentmalloc;
    }
    out (stdout, "%lli total malloc cycles run\n", totalmalloc);
    close(malloc_pipe[0]);
  }
  else {
    out(stdout, "malloctest not run\n");
  }

  /* Calculate total hdd cycles */
  if (hddtest > 0) {
    for (x=0; x < hddtest; x++) {
      read(hdd_pipe[0], line, BUFSIZE);
      sscanf(line,"%lli",&currenthdd);
      hddresult = strtok(line,hdddelims);
      while(hddresult!=NULL) {
	sscanf(hddresult,"%lli",&resulttarget);
        hddresult = strtok(NULL,hdddelims);
	sscanf(hddresult,"%lli",&hddint);
	hdd_results[resulttarget] += hddint;
	hddresult = strtok(NULL,hdddelims);
      }
    }
    for (z = 0; z < hdd_targets; z++) {
      out (stdout, "%lli total hdd cycles run on %s\n", hdd_results[z], targetlist[z]);
    } 
    close(hdd_pipe[0]);
  }
  else { 
    out (stdout, "hddtest not run\n");
  }

  /* Calculate CPU operations/second (sqrt results) */
  if ((cputest > 0) && (!do_dryrun)) {
    cpupersec = totalcpu / runtime; 
    cpupercore = (cpupersec / numprocs) / 1024; 
    out (stdout, "Your CPU performed %lliK operations per second (%lliK per core)\n", cpupersec, cpupercore);
  }

  /* Calculate IO operations/second (sync results) */
  if ((iotest > 0) && (!do_dryrun)) {
    iopersec = totalio / runtime;
    out (stdout, "%lli total sync() operations run (%lli/second)\n", totalio, iopersec);
  }

  /* Calculate bytes allocated/second (malloc results)*/
  if ((malloctest > 0) && (!do_dryrun)) {
    allocatedbytes = ((totalmalloc * do_vm_bytes) / 1024) / 1024;
    bytespersec = allocatedbytes / runtime;
    out (stdout, "%lli total megabytes RAM allocated (%lliMB/second)\n", allocatedbytes, bytespersec);
  }

  /* Calculate bytes written/seconds (write results)*/
  if ((hddtest > 0) && (!do_dryrun)) {
    for (z=0; z < hdd_targets; z++) {
      writtenbytes = ((hdd_results[z] * do_hdd_bytes) / 1024) / 1024; 
      writepersec = writtenbytes / runtime; 
      out (stdout, "%lli total megabyte written (%lliMB/second) on %s\n", writtenbytes, writepersec, targetlist[z]);
    }
  }

  /* Print final status message.  */
  if (retval)
    {
      err (stderr, "failed run completed in %lis\n", runtime);
    }
  else
    {
      out (stdout, "successful run completed in %llis\n", runtime);
    }

  exit (retval);
}

int
hogcpu (int cpu_pipe[], long long timeout)
{
  signal(SIGALRM, &sigh);
  unsigned long long cpucycles = 0;
  close(cpu_pipe[0]);
  alarm(timeout);
  while (!alarmed) {
    sqrt (rand ());
    cpucycles++;
  }
  sprintf(line,"%lli",cpucycles);
  write(cpu_pipe[1], line, BUFSIZE);
  close(cpu_pipe[1]); 
  return 0;
}

int
hogio (int io_pipe[], long long timeout)
{
  signal(SIGALRM, &sigh);
  unsigned long long synccycles = 0;
  close(io_pipe[0]);
  alarm(timeout);
  while (!alarmed) {
    sync ();
    synccycles++;
  }
  sprintf(line,"%lli",synccycles);
  write(io_pipe[1], line, BUFSIZE);
  close(io_pipe[1]);
  return 0;
}

int
hogvm (long long bytes, long long stride, long long hang, int keep, int malloc_pipe[], long long timeout)
{
  signal (SIGALRM, &sigh);
  long long i;
  long long malloccycles = 0, freecycles = 0;
  close(malloc_pipe[0]);
  char *ptr = 0;
  char c;
  int do_malloc = 1;
  alarm(timeout);
  while (!alarmed)
    {
      if (do_malloc)
        {
          dbg (stdout, "allocating %lli bytes ...\n", bytes);
          if (!(ptr = (char *) malloc (bytes * sizeof (char))))
            {
              err (stderr, "hogvm malloc failed: %s\n", strerror (errno));
              return 1;
            }
          if (keep)
            do_malloc = 0;
        }

      dbg (stdout, "touching bytes in strides of %lli bytes ...\n", stride);
      for (i = 0; i < bytes; i += stride)
        ptr[i] = 'Z';           /* Ensure that COW happens.  */

      if (hang == 0)
        {
          dbg (stdout, "sleeping forever with allocated memory\n");
          while (1)
            sleep (1024);
        }
      else if (hang > 0)
        {
          dbg (stdout, "sleeping for %llis with allocated memory\n", hang);
          sleep (hang);
        }

      for (i = 0; i < bytes; i += stride)
        {
          c = ptr[i];
          if (c != 'Z')
            {
              err (stderr, "memory corruption at: %p\n", ptr + i);
              return 1;
            }
        }

      if (do_malloc)
        {
          free (ptr);
          dbg (stdout, "freed %lli bytes\n", bytes);
	  malloccycles++;
        }
    }
  sprintf(line,"%lli",malloccycles);
  write(malloc_pipe[1], line, BUFSIZE);
  close(malloc_pipe[1]);
  return 0;
}

int
hoghdd (long long bytes, int hdd_pipe[], long long timeout, char *target, int targetnum)
{
  signal (SIGALRM, &sigh);
  long long i, j;
  long long hddcycles = 0;
  close(hdd_pipe[0]);
  int fd;
  int chunk = (1024 * 1024) - 1;        /* Minimize slow writing.  */
  char buff[chunk];
  char name[BUFSIZE];
  /* Initialize buffer with some random ASCII data.  */
  dbg (stdout, "seeding %d byte buffer with random data\n", chunk);
  for (i = 0; i < chunk - 1; i++)
    {
      j = rand ();
      j = (j < 0) ? -j : j;
      j %= 95;
      j += 32;
      buff[i] = j;
    }
  buff[i] = '\n';
  if (target[strlen(target) -1] != '/') {
    strcat(target,"/");
  }
	
  while (!alarmed)
    {
      if (do_hdd_target) {
        char name[] = "stress.XXXXXXX";
 	strcat(target,name);
      }
      else {
        char name[] = "./stress.XXXXXX";
	target = name;  
      }
      dbg (stdout, "new target is %s\n", target);
      if ((fd = mkstemp (target)) == -1)
        {
          err (stderr, "mkstemp failed: %s\n", strerror (errno));
          return 1;
        }

      dbg (stdout, "opened %s for writing %lli bytes\n", target, bytes);

      dbg (stdout, "unlinking %s\n", target);
      if (unlink (target) == -1)
        {
          err (stderr, "unlink of %s failed: %s\n", target, strerror (errno));
          return 1;
        }

      dbg (stdout, "fast writing to %s\n", target);
      for (j = 0; bytes == 0 || j + chunk < bytes; j += chunk)
        {
          if (write (fd, buff, chunk) == -1)
            {
              err (stderr, "write failed: %s\n", strerror (errno));
              return 1;
            }
        }

      dbg (stdout, "slow writing to %s\n", target);
      for (; bytes == 0 || j < bytes - 1; j++)
        {
          if (write (fd, &buff[j % chunk], 1) == -1)
            {
              err (stderr, "write failed: %s\n", strerror (errno));
              return 1;
            }
        }
      if (write (fd, "\n", 1) == -1)
        {
          err (stderr, "write failed: %s\n", strerror (errno));
          return 1;
        }
      ++j;

      dbg (stdout, "closing %s after %lli bytes\n", target, j);
      close (fd);
      hddcycles++;
    }
  sprintf(line,"%lli",hddcycles);
  sprintf(fullline,"%d,",targetnum);
  strcat(fullline,line);
   
  write(hdd_pipe[1], fullline, BUFSIZE);
  close(hdd_pipe[1]);
  return 0;
}

/* Convert a string representation of a number with an optional size suffix
 * to a long long.
 */
long long
atoll_b (const char *nptr)
{
  int pos;
  char suffix;
  long long factor = 0;
  long long value;

  if ((pos = strlen (nptr) - 1) < 0)
    {
      err (stderr, "invalid string\n");
      exit (1);
    }

  switch (suffix = nptr[pos])
    {
    case 'b':
    case 'B':
      factor = 0;
      break;
    case 'k':
    case 'K':
      factor = 10;
      break;
    case 'm':
    case 'M':
      factor = 20;
      break;
    case 'g':
    case 'G':
      factor = 30;
      break;
    default:
      if (suffix < '0' || suffix > '9')
        {
          err (stderr, "unrecognized suffix: %c\n", suffix);
          exit (1);
        }
    }

  if (sscanf (nptr, "%lli", &value) != 1)
    {
      err (stderr, "invalid number: %s\n", nptr);
      exit (1);
    }

  value = value << factor;

  return value;
}

/* Convert a string representation of a number with an optional time suffix
 * to a long long.
 */
long long
atoll_s (const char *nptr)
{
  int pos;
  char suffix;
  long long factor = 1;
  long long value;

  if ((pos = strlen (nptr) - 1) < 0)
    {
      err (stderr, "invalid string\n");
      exit (1);
    }

  switch (suffix = nptr[pos])
    {
    case 's':
    case 'S':
      factor = 1;
      break;
    case 'm':
    case 'M':
      factor = 60;
      break;
    case 'h':
    case 'H':
      factor = 60 * 60;
      break;
    case 'd':
    case 'D':
      factor = 60 * 60 * 24;
      break;
    case 'y':
    case 'Y':
      factor = 60 * 60 * 24 * 365;
      break;
    default:
      if (suffix < '0' || suffix > '9')
        {
          err (stderr, "unrecognized suffix: %c\n", suffix);
          exit (1);
        }
    }

  if (sscanf (nptr, "%lli", &value) != 1)
    {
      err (stderr, "invalid number: %s\n", nptr);
      exit (1);
    }

  value = value * factor;

  return value;
}

int
version (int status)
{
  char *mesg = "%s %s\n";

  fprintf (stdout, mesg, global_progname, VERSION);

  if (status <= 0)
    exit (-1 * status);

  return 0;
}

int
usage (int status)
{
  char *mesg =
    "`%s' imposes certain types of compute stress on your system\n\n"
    "Usage: %s [OPTION [ARG]] ...\n"
    " -?, --help         show this help statement\n"
    "     --version      show version statement\n"
    " -v, --verbose      be verbose\n"
    " -q, --quiet        be quiet\n"
    " -n, --dry-run      show what would have been done\n"
    " -t, --timeout N    timeout after N seconds\n"
    "     --backoff N    wait factor of N microseconds before work starts\n"
    " -c, --cpu N        spawn N workers spinning on sqrt(), use N=a for auto\n"
    " -i, --io N         spawn N workers spinning on sync()\n"
    " -m, --vm N         spawn N workers spinning on malloc()/free(), use N=a for auto\n"
    "     --vm-bytes B   malloc B bytes per vm worker (default is 256MB), using '--vm a' sets this to 2GB\n"
    "     --vm-stride B  touch a byte every B bytes (default is 4096)\n"
    "     --vm-hang N    sleep N secs before free (default none, 0 is inf)\n"
    "     --vm-keep      redirty memory instead of freeing and reallocating\n"
    " -d, --hdd N        spawn N workers spinning on write()/unlink()\n"
    "     --hdd-bytes B  write B bytes per hdd worker (default is 1GB)\n"
    "     --hdd-targets N <targets> Set targets for hdd workers, N must match number of targets listed after"
    "     --auto         Automatically set values for --cpu and --vm\n\n"
    "Example: %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M --timeout 10s\n\n"
    "Note: Numbers may be suffixed with s,m,h,d,y (time) or B,K,M,G (size).\n";

  fprintf (stdout, mesg, global_progname, global_progname, global_progname);

  if (status <= 0)
    exit (-1 * status);

  return 0;
}
