# - Try to find SUITESPARSE
# Once done this will define
#  
#  SUITESPARSE_FOUND            - system has SUITESPARSE
#  SUITESPARSE_INCLUDE_DIRS     - the SUITESPARSE include directory
#  SUITESPARSE_LIBRARIES        - Link these to use SUITESPARSE
#  SUITESPARSE_SPQR_LIBRARY     - name of spqr library (necessary due to error in debian package)
#  SUITESPARSE_SPQR_LIBRARY_DIR - name of spqr library (necessary due to error in debian package)
#  SUITESPARSE_LIBRARY_DIR      - Library main directory containing suitesparse libs
#  SUITESPARSE_LIBRARY_DIRS     - all Library directories containing suitesparse libs
#  SUITESPARSE_SPQR_VALID       - automatic identification whether or not spqr package is installed correctly

IF (SUITESPARSE_INCLUDE_DIRS)
  # Already in cache, be silent
  SET(SUITESPARSE_FIND_QUIETLY TRUE)
ENDIF (SUITESPARSE_INCLUDE_DIRS)

if( WIN32 )
   # Find cholmod part of the suitesparse library collection

   FIND_PATH( CHOLMOD_INCLUDE_DIR cholmod.h
              PATHS "C:\\libs\\win32\\SuiteSparse\\Include"  )

   # Add cholmod include directory to collection include directories
   IF ( CHOLMOD_INCLUDE_DIR )
	list ( APPEND SUITESPARSE_INCLUDE_DIRS ${CHOLMOD_INCLUDE_DIR} )
   ENDIF( CHOLMOD_INCLUDE_DIR )


   # find path suitesparse library
   FIND_PATH( SUITESPARSE_LIBRARY_DIRS 
	         amd.lib
               PATHS "C:\\libs\\win32\\SuiteSparse\\libs" )

   # if we found the library, add it to the defined libraries
   IF ( SUITESPARSE_LIBRARY_DIRS )
	list ( APPEND SUITESPARSE_LIBRARIES optimized;amd;optimized;camd;optimized;ccolamd;optimized;cholmod;optimized;colamd;optimized;metis;optimized;spqr;optimized;umfpack;debug;amdd;debug;camdd;debug;ccolamdd;debug;cholmodd;debug;spqrd;debug;umfpackd;debug;colamdd;debug;metisd;optimized;blas;optimized;libf2c;optimized;lapack;debug;blasd;debug;libf2cd;debug;lapackd )
   ENDIF( SUITESPARSE_LIBRARY_DIRS )  

else( WIN32 )
   IF(APPLE)
	   FIND_PATH( CHOLMOD_INCLUDE_DIR cholmod.h
        	      PATHS /opt/local/include/ufsparse
			    /usr/local/include )

           FIND_PATH( SUITESPARSE_LIBRARY_DIR
                      NAMES libcholmod.a
                      PATHS /opt/local/lib
			    /usr/local/lib )
   ELSE(APPLE)
	   FIND_PATH( CHOLMOD_INCLUDE_DIR cholmod.h
        	      PATHS /usr/local/include 
        	            /usr/include 
        	            /usr/include/suitesparse/ 
		            ${CMAKE_PREFIX_PATH}/include
        	            ${CMAKE_SOURCE_DIR}/MacOS/Libs/cholmod
              	      PATH_SUFFIXES cholmod/ CHOLMOD/ )
   	
           FIND_PATH( SUITESPARSE_LIBRARY_DIR
                      NAMES libcholmod.so libcholmod.a
                      PATHS /usr/lib 
                            /usr/lib64
                            /usr/lib/x86_64-linux-gnu
                            /usr/lib/i386-linux-gnu
                            /usr/local/lib
                            /usr/lib/arm-linux-gnueabihf/
                            /usr/lib/aarch64-linux-gnu/
                            /usr/lib/arm-linux-gnueabi/
		            ${CMAKE_PREFIX_PATH}/lib/
                            /usr/lib/arm-linux-gnu)
   ENDIF(APPLE)

   # Add cholmod include directory to collection include directories
   IF ( CHOLMOD_INCLUDE_DIR )
	list ( APPEND SUITESPARSE_INCLUDE_DIRS ${CHOLMOD_INCLUDE_DIR} )
   ENDIF( CHOLMOD_INCLUDE_DIR )

   # if we found the library, add it to the defined libraries
   IF ( SUITESPARSE_LIBRARY_DIR )

       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libamd.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libbtf.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libcamd.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libccolamd.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libcholmod.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libcolamd.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libcsparse.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libklu.so)
       list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libumfpack.so)

       IF (APPLE)
           list ( APPEND SUITESPARSE_LIBRARIES suitesparseconfig)
       ENDIF (APPLE)

       # Metis and spqr are optional
       FIND_LIBRARY( SUITESPARSE_METIS_LIBRARY
                     NAMES metis
                     PATHS ${SUITESPARSE_LIBRARY_DIR} )
       IF (SUITESPARSE_METIS_LIBRARY)			
	  list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libmetis.so)
       ENDIF(SUITESPARSE_METIS_LIBRARY)

       if(EXISTS  "${CHOLMOD_INCLUDE_DIR}/SuiteSparseQR.hpp")
	  SET(SUITESPARSE_SPQR_VALID TRUE CACHE BOOL "SuiteSparseSPQR valid")
       else()
	  SET(SUITESPARSE_SPQR_VALID false CACHE BOOL "SuiteSparseSPQR valid")
       endif()

       if(SUITESPARSE_SPQR_VALID)
	  FIND_LIBRARY( SUITESPARSE_SPQR_LIBRARY
		      NAMES spqr
		      PATHS ${SUITESPARSE_LIBRARY_DIR} )
	  IF (SUITESPARSE_SPQR_LIBRARY)			
            list ( APPEND SUITESPARSE_LIBRARIES ${SUITESPARSE_LIBRARY_DIR}/libspqr.so)
	  ENDIF (SUITESPARSE_SPQR_LIBRARY)
       endif()
       
    ENDIF( SUITESPARSE_LIBRARY_DIR )  
   
endif( WIN32 )


IF (SUITESPARSE_INCLUDE_DIRS AND SUITESPARSE_LIBRARIES)
   IF(WIN32)
    list (APPEND SUITESPARSE_INCLUDE_DIRS ${CHOLMOD_INCLUDE_DIR}/../../UFconfig )
   ENDIF(WIN32)
   SET(SUITESPARSE_FOUND TRUE)
   MESSAGE(STATUS "Found SuiteSparse")
ELSE (SUITESPARSE_INCLUDE_DIRS AND SUITESPARSE_LIBRARIES)
   SET( SUITESPARSE_FOUND FALSE )
   MESSAGE(FATAL_ERROR "Unable to find SuiteSparse")
ENDIF (SUITESPARSE_INCLUDE_DIRS AND SUITESPARSE_LIBRARIES)

