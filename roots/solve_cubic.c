/* solve_cubic.c - finds the real roots of a x^3 + b x + c = 0 */

#include <math.h>
#include "poly_solve.h"
#include "constants.h"
#include "sorting.h"


int 
gsl_root_solve_cubic (double a, double b, double c, double x[])
{
  double tQ = (a * a - 3 * b) / 9;
  double tR = (2 * a * a * a - 9 * a * b + 27 * c) / 54;
  double tQ3 = tQ * tQ * tQ;
  double tR2 = tR * tR;

  if (tR2 < tQ3)
    {
      double rtQ = sqrt (tQ);
      double rtQ3 = rtQ * rtQ * rtQ;
      double theta = acos (tR / rtQ3);
      double norm = -2 * rtQ;
      x[0] = norm * cos (theta / 3) - a / 3;
      x[1] = norm * cos ((theta + 2 * M_PI) / 3) - a / 3;
      x[2] = norm * cos ((theta - 2 * M_PI) / 3) - a / 3;
      heapsortReal (x, 3);
      return 3;
    }
  else
    {
      double sgnR = (tR >= 0 ? 1 : -1);
      double tA = -sgnR * pow (fabs (tR) + sqrt (tR2 - tQ3), 1.0/3.0);
      double tB = (tA != 0 ? tQ / tA : 0);
      x[0] = tA + tB - a / 3;
      return 1;
    }
}
