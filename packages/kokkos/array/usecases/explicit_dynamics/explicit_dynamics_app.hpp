#ifndef INTERNALFORCE
#define INTERNALFORCE

#include <sys/time.h>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cmath>

#include <BoxMeshFixture.hpp>
#include <region.hpp>
#include <initialize_element.hpp>
#include <initialize_node.hpp>
#include <grad_hgop.hpp>
#include <decomp_rotate.hpp>
#include <divergence.hpp>
#include <minimum_stable_time_step.hpp>
#include <finish_step.hpp>

//----------------------------------------------------------------------------

namespace explicit_dynamics {

struct PerformanceData {
  double mesh_time ;
  double init_time ;
  double internal_force_time;
  double minimum_stable_time_step;
  double central_diff;
  double copy_to_host_time;

  PerformanceData()
  : mesh_time(0)
  , init_time(0)
  , internal_force_time(0)
  , minimum_stable_time_step(0)
  , central_diff(0)
  , copy_to_host_time(0)
  {}

  void best( const PerformanceData & rhs )
  {
    if ( rhs.mesh_time < mesh_time ) mesh_time = rhs.mesh_time ;
    if ( rhs.init_time < init_time ) init_time = rhs.init_time ;
    if ( rhs.internal_force_time < internal_force_time ) internal_force_time = rhs.internal_force_time ;
    if ( rhs.minimum_stable_time_step < minimum_stable_time_step ) minimum_stable_time_step = rhs.minimum_stable_time_step ;
    if ( rhs.central_diff < central_diff ) central_diff = rhs.central_diff ;
    if ( rhs.copy_to_host_time < copy_to_host_time ) copy_to_host_time = rhs.copy_to_host_time ;
  }
};

template<typename Scalar, class device_type>
double explicit_dynamics_app( const size_t ex, const size_t ey, const size_t ez, PerformanceData & perf )
{
  typedef typename Kokkos::MDArrayView<Scalar,device_type>::HostView  scalar_array_h;
  typedef typename Kokkos::MDArrayView<int,device_type>::HostView     int_array_h;

  typedef typename Kokkos::MDArrayView<Scalar,device_type>            scalar_array_d;
  typedef typename Kokkos::MDArrayView<int,device_type>               int_array_d;

  typedef typename Kokkos::ValueView<Scalar,device_type>::HostView   scalar_h;
  typedef typename Kokkos::ValueView<Scalar,device_type>             scalar_d;

  const int NumStates = 2;

  const Scalar user_dt = 1.0e-5;
  //const Scalar  end_time = 0.0050;

  // element block parameters
  const Scalar  lin_bulk_visc = 0.0;
  const Scalar  quad_bulk_visc = 0.0;
  //const Scalar  lin_bulk_visc = 0.06;
  //const Scalar  quad_bulk_visc = 1.2;
  const Scalar  hg_stiffness = 0.0;
  const Scalar  hg_viscosity = 0.0;
  //const Scalar  hg_stiffness = 0.03;
  //const Scalar  hg_viscosity = 0.001;

  // material properties
  const Scalar youngs_modulus=1.0e6;
  const Scalar poissons_ratio=0.0;
  const Scalar  density = 8.0e-4;

  Kokkos::Impl::Timer wall_clock ;

  BoxMeshFixture<int_array_h, scalar_array_h> mesh(ex,ey,ez);
  Region<Scalar,device_type>  region( NumStates,
                                      mesh,
                                      lin_bulk_visc,
                                      quad_bulk_visc,
                                      hg_stiffness,
                                      hg_viscosity,
                                      youngs_modulus,
                                      poissons_ratio,
                                      density);


  scalar_array_h  nodal_mass_h     =  Kokkos::create_mdarray< scalar_array_h >(region.num_nodes);
  scalar_array_h  elem_mass_h      =  Kokkos::create_mdarray< scalar_array_h >(region.num_elements);


  scalar_array_h  acceleration_h   =  Kokkos::create_mdarray< scalar_array_h >(region.num_nodes, 3);
  scalar_array_h  velocity_h     =   Kokkos::create_mdarray< scalar_array_h >(region.num_nodes, 3, 2); // two state field
  scalar_array_h  displacement_h   =  Kokkos::create_mdarray< scalar_array_h >(region.num_nodes, 3, 2); // two state field
  scalar_array_h  internal_force_h =  Kokkos::create_mdarray< scalar_array_h >(region.num_nodes, 3);
  scalar_array_h  stress_new_h     =  Kokkos::create_mdarray< scalar_array_h >(region.num_elements,6);

  perf.mesh_time = wall_clock.seconds(); // Mesh and graph allocation and population.

  //setup the initial condition on velocity
  {
    const unsigned X = 0;
    for (int inode = 0; inode< region.num_nodes; ++inode) {
      if ( region.model_coords(inode,X) == 0) {
        velocity_h(inode,X,0) = 1.0e3;
        velocity_h(inode,X,1) = 1.0e3;
      }
    }
  }

  Kokkos::deep_copy(region.velocity, velocity_h);

  // Parameters required for the internal force computations.


  //--------------------------------------------------------------------------
  // We will call a sequence of functions.  These functions have been
  // grouped into several functors to balance the number of global memory
  // accesses versus requiring too many registers or too much L1 cache.
  // Global memory accees have read/write cost and memory subsystem contention cost.
  //--------------------------------------------------------------------------

  Kokkos::parallel_for( region.num_elements,
      initialize_element<Scalar,device_type>(region)
      );

  Kokkos::parallel_for( region.num_nodes,
      initialize_node<Scalar,device_type>(region)
      );

  perf.init_time = wall_clock.seconds(); // Initialization

  int current_state = 0;
  int previous_state = 0;
  int next_state = 0;

  const int total_num_steps = 10000;

  for (int step = 0; step < total_num_steps; ++step) {

    //rotate the states
    previous_state = current_state;
    current_state = next_state;
    ++next_state;
    next_state %= NumStates;

    wall_clock.reset();

    // First kernel 'grad_hgop' combines three functions:
    // gradient, velocity gradient, and hour glass operator.
    Kokkos::parallel_for( region.num_elements ,
        grad_hgop<Scalar, device_type> ( region,
                                         current_state,
                                         previous_state
                                       ));

    // Combine tensor decomposition and rotation functions.
    Kokkos::parallel_for( region.num_elements ,
        decomp_rotate<Scalar, device_type> ( region,
                                             current_state,
                                             previous_state
                                           ));


    // Single beastly function in this last functor,
    // did not notice any opportunity for splitting.
    Kokkos::parallel_for( region.num_elements ,
        divergence<Scalar, device_type> ( region,
                                          user_dt,
                                          current_state,
                                          previous_state
                                        ));

    perf.internal_force_time += wall_clock.seconds();

    Kokkos::parallel_reduce( region.num_elements,
        minimum_stable_time_step<Scalar, device_type>( region),    //reduction op
        set_next_time_step<Scalar,device_type>(region,next_state)); //post process

    perf.minimum_stable_time_step += wall_clock.seconds();


    // Assembly of elements' contributions to nodal force into
    // a nodal force vector.  Update the accelerations, velocities,
    // displacements.
    // The same pattern can be used for matrix-free residual computations.
    Kokkos::parallel_for( region.num_nodes ,
        finish_step<Scalar, device_type>( region,
                                          ex,
                                          current_state,
                                          next_state
                                        ));
    perf.central_diff += wall_clock.seconds();

#ifdef KOKKOS_DEVICE_CUDA
    if (step%100 == 0 ) {
      Kokkos::deep_copy(acceleration_h,region.acceleration);
      Kokkos::deep_copy(velocity_h,region.velocity);
      Kokkos::deep_copy(displacement_h,region.displacement);
      Kokkos::deep_copy(internal_force_h,region.internal_force);
      Kokkos::deep_copy(stress_new_h,region.stress_new);
    }
#endif

    perf.copy_to_host_time += wall_clock.seconds();


  }

  return 0;
}


template <typename Scalar, typename Device>
static void driver( const char * label , int beg , int end , int runs )
{

  int shift = 20;

  std::cout << std::endl ;
  std::cout << "\"MiniExplicitDynamics with Kokkos " << label << "\"" << std::endl;
  std::cout << std::left << std::setw(shift) << "\"Size\" , ";
  std::cout << std::left << std::setw(shift) << "\"Setup\" , ";
  std::cout << std::left << std::setw(shift) << "\"Initialize\" , ";
  std::cout << std::left << std::setw(shift) << "\"InternalForce\" , ";
  std::cout << std::left << std::setw(shift) << "\"StableTimeStep\" , ";
  std::cout << std::left << std::setw(shift) << "\"CentralDiff\" , ";
  std::cout << std::left << std::setw(shift) << "\"CopyToHost\" , ";
  std::cout << std::left << std::setw(shift) << "\"TimePerElement\"";

  std::cout << std::endl;

  std::cout << std::left << std::setw(shift) << "\"elements\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec\" , ";
  std::cout << std::left << std::setw(shift) << "\"millisec/element\"";

  std::cout << std::endl;

  for(int i = beg ; i < end; ++i )
  {
    int two_to_the_i = 1 << i;
    int factor = static_cast<int>(cbrt(static_cast<double>(two_to_the_i)));

    const int ix = 10 * factor;
    const int iy = factor;
    const int iz = factor;
    const int n  = ix * iy * iz ;

    PerformanceData perf , best ;

    for(int j = 0; j < runs; j++){

     explicit_dynamics_app<Scalar,Device>(ix,iy,iz,perf);

     if( j == 0 ) {
       best = perf ;
     }
     else {
       best.best( perf );
     }
   }
   double time_per_element = (best.internal_force_time + best.minimum_stable_time_step + best.central_diff)/n;




   std::cout << std::setw(shift-3) << n << " , "
             << std::setw(shift-3) << best.mesh_time * 1000 << " , "
             << std::setw(shift-3) << best.init_time * 1000 << " , "
             << std::setw(shift-3) << best.internal_force_time * 1000 << " , "
             << std::setw(shift-3) << best.minimum_stable_time_step * 1000 << " , "
             << std::setw(shift-3) << best.central_diff * 1000 << " , "
             << std::setw(shift-3) << best.copy_to_host_time * 1000 << " , "
             << std::setw(shift) << time_per_element * 1000
             << std::endl ;
  }
}


} // namespace explicit_dynamics

#endif
