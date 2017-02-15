#include <sc_reduce.h>
#include <pXest.h>
#include <util.h>
#include <linalg.h>
#include <curved_element_data.h>
#include <sipg_flux_vector_fcns.h>
#include <curved_Gauss_sipg_flux_scalar_fcns.h>
#include <curved_Gauss_sipg_flux_vector_fcns.h>
#include <curved_Gauss_central_flux_vector_fcns.h>
#include <problem.h>
#include <problem_data.h>
#include <problem_weakeqn_ptrs.h>
#include <central_flux_params.h>
#include <curved_poisson_operator.h>
#include <curved_bi_estimator.h>
#include <krylov_petsc.h>
#include <matrix_sym_tester.h>
#include <dg_norm.h>
#include <hp_amr.h>
#include <hp_amr_curved_smooth_pred.h>
#include <d4est_geometry.h>
#include <d4est_geometry_sphere.h>
#include <d4est_geometry_disk.h>
#include <curved_poisson_debug_vecs.h>
#include <d4est_vtk.h>
#include <bi_estimator_flux_fcns.h>
#include <ini.h>
#include "time.h"
#include "util.h"


#define NUM_PUNCTURES 2
static const double pi = 3.1415926535897932384626433832795;
static const double puncture_eps = 0.00000000001;// .000000000001;
static double xyz_bh [NUM_PUNCTURES][3];
static double P_bh [NUM_PUNCTURES][3];
static double S_bh [NUM_PUNCTURES][3];
static double M_bh [NUM_PUNCTURES];

static
double levi_civita(int a, int b, int c)
{
  double eps;
  if( ( ( a == 0 )&&( b == 1 )&&( c == 2 ) ) ||
      ( ( a == 1 )&&( b == 2 )&&( c == 0 ) ) ||
      ( ( a == 2 )&&( b == 0 )&&( c == 1 ) ) ) {
    eps = 1.;
  } else
    if( ( ( a == 1 )&&( b == 0 )&&( c == 2 ) ) ||
        ( ( a == 0 )&&( b == 2 )&&( c == 1 ) ) ||
        ( ( a == 2 )&&( b == 1 )&&( c == 0 ) ) ) {
      eps = -1.;
    } else {
      eps = 0.;
    }
  return eps;
}

static
double kronecker(int a, int b)
{
  /* printf("a = %d\n", a); */
  /* printf("b = %d\n", b); */
  /* printf("a == b = %d\n", (a==b)); */
  
  if (a != b)
    return 0.0;
  else
    return 1.0;

  /* return (double)(a==b); */
}

static
gamma_params_t
gamma_setter(curved_element_data_t* elem_data)
{
  gamma_params_t gamma_hpn;
  gamma_hpn.gamma_h = 0.;
  gamma_hpn.gamma_p = 0.;
  gamma_hpn.gamma_n = 0.;
  return gamma_hpn;
}

static
double psi_fcn
(
 double x,
 double y,
 double z,
 double u
)
{
  double sumn_mn_o_2rn = 0.;
  double dxn, dyn, dzn, r;
  int n;
  for (n = 0; n < NUM_PUNCTURES; n++){
    dxn = x - xyz_bh[n][0];
    dyn = y - xyz_bh[n][1];
    dzn = z - xyz_bh[n][2];
    r = sqrt(dxn*dxn + dyn*dyn + dzn*dzn);
    if (r == 0.)
      r += puncture_eps;
    sumn_mn_o_2rn += M_bh[n]/(2.*r);
  }

  return 1. + u + sumn_mn_o_2rn;
}

