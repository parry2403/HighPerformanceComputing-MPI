/**
 * @file    main.cpp
 * @author  Patrick Flick <patrick.flick@gmail.com>
 * @brief   Implements the main function for the 'jacobi' executable.
 *
 * Copyright (c) 2014 Georgia Institute of Technology. All Rights Reserved.
 */

/*********************************************************************
 *                  !!  DO NOT CHANGE THIS FILE  !!                  *
 *********************************************************************/

#include <mpi.h>

#include <stdexcept>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <string>
#include <sstream>

#include "jacobi.h"
#include "mpi_jacobi.h"
#include "utils.h"
#include "io.h"

#define IS_MAC 1 // 0 for linux, 1 for mac

#if IS_MAC
// ---------------------------- OSX
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif


void current_utc_time(struct timespec *ts) {
	
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
	clock_serv_t cclock;
	mach_timespec_t mts;
	host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
	ts->tv_sec = mts.tv_sec;
	ts->tv_nsec = mts.tv_nsec;
#else
	clock_gettime(SYSTEM_CLOCK, ts);
#endif
	
}
// ------------------------------------
#endif


void print_usage()
{
    std::cerr << "Usage: ./jacobi <input_A> <input_b> <output_x>" << std::endl;
    std::cerr << "                  Reads the input A and b from the given binary files and" << std::endl;
    std::cerr << "                  writes the result to the given file in binary format." << std::endl;
    std::cerr << "       ./jacobi -n <n> [-d <difficulty>]" << std::endl;
    std::cerr << "                  Creates random input of size <n> (A has size n-by-n) of" << std::endl;
    std::cerr << "                  the given difficulty, a value between 0.0 (easiest) and 1.0" << std::endl;
    std::cerr << "                  (optional, default = 0.5)." << std::endl;
}

int main(int argc, char *argv[])
{
   // set up MPI
   MPI_Init(&argc, &argv);

   // get communicator size and my rank
   MPI_Comm comm = MPI_COMM_WORLD;
   int p, rank;
   MPI_Comm_size(comm, &p);
   MPI_Comm_rank(comm, &rank);

   // Ax = b
   std::vector<double> A;
   std::vector<double> b;
   std::vector<double> x;

   bool write_output = false;
   std::string outfile_name;

   int n;
   if (rank == 0)
   {
      if (argc < 3)
      {
         print_usage();
         exit(EXIT_FAILURE);
      }

      if (std::string(argv[1]) == "-n")
      {
         // randomly generate input
         n = atoi(argv[2]);
         if (!(n > 0))
         {
            print_usage();
            exit(EXIT_FAILURE);
         }

         double difficulty = 0.5;
         if (argc == 5)
         {
            if (std::string(argv[3]) != "-d")
            {
               print_usage();
               exit(EXIT_FAILURE);
            }
            std::istringstream iss(argv[4]);
            iss >> difficulty;
         }

         // generate random input of the given size
         A = diag_dom_rand(n, difficulty);
         b = randn(n);
      }
      else
      {

         if (argc != 4)
         {
            print_usage();
            exit(EXIT_FAILURE);
         }

         // get output filename
         outfile_name = std::string(argv[3]);
         write_output = true;

         // read input from file
         A = read_binary_file<double>(argv[1]);
         b = read_binary_file<double>(argv[2]);
         n = b.size();


         if (A.size() != n*(size_t)n)
         {
            throw std::runtime_error("The input dimensions are not matching");
         }
      }
   }

   // start timer
   //   we omit the file loading and argument parsing from the runtime
   //   timings, we measure the time needed by the processor with rank 0
   struct timespec t_start, t_end;
	if (rank == 0) {
	  #if IS_MAC
	  current_utc_time(&t_start);
      #else
	  clock_gettime(CLOCK_MONOTONIC,  &t_start);
      #endif
	}
   if (p > 1)
   {
      MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

      // get the dimensions
      int q = (int)sqrt(p);
      if (p != q*q)
      {
         throw std::runtime_error("The number of MPI processes must be a perfect square");
      }

      // create cartesian grid for the processors
      MPI_Comm grid_comm;
      int dims[2] = {q, q};
      int periods[2] = {0, 0};
      MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &grid_comm);

      // allocate output and run the parallel jacobi implementation
      if (rank == 0)
         x = std::vector<double>(n);
     //   for (int i=0;i<A.size();i++)
     //  {
      
     //       std::cerr<<A[i]<<std::endl;
     // }
      mpi_jacobi(n, &A[0], &b[0], &x[0], grid_comm);
   }
   else
   {
      std::cerr << "[WARNING]: Running the sequential solver. Start with mpirun to execute the parallel version." << std::endl;

      // sequential jacobi
      x = std::vector<double>(n);
    //   for (int i=0;i<A.size();i++)
    // {
      
    //       std::cerr<<A[i]<<std::endl;
    // }
      jacobi(n, &A[0], &b[0], &x[0]);
   }

   if (rank == 0)
   {
      // end timer
	  #if IS_MAC
	  current_utc_time(&t_end);
	  #else
	  clock_gettime(CLOCK_MONOTONIC,  &t_end);
	  #endif
      // time in seconds
      double time_secs = (t_end.tv_sec - t_start.tv_sec)
         + (double) (t_end.tv_nsec - t_start.tv_nsec) * 1e-9;
      // output time
      std::cerr << time_secs << std::endl;

      // write output
      if (write_output)
      {
         write_binary_file(outfile_name.c_str(), x);
      }
   }


   // finalize MPI
   MPI_Finalize();
   return 0;
}
