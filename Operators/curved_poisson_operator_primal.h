#ifndef CURVED_POISSON_OPERATOR_PRIMAL_H
#define CURVED_POISSON_OPERATOR_PRIMAL_H 

void curved_poisson_operator_primal_apply_aij(p4est_t *p4est,
                                              p4est_ghost_t *ghost,curved_element_data_t *ghost_data,problem_data_t *prob_vecs,dgmath_jit_dbase_t *dgmath_jit_dbase,d4est_geometry_t *geom);

#endif
