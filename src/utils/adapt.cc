//
// adapt - read in one or more statistics files written by
//         from AdaptiveSampler::saveState() and perform one
//         round of adaptation to improve sampling statistics.
//         AdaptiverSampler::restoreState() can read the output
//         file generated by adapt and see improvements in its
//         sampling efficiency, provided that the distribution
//         being sampled has not changed.
//
// author: richard.t.jones at uconn.edu
// version: february 23, 2016
//
// see AdaptiveSampler.cc, .hh for more information.

#include <iostream>
#include <string>
#include <stdlib.h>

#include "AdaptiveSampler.hh"

#include <TRandom.h>

TRandom randoms(0);
void my_randoms(int n, double *u) { randoms.RndmArray(n,u); }

void usage() {
   std::cout << "Usage: adapt [options] <input1> [<input2> ...]" << std::endl
             << "  where options include" << std::endl
             << "     -o <output_file> : output filename [adapted.astate]" << std::endl
             << "     -t <threshold> : sampling threshold [1000]" << std::endl;
   exit(1);
}

int main(int argc, char **argv)
{
   int Ndim=0;
   double threshold=1000;
   std::string outfile("adapted.astate");
   AdaptiveSampler *sampler = 0;
   for (int iarg=1; iarg < argc; ++iarg) {
      char stropt[999] = "";
      int opt;
      if ((opt = sscanf(argv[iarg], "-o %s", stropt))) {
         if (opt == EOF)
            sscanf(argv[++iarg], "%s", stropt);
         outfile = stropt;
         continue;
      }
      else if ((opt = sscanf(argv[iarg], "-t %lf", &threshold))) {
         if (opt == EOF)
            sscanf(argv[++iarg], "%lf", &threshold);
         if (threshold > 0)
            continue;
         else
            usage();
      }
      else if (argv[iarg][0] == '-') {
         usage();
      }
      else if (Ndim == 0) {
         FILE *fin = fopen(argv[iarg], "r");
         if (fin) {
            if (fscanf(fin, "fNdim=%d", &Ndim) == 0) {
               std::cerr << "adapt - invalid data in input file "
                         << argv[iarg] << std::endl;
               usage();
            }
            sampler = new AdaptiveSampler(Ndim, my_randoms);
         }
         else {
            std::cerr << "adapt - error opening input file "
                      << argv[iarg] << std::endl;
            usage();
         }
      }
      //std::cout << "reading from " << argv[iarg] << std::endl;
      sampler->mergeState(argv[iarg]);
   }

   double error;
   double result = sampler->getResult(&error);
   if (result > 0)
      std::cout << "result = " << result << " +/- " << error << std::endl;
   else
      std::cout << "result unknown" << std::endl;
   sampler->setAdaptation_sampling_threshold(threshold);
   int Na = sampler->adapt();
   std::cout << "sampler.adapt() returns " << Na << std::endl;
   sampler->saveState(outfile);
   sampler->display_tree();
   return (Na == 0);
}