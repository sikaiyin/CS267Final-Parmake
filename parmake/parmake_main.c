/**
 * Parallel Make
 * CS 241 - Fall 2016
 */

#include "parmake.h"

// Seperate main file used for grading. Do not modify!
int main(int argc, char **argv) {
  // calls the student code
#ifdef _OPENMP
#pragma omp parallel default(shared)
#endif
  parmake(argc, argv);
}