static
double compute_confAij
(
 int i,
 int j,
 double x,
 double y,
 double z
)
{
  double nvec[NUM_PUNCTURES][3];
  double confAij = 0.;
  
  /* initialize and check if (x,y,z) is a puncture point */
  int n;
  for (n = 0; n < NUM_PUNCTURES; n++){
    double dxn = (x - xyz_bh[n][0]);
    double dyn = (y - xyz_bh[n][1]);
    double dzn = (z - xyz_bh[n][2]);
    double rn = sqrt(dxn*dxn + dyn*dyn + dzn*dzn);
    if (rn == 0.)
      rn += puncture_eps;
    nvec[n][0] = (x - xyz_bh[n][0])/rn;
    nvec[n][1] = (y - xyz_bh[n][1])/rn;
    nvec[n][2] = (z - xyz_bh[n][2])/rn;

    double lcikl_dot_Sk_dot_nln_times_njn = 0.;
    double lcjkl_dot_Sk_dot_nln_times_nin = 0.;
    double Pn_dot_nvec = 0.;
    
    int k,l;
    for (k = 0; k < 3; k++){
      Pn_dot_nvec += P_bh[n][k]*nvec[n][k];
      for (l = 0; l < 3; l++){
        lcikl_dot_Sk_dot_nln_times_njn += (levi_civita(i,k,l))*S_bh[n][k]*nvec[n][l]*nvec[n][j];
        lcjkl_dot_Sk_dot_nln_times_nin += (levi_civita(j,k,l))*S_bh[n][k]*nvec[n][l]*nvec[n][i];
      }
    }

    double a = nvec[n][i]*P_bh[n][j] + nvec[n][j]*P_bh[n][i] - (kronecker(i,j) - nvec[n][i]*nvec[n][j])*Pn_dot_nvec;
    double b = lcikl_dot_Sk_dot_nln_times_njn + lcjkl_dot_Sk_dot_nln_times_nin;

    confAij += (1./(rn*rn))*(a + (2./rn)*b);
  }

  confAij *= 3./2.;
  return confAij;
}

static
double compute_confAij_sqr
(
 double x,
 double y,
 double z
)
{
  double confAij;
  double confAij_sqr = 0.;
  int i,j;
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++){
      confAij = compute_confAij(i,j,x,y,z);
      confAij_sqr += confAij*confAij;
    }
  return confAij_sqr;
}


static
double neg_1o8_K2_psi_neg7
(
 double x,
 double y,
 double z,
 double u,
 void* user
)
{
  double sumn_mn_o_2rn = 0.;
  double dxn, dyn, dzn, r;
  int n;
  for (n = 0; n < NUM_PUNCTURES; n++){
    dxn = x - xyz_bh[n][0];
    dyn = y - xyz_bh[n][1];
    dzn = z - xyz_bh[n][2];
    r = sqrt(dxn*dxn + dyn*dyn + dzn*dzn);
    sumn_mn_o_2rn += M_bh[n]/(2.*r);
  }

  double psi_0 = 1. + u + sumn_mn_o_2rn;
  double confAij_sqr = compute_confAij_sqr(x,y,z);

  if (r > puncture_eps)
    return (-1./8.)*confAij_sqr/(psi_0*psi_0*psi_0*psi_0*psi_0*psi_0*psi_0);
  else
    return 0.;
  /* return (1000./8.)*confAij_sqr/(psi_0*psi_0*psi_0*psi_0*psi_0*psi_0*psi_0); */
}


static
void
build_residual
(
 p4est_t* p4est,
 p4est_ghost_t* ghost,
 curved_element_data_t* ghost_data,
 problem_data_t* prob_vecs,
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 d4est_geometry_t* d4est_geom
)
{
  curved_Gauss_poisson_apply_aij(p4est, ghost, ghost_data, prob_vecs, dgmath_jit_dbase, d4est_geom);  

  double* M_neg_1o8_K2_psi_neg7_vec= P4EST_ALLOC(double, prob_vecs->local_nodes);
 
  for (p4est_topidx_t tt = p4est->first_local_tree;
       tt <= p4est->last_local_tree;
       ++tt)
    {
      p4est_tree_t* tree = p4est_tree_array_index (p4est->trees, tt);
      sc_array_t* tquadrants = &tree->quadrants;
      int Q = (p4est_locidx_t) tquadrants->elem_count;
      for (int q = 0; q < Q; ++q) {
        p4est_quadrant_t* quad = p4est_quadrant_array_index (tquadrants, q);
        curved_element_data_t* ed = quad->p.user_data;        
        dgmath_apply_fofufofvlj_Gaussnodes
          (
           dgmath_jit_dbase,
           &prob_vecs->u[ed->nodal_stride],
           NULL,
           ed->deg,
           ed->J_integ,
           ed->xyz_integ,
           ed->deg_integ,
           (P4EST_DIM),
           &M_neg_1o8_K2_psi_neg7_vec[ed->nodal_stride],
           neg_1o8_K2_psi_neg7,
           NULL,
           NULL,
           NULL
          );
      }
    }

  linalg_vec_axpy(1.0,
                  M_neg_1o8_K2_psi_neg7_vec,
                  prob_vecs->Au,
                  prob_vecs->local_nodes);

  P4EST_FREE(M_neg_1o8_K2_psi_neg7_vec); 
}

