#include <stdio.h>

#define GSL_CHECK_RANGE yes
#include <gsl_vector.h>
#include <gsl_test.h>

#define N 10000

void my_error_handler (const char *reason, const char *file, int line);

int status = 0 ;

int main (void) 
{
  gsl_vector * v;
  size_t j = 0;
  double x; 

  gsl_set_error_handler (&my_error_handler);

  v = gsl_vector_alloc(N) ;

  status = 0 ;
  gsl_vector_set(v, j-1, 1.2) ;
  gsl_test(!status, "gsl_vector_set traps index below lower array bound") ;

  status = 0 ;
  gsl_vector_set(v, N+1, 1.2) ;
  gsl_test(!status, "gsl_vector_set traps index above upper array bound") ;

  status = 0 ;
  x = gsl_vector_get(v, j-1) ;
  gsl_test(!status, "gsl_vector_get traps index below lower array bound") ;
  gsl_test(x != 0, "gsl_vector_get returns zero for index below lower array bound") ;

  status = 0 ;
  x = gsl_vector_get(v, N+1) ;
  gsl_test(!status, "gsl_vector_get traps index above upper array bound") ;
  gsl_test(x != 0, "gsl_vector_get returns zero for index above upper array bound") ;
  return gsl_test_summary ();
}

void
my_error_handler (const char *reason, const char *file, int line)
{
  if (0) printf ("(caught [%s:%d: %s])\n", file, line, reason) ;
  status = 1 ;
}
