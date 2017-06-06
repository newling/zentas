#ifndef ZENTAS_VDIMAP_HPP
#define ZENTAS_VDIMAP_HPP

#include <vector>
#include <iostream>

namespace nszen{
namespace vdimap{


template <typename T>
void make_all_nperp(T* const evs, size_t dim, size_t neigs);
  
template <typename T>
void vdimap(std::vector<T> & v_mapped, const T * const ptr_datain, size_t ndata, size_t dimension, size_t seed);


}
}

#endif