typedef struct {

  int num_of_amr_levels;
  int deg;
  int deg_integ;
  double ip_flux_penalty;
  int count;
  
} problem_input_t;


static
int problem_input_handler
(
 void* user,
 const char* section,
 const char* name,
 const char* value
)
{
  problem_input_t* pconfig = (problem_input_t*)user;
  if (util_match_couple(section,"amr",name,"num_of_amr_levels")) {
    mpi_assert(pconfig->num_of_amr_levels == -1);
    pconfig->num_of_amr_levels = atoi(value);
    pconfig->count += 1;
  }
  else if (util_match_couple(section,"flux",name,"ip_flux_penalty")) {
    mpi_assert(pconfig->ip_flux_penalty == -1);
    pconfig->ip_flux_penalty = atof(value);
    pconfig->count += 1;
  } 
  else if (util_match_couple(section,"problem",name,"deg")) {
    mpi_assert(pconfig->deg == -1);
    pconfig->deg = atoi(value);
    pconfig->count += 1;
  }
  else if (util_match_couple(section,"problem",name,"deg_integ")) {
    mpi_assert(pconfig->deg_integ == -1);
    pconfig->deg_integ = atoi(value);
    pconfig->count += 1;
  }  

  else {
    return 0;  /* unknown section/name, error */
  }
  return 1;
}


static
problem_input_t
problem_input
(
 const char* input_file
)
{
  int num_of_options = 4;
  
  problem_input_t input;
  input.num_of_amr_levels = -1;
  input.ip_flux_penalty = -1;
  input.deg = -1;
  input.deg_integ = -1;
  
  input.count = 0;
  
  if (ini_parse(input_file, problem_input_handler, &input) < 0) {
    mpi_abort("Can't load input file");
  }

  mpi_assert(input.count == num_of_options);
  return input;
}

/* p4est_geometry_t* */
/* problem_build_geom */
/* ( */
/*  p4est_connectivity_t* conn */
/* ) */
/* { */
/*   /\* mpi_assert((P4EST_DIM)==3); *\/ */
/*   p4est_geometry_t* geom; */
/*   problem_input_t input = problem_input("options.input"); */

/*   /\* geom = d4est_geometry_new_compact_sphere(conn, input.R2, input.R1, input.R0, input.w, input.Rinf); *\/ */
/* #if (P4EST_DIM)==3 */
/*   geom = d4est_geometry_new_sphere(conn, input.R2, input.R1, input.R0); */
/* #endif */
/* #if (P4EST_DIM)==2 */
/*   geom = d4est_geometry_new_disk(conn, input.R1, input.R2); */
/* #endif */
  
/*   /\* printf("input.R2 = %.25f\n", input.R2); *\/ */

  
/*   return geom;   */
/* } */

p4est_t*
problem_build_p4est
(
 sc_MPI_Comm mpicomm,
 p4est_connectivity_t* conn,
 p4est_locidx_t min_quadrants,
 int min_level, 
 int fill_uniform
)
{
  return p4est_new_ext
    (
     mpicomm,
     conn,
     min_quadrants,
     min_level,
     fill_uniform,
     sizeof(curved_element_data_t),
     NULL,
     NULL
    );
}

p4est_t*
problem_load_p4est_from_checkpoint
(
 const char* filename,
 sc_MPI_Comm mpicomm,
 p4est_connectivity_t** conn
){
  int autopartition = 1;
  int load_data = 1;
  int broadcasthead = 0;
  
  return p4est_load_ext (filename,
                mpicomm,
                sizeof(curved_element_data_t),
                load_data,
                autopartition,
                broadcasthead,
                NULL,
                conn);
}

void
problem_set_degrees
(
 curved_element_data_t* elem_data,
 void* user_ctx
)
{
  problem_input_t* input = user_ctx;
  elem_data->deg = input->deg;
  elem_data->deg_integ = input->deg_integ;
}



