#include "./pXest/pXest.h"
#include "./pXest/pXest_input.h"
#include "./Problems/problem.h"
#include "./Geometry/d4est_geometry.h"
 #include <petscsnes.h>

int main(int argc, char *argv[])
{  
  sc_MPI_Comm mpicomm;
  PetscInitialize(&argc,&argv,(char*)0,NULL);
  mpicomm = PETSC_COMM_WORLD;
  
  int proc_size;
  int proc_rank;
  MPI_Comm_size(mpicomm, &proc_size);
  MPI_Comm_rank(mpicomm, &proc_rank);


  
#ifndef NDEBUG
  if(proc_rank == 0)
    printf("[D4EST_INFO]: DEBUG MODE ON\n");
  /* p4est_init(NULL, SC_LP_ALWAYS); */
  p4est_init(NULL, SC_LP_ERROR);
#else
  if(proc_rank == 0)
    printf("[D4EST_INFO]: DEBUG MODE OFF\n");
  p4est_init(NULL, SC_LP_ERROR);
#endif

#ifdef CURVED
  if(proc_rank == 0)
    printf("[D4EST_INFO]: Using CURVED infrastructure\n");
#else
  if(proc_rank == 0)
    printf("[D4EST_INFO]: Using NON-CURVED infrastructure\n");  
#endif
  
#if (P4EST_DIM)==3
  if(proc_rank == 0)
    printf("[D4EST_INFO]: DIM = 3\n");
#else
  if(proc_rank == 0)
    printf("[D4EST_INFO]: DIM = 2\n");
#endif

 
  d4est_geometry_t* d4est_geom = d4est_geometry_new(proc_rank,
                                                    "options.input",
                                                    "geometry",
                                                    "[D4EST_GEOMETRY]");
  /* p4est_connectivity_t* conn = problem_build_conn(); */

  pXest_input_t pXest_input = pXest_input_parse("options.input");
  p4est_t* p4est = problem_build_p4est
                   (
                    mpicomm,
                    d4est_geom->p4est_conn,
                    pXest_input.min_quadrants,
                    pXest_input.min_level,
                    pXest_input.fill_uniform
                   );

  if (proc_rank == 0){
    printf("[D4EST_INFO]: mpisize = %d\n", proc_size);
    printf("[D4EST_INFO]: min_quadrants = %d\n", pXest_input.min_quadrants);
    printf("[D4EST_INFO]: min_level = %d\n", pXest_input.min_level);
    printf("[D4EST_INFO]: fill_uniform = %d\n", pXest_input.fill_uniform);
  }
  if(pXest_input.print_elements_per_proc){
    sc_MPI_Barrier(mpicomm);
    printf("[D4EST_INFO]: elements on proc %d = %d\n", proc_rank, p4est->local_num_quadrants);
    sc_MPI_Barrier(mpicomm);
  }
  
  /* start just-in-time dg-math */
  dgmath_jit_dbase_t* dgmath_jit_dbase = dgmath_jit_dbase_init();  
  
  /* Solve Problem */
  problem_init
    (
     "options.input",
     p4est,
     d4est_geom,
     dgmath_jit_dbase,
     proc_size,
     mpicomm
    );
  
  dgmath_jit_dbase_destroy(dgmath_jit_dbase);
  
  /* free pXest */
  p4est_destroy(p4est);
  d4est_geometry_destroy(d4est_geom);


  PetscFinalize();
  /* sc_finalize (); */
  return 0;
}
