# ZENTAS

A C++ and (optional) Python tool for partitional clustering around centers. Optimised and versatile implementations of K-Medoids and K-Means for various data types. More information at [arXiv 1609.04723](https://arxiv.org/abs/1609.04723). 



### K-Medoids a.k.a. K-Centers

Given *N* elements *x(1)...x(N)*, select *K* elements indexed by *c(1)...c(K)*, to minimise  *sum(i=1...N) min(k=1...K) E(distance (x(i), x(c(k))))* where *distance* is a valid distance and *E* is a non-decreasing function with *E(0) = 0*.

*distance* options are  
  * for sparse and dense vectors : l-0, l-1, l-2, l-infinity
  * for sequence data : Levenshtein and Normalised Levenshtein.
  
  
Energy *E* options are  
  * identity, quadratic, cubic, square-potential, exponential, and logarithmic.

### K-Means for dense and sparse vector data  

   * minimise sum of squares of l2 distances to cluster mean   
   * minimise sum of l1 distances to cluster dimension-wise median


## PREREQUISITES

* CMake
* for the Python library: Cython and Python


## CONFIGURE WITH CMAKE


Create a build directory:
```
mkdir build; cd build;
```

If you do NOT want the Python library, 

```
cmake -DBUILD_PYTHON_LIB=NO ..
```

If you do want the Python library, 


```
cmake ..
```

## BUILD

The library can be built, from the `build` directory 

```
make -j5
```

The shared library should now be in ./build/zentas (libzentas.so in Linux) and the Python shared library in ./build/python (pyzentas.so in Linux). These can be moved/copied elsewhere manually, there is currently no install option for zentas.


## USING

Example use cases of the C++ library and headers are in testsexamples, with the corresponding executables in build/testsexamples. There is an example of clustering dense vectors (exdense.cpp), sparse vectors (exsparse.cpp), and sequences (exwords.cpp). 

To use the Python library, make sure pyzentas.so is on PYTHONPATH, for example you can use `sys.path.append(/path/to/pyzentas.so)`. Examples using pyzentas are in python/examples.py.  More information can be obtained from the doc strings, try 
```
import pyzentas
help(pyzentas)
``` 


## Doesn't work, or missing a feature?

Please raise an issue in the zentas repository

