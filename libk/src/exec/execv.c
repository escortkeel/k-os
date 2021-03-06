#include <_ansi.h>
#include <unistd.h>

/* Only deal with a pointer to environ, to work around subtle bugs with shared
   libraries and/or small data systems where the user declares his own
   'environ'.  */
static char ***p_environ = &environ;

int
execv (const char *path,
	char * const argv[])
{
  return execve (path, (char * const *) argv, *p_environ);
}
