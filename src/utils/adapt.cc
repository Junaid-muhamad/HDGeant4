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
             << "     -t <threshold> : sampling threshold (%) [1]" << std::endl
             << "     -v <verbosity> : verbosity level [3]" << std::endl
             << "     -c <count> : internal generator check [0]" << std::endl
             << "     -s : just report statistics, no adaption" << std::endl;
   exit(1);
}

int main(int argc, char **argv)
{
   int Ndim=0;
   int Nfixed=0;
   int do_adaptation=1;
   double threshold=0.01;
   int verbosity_level=1;
   long int internal_check_count = 0;
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
         if (threshold > 0) {
            threshold *= 0.01;
            continue;
         }
         else
            usage();
      }
      else if ((opt = sscanf(argv[iarg], "-v %d", &verbosity_level))) {
         if (opt == EOF)
            sscanf(argv[++iarg], "%d", &verbosity_level);
         AdaptiveSampler::setVerbosity(verbosity_level);
         continue;
      }
      else if ((opt = sscanf(argv[iarg], "-c %ld", &internal_check_count))) {
         if (opt == EOF)
            sscanf(argv[++iarg], "%ld", &internal_check_count);
         continue;
      }
      else if (strncmp(argv[iarg], "-s", 2) == 0) {
         do_adaptation = 0;
         continue;
      }
      else if (argv[iarg][0] == '-') {
         usage();
      }
      else if (Ndim == 0) {
         FILE *fin = fopen(argv[iarg], "r");
         if (fin) {
            if (fscanf(fin, "fNdim=%d\n", &Ndim) == 0) {
               std::cerr << "adapt - invalid data in input file "
                         << argv[iarg] << std::endl;
               usage();
            }
            else if (fscanf(fin, "fNfixed=%d\n", &Nfixed) == 0) {
               std::cerr << "adapt - invalid data in input file "
                         << argv[iarg] << std::endl;
               usage();
            }
            sampler = new AdaptiveSampler(Ndim, my_randoms, Nfixed);
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
   if (sampler == 0 || sampler->getNdim() == 0)
      usage();

   if (internal_check_count > 0) {
      sampler->reset_stats();
      sampler->check_subsets();
      int nfixed = sampler->getNfixed();
      double *u = new double[Ndim];
      for (int i=0; i < internal_check_count; ++i) {
         for (int j=0; j < nfixed; ++j) {
            u[j] = random() / (RAND_MAX + 0.1);
         }
         double wgt = sampler->sample(u);
         sampler->feedback(u, wgt);
      }
      delete [] u;
   }

   if (verbosity_level > 0)
      std::cout << "sample size N = " << sampler->getNsample() << std::endl;

   double error;
   double error_uncertainty;
   double efficiency = sampler->getEfficiency();
   double result = sampler->getResult(&error, &error_uncertainty);
   if (result > 0) {
      if (verbosity_level > 0)
         std::cout << "result = " << result << " +/- " << error
                   << " +/- " << error_uncertainty 
                   << ", efficiency = " << efficiency
                   << std::endl;
   }
   else {
      if (verbosity_level > 0)
         std::cout << "result unknown" << std::endl;
   }

   if (verbosity_level > 0) {
      int warnings = sampler->check_subsets();
      if (warnings > 0) {
         std::cout << warnings << " warnings from check_subsets,"
                   << " there seem to be problems with this tree!"
                   << std::endl;
      }
   }

   int Na = 0;
   if (do_adaptation) {
      sampler->setAdaptation_sampling_threshold(threshold);
      Na = sampler->adapt();
      if (verbosity_level > 0)
         std::cout << "sampler.adapt() returns " << Na << std::endl;
      double new_error;
      double new_error_uncertainty;
      double new_result = sampler->getReweighted(&new_error, &new_error_uncertainty);
      double new_efficiency = sampler->getEfficiency(true);
      if (verbosity_level > 0)
         std::cout << "improved result = " << new_result << " +/- "
                   << new_error << " +/- " << new_error_uncertainty
                   << ", efficiency = " << new_efficiency
                   << std::endl;
   }
   sampler->saveState(outfile, do_adaptation);
   if (verbosity_level > 2)
      sampler->display_tree(do_adaptation);
   return (Na == 0);
}