void
problem_init
(
 const char* input_file,
 p4est_t* p4est,
 d4est_geometry_t* d4est_geom,
 dgmath_jit_dbase_t* dgmath_jit_dbase,
 int proc_size,
 sc_MPI_Comm mpicomm
)
{
  double M = 1.;
  
  M_bh[0] = .5*M;
  M_bh[1] = .5*M;

  xyz_bh[0][0] = -3*M;
  xyz_bh[0][1] = 0.;
  xyz_bh[0][2] = 0.;

  xyz_bh[1][0] = 3*M;
  xyz_bh[1][1] = 0;
  xyz_bh[1][2] = 0;

  P_bh[0][0] = 0.;
  P_bh[0][1] = -0.2*M;
  P_bh[0][2] = 0.;

  P_bh[1][0] = 0.;
  P_bh[1][1] = 0.2*M;
  P_bh[1][2] = 0.;

  S_bh[0][0] = 0.;
  S_bh[0][1] = 0.;
  S_bh[0][2] = 0.;
  
  S_bh[1][0] = 0.;
  S_bh[1][1] = 0.;
  S_bh[1][2] = 0.;

  problem_input_t input = problem_input(input_file);
  
  int level;
  
  mpi_assert((P4EST_DIM) == 2 || (P4EST_DIM) == 3);
  int world_rank, world_size;
  sc_MPI_Comm_rank(sc_MPI_COMM_WORLD, &world_rank);
  sc_MPI_Comm_size(sc_MPI_COMM_WORLD, &world_size);

  double* Au = P4EST_ALLOC_ZERO(double, 1);
  double* rhs = P4EST_ALLOC_ZERO(double, 1);
  double* u = P4EST_ALLOC_ZERO(double, 1);
  int local_nodes = 1;

  penalty_calc_t bi_u_penalty_fcn = bi_u_prefactor_conforming_maxp_minh;
  penalty_calc_t bi_u_dirichlet_penalty_fcn = bi_u_prefactor_conforming_maxp_minh;
  penalty_calc_t bi_gradu_penalty_fcn = bi_gradu_prefactor_maxp_minh;
  
  ip_flux_params_t ip_flux_params;
  ip_flux_params.ip_flux_penalty_prefactor = input.ip_flux_penalty;
  ip_flux_params.ip_flux_penalty_calculate_fcn = sipg_flux_vector_calc_penalty_maxp2_over_minh;

  central_flux_params_t central_flux_params;
  central_flux_params.central_flux_penalty_prefactor = input.ip_flux_penalty;
  
  p4est_ghost_t* ghost = p4est_ghost_new (p4est, P4EST_CONNECT_FACE);
  /* create space for storing the ghost data */
  curved_element_data_t* ghost_data = P4EST_ALLOC (curved_element_data_t,
                                                   ghost->ghosts.elem_count);

  p4est_partition(p4est, 0, NULL);
  p4est_balance (p4est, P4EST_CONNECT_FACE, NULL);
  /* geometric_factors_t* geometric_factors = geometric_factors_init(p4est); */


  /* grid_fcn_t boundary_flux_fcn = zero_fcn; */
  
  problem_data_t prob_vecs;
  prob_vecs.rhs = rhs;
  prob_vecs.Au = Au;
  prob_vecs.u = u;
  prob_vecs.local_nodes = local_nodes;

  curved_weakeqn_ptrs_t prob_fcns;
  /* prob_fcns.apply_lhs = curved_Gauss_poisson_apply_aij; */
  prob_fcns.build_residual = build_residual;
     
  geometric_factors_t* geometric_factors = geometric_factors_init(p4est);


  d4est_geom->dxdr_method = INTERP_X_ON_LOBATTO;    
  /* curved_element_data_init(p4est, geometric_factors, dgmath_jit_dbase, d4est_geom, degree, input.gauss_integ_deg); */
  curved_element_data_init_new(p4est,
                               geometric_factors,
                               dgmath_jit_dbase,
                               d4est_geom,
                               problem_set_degrees,
                               (void*)&input);




    
    local_nodes = curved_element_data_get_local_nodes(p4est);

    Au = P4EST_REALLOC(Au, double, local_nodes);
    u = P4EST_REALLOC(u, double, local_nodes);
    rhs = P4EST_REALLOC(rhs, double, local_nodes);
    /* u_analytic = P4EST_REALLOC(u_analytic, double, local_nodes); */

    prob_vecs.Au = Au;
    prob_vecs.u = u;
    prob_vecs.rhs = rhs;
    prob_vecs.local_nodes = local_nodes;


    prob_vecs.curved_vector_flux_fcn_data = curved_Gauss_sipg_flux_vector_dirichlet_fetch_fcns
                                            (
                                             zero_fcn,
                                             &ip_flux_params
                                            );

    prob_vecs.curved_scalar_flux_fcn_data = curved_Gauss_sipg_flux_scalar_dirichlet_fetch_fcns
                                            (zero_fcn);

    

    int percentile = 5;
    hp_amr_scheme_t* scheme =
      hp_amr_curved_smooth_pred_init
      (
       p4est,
       gamma_setter,
       (MAX_DEGREE)-2,
       hp_amr_curved_smooth_pred_get_percentile_marker(&percentile)
      );


    

    
  for (level = 0; level < input.num_of_amr_levels; ++level){
    
    linalg_fill_vec(prob_vecs.u, 0., local_nodes);   
    curved_bi_estimator_compute
      (
       p4est,
       &prob_vecs,
       &prob_fcns,
       bi_u_penalty_fcn,
       bi_u_dirichlet_penalty_fcn,
       bi_gradu_penalty_fcn,
       zero_fcn,
       ip_flux_params.ip_flux_penalty_prefactor,
       ghost,
       ghost_data,
       dgmath_jit_dbase,
       d4est_geom
      );
    
    estimator_stats_t stats;
    estimator_stats_compute(p4est, &stats,0);

    if(world_rank == 0)
      estimator_stats_print(&stats, 0);

    double local_eta2 = stats.total;
    printf("local_eta2 = %.25f\n", local_eta2);

    
    int* deg_array = P4EST_ALLOC(int, p4est->local_num_quadrants);
    int* eta_array = P4EST_ALLOC(int, p4est->local_num_quadrants);
    int vtk_nodes = 0;
     
    int stride = 0;
    for (p4est_topidx_t tt = p4est->first_local_tree;
         tt <= p4est->last_local_tree;
         ++tt)
      {
        p4est_tree_t* tree = p4est_tree_array_index (p4est->trees, tt);
        sc_array_t* tquadrants = &tree->quadrants;
        int Q = (p4est_locidx_t) tquadrants->elem_count;
        for (int q = 0; q < Q; ++q) {
          /* k++; */
          p4est_quadrant_t* quad = p4est_quadrant_array_index (tquadrants, q);
          curved_element_data_t* ed = quad->p.user_data;
          deg_array[stride] = ed->deg;
          eta_array[stride] = ed->local_estimator;
          vtk_nodes = util_int_pow_int(deg_array[stride], (P4EST_DIM))*(P4EST_CHILDREN);
          stride++;
        }
      }

  p4est_geometry_t* geom_vtk = d4est_geom->p4est_geom;
  d4est_vtk_context_t* vtk_ctx = d4est_vtk_dg_context_new(p4est, dgmath_jit_dbase, "compact-sphere");
  d4est_vtk_context_set_geom(vtk_ctx, geom_vtk);
  d4est_vtk_context_set_scale(vtk_ctx, .99);
  d4est_vtk_context_set_deg_array(vtk_ctx, deg_array);
  vtk_ctx = d4est_vtk_write_dg_header(vtk_ctx, dgmath_jit_dbase);
  vtk_ctx = d4est_vtk_write_dg_point_dataf(vtk_ctx, 1, 0, "u",u, vtk_ctx);
  vtk_ctx = d4est_vtk_write_dg_cell_dataf
            (
             vtk_ctx,
             1,
             1,
             1,
             0,
             1,
             1,
             0,
             "eta",
             eta_array,
             vtk_ctx
            );


  
  d4est_vtk_write_footer(vtk_ctx);
  P4EST_FREE(deg_array);
  P4EST_FREE(eta_array);

    
    
    hp_amr(p4est,
           dgmath_jit_dbase,
           &u,
           &stats,
           scheme,
           1
          );        
    
    p4est_ghost_destroy(ghost);
    P4EST_FREE(ghost_data);

    ghost = p4est_ghost_new(p4est, P4EST_CONNECT_FACE);
    ghost_data = P4EST_ALLOC(curved_element_data_t, ghost->ghosts.elem_count);
    
    curved_element_data_init_new(p4est,
                                 geometric_factors,
                                 dgmath_jit_dbase,
                                 d4est_geom,
                                 problem_set_degrees,
                                 (void*)&input);
    
    local_nodes = curved_element_data_get_local_nodes(p4est);

    Au = P4EST_REALLOC(Au, double, local_nodes);
    prob_vecs.Au = Au;
    prob_vecs.u = u;
    prob_vecs.u0 = u;
    prob_vecs.local_nodes = local_nodes;
          
  }
         
    geometric_factors_destroy(geometric_factors);

    if (ghost) {
      p4est_ghost_destroy (ghost);
      P4EST_FREE (ghost_data);
      ghost = NULL;
      ghost_data = NULL;
    }

  P4EST_FREE(Au);
  P4EST_FREE(rhs);
  /* P4EST_FREE(u_analytic); */
  P4EST_FREE(u);
}