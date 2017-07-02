#ifndef D4EST_PETSC_H
#define D4EST_PETSC_H 

#include <pXest.h>
#include <problem_data.h>
#include <d4est_operators.h>
#include <d4est_quadrature.h>
#include <d4est_geometry.h>
#include <petscsnes.h>

typedef struct {

  p4est_t* p4est;
  d4est_elliptic_problem_data_t* vecs;
  void* fcns;
  p4est_ghost_t** ghost;
  void** ghost_data;
  d4est_operators_t* d4est_ops;
  d4est_geometry_t* d4est_geom;
  d4est_quadrature_t* d4est_quad;
  
} petsc_ctx_t;

#endif
