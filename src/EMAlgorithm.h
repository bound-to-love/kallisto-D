#ifndef KALLISTO_EMALGORITHM_H
#define KALLISTO_EMALGORITHM_H

#include "common.h"
#include "KmerIndex.h"
#include "MinCollector.h"
#include "weights.h"

#include <algorithm>
#include <numeric>
#include <iostream>
#include <limits>
#include <vector>

// smallest weight we expect is ~10^-4
// on most machines, TOLERANCE should be 2.22045e-15
//const double TOLERANCE = std::numeric_limits<double>::epsilon() * 10;
//const double TOLERANCE = 1e-100;
const double TOLERANCE = std::numeric_limits<double>::denorm_min();

struct EMAlgorithm {
  // ecmap is the ecmap from KmerIndex
  // counts is vector from collector, with indices corresponding to ec ids
  // target_names is the target_names_ from collector
  // TODO: initialize alpha a bit more intelligently
  EMAlgorithm(const std::vector<uint32_t>& counts,
              const KmerIndex& index,
              const MinCollector& tc,
              const std::vector<double>& all_means,
              const ProgramOptions& opt) :
    index_(index),
    tc_(tc),
    num_trans_(index.target_names_.size()),
    ecmapinv_(index.ecmapinv),
    counts_(counts),
    target_names_(index.target_names_),
    post_bias_(4096,1.0),
    alpha_(num_trans_, 1.0/num_trans_), // uniform distribution over targets
    rho_(num_trans_, 0.0),
    rho_set_(false),
    all_fl_means(all_means),
    opt(opt)
  {
    assert(all_fl_means.size() == index_.target_lens_.size());  
    if (opt.long_read){
     assert(tc.flens_lr.size() == index_.target_lens_.size());
     eff_lens_.reserve(tc.flens_lr.size()); 
     double eff_len; 
     for (int i = 0; i < tc.flens_lr.size(); i++){
       if  (tc.flens_lr_c[i] < 0.000001){
         eff_len = 31; //(int)index_.target_lens_[i]; //if we are not seeing this transcript uniquely mapping at all then just use the length of the transcript 
       } else {
         eff_len = (double)index_.target_lens_[i] - std::fabs(double(tc.flens_lr[i]-31*tc.flens_lr_c[i])/double(tc.flens_lr_c[i]));
         //eff_len = 1; //std::fabs(30000 - std::fabs(double(tc.flens_lr[i])/double(tc.flens_lr_c[i])));
         // the rationale for this is that in sequencing the longer transcripts of a given gene may be less likely to be fully sequenced vs shorter ones of the same gene, so the longer one may look like the shorter one.  
         if (eff_len > 1.0) {
           //std::cerr << "target length: " << index_.target_lens_[i] << " flens_lr: " << tc.flens_lr[i] << " flens_lr_c: " << tc.flens_lr_c[i] << std::endl; std::cerr.flush(); 
           eff_len = 31; //1; //if the average length of a read mapping within a transcript is within 1 of the target length then the average number of positions that the read could begin at is the first.  
         } 
       }
       eff_lens_.push_back(eff_len);
     }
   }
   else {
     eff_lens_ = calc_eff_lens(index_.target_lens_, all_fl_means);
   }
    weight_map_ = calc_weights(tc_.counts, ecmapinv_, eff_lens_);
    assert(target_names_.size() == eff_lens_.size());
  }

  ~EMAlgorithm() {}

  void run(size_t n_iter = 10000, size_t min_rounds=50, bool verbose = true, bool recomputeEffLen = true) {
    std::vector<double> next_alpha(alpha_.size(), 0.0);

    assert(weight_map_.size() <= counts_.size());

    double denom;
    const double alpha_limit = 1e-7;
    const double alpha_change_limit = 1e-2;
    const double alpha_change = 1e-2;
    bool finalRound = false;

    if (verbose) {
      std::cerr << "[   em] quantifying the abundances ..."; std::cerr.flush();
    }

    int i;
    for (i = 0; i < n_iter; ++i) {
      if (recomputeEffLen && (i == min_rounds || i == min_rounds + 500) && !opt.long_read) {
        eff_lens_ = update_eff_lens(all_fl_means, tc_, index_, alpha_, eff_lens_, post_bias_, opt);
        weight_map_ = calc_weights (tc_.counts, index_.ecmapinv, eff_lens_);
      }
      
      if (recomputeEffLen && (i == min_rounds || i % min_rounds == 0) && opt.long_read) {
        weight_map_ = calc_weights (tc_.counts, index_.ecmapinv, eff_lens_);
      }


      for (const auto& it : ecmapinv_) {
        if (it.first.cardinality() == 1) {
          next_alpha[it.first.maximum()] = counts_[it.second];
        }
      }

      for (const auto& it : ecmapinv_) {
        if (it.first.cardinality() == 1) { // Individual transcript
          continue;
        }

        denom = 0.0;

        if (counts_[it.second] == 0) {
          continue;
        }

        // first, compute the denominator: a normalizer
        // iterate over targets in EC map
        auto& wv = weight_map_[it.second];

        // everything in ecmap should be in weight_map
        //assert( w_search != weight_map_.end() );
        //assert( w_search->second.size() == ec_kv.second.size() );

        // wv is weights vector
        // trs is a vector of transcript ids

        auto& r = it.first; //ecmap_.find(ec)->second;
        auto numEC = r.cardinality();
        uint32_t* trs = new uint32_t[numEC];
        r.toUint32Array(trs);

        for (auto t_it = 0; t_it < numEC; ++t_it) {
          denom += alpha_[trs[t_it]] * wv[t_it];
        }

        if (denom < TOLERANCE) {
          continue;
        }

        // compute the update step
        auto countNorm = counts_[it.second] / denom;
        for (auto t_it = 0; t_it < numEC; ++t_it) {
          next_alpha[trs[t_it]] += (wv[t_it] * alpha_[trs[t_it]]) * countNorm;
        }

        delete[] trs;
        trs = nullptr;

      }

      // TODO: check for relative difference for convergence in EM

      bool stopEM = false; //!finalRound && (i >= min_rounds); // false initially
      //double maxChange = 0.0;
      int chcount = 0;
      for (int ec = 0; ec < num_trans_; ec++) {
        if (next_alpha[ec] > alpha_change_limit && (std::fabs(next_alpha[ec] - alpha_[ec]) / next_alpha[ec]) > alpha_change) {
          chcount++;
        }

        //if (stopEM && next_alpha[ec] >= alpha_limit) {

          /* double reldiff = abs(next_alpha[ec]-alpha_[ec]) / next_alpha[ec];
          if (reldiff >= alpha_change) {
            stopEM = false;
            }*/
        //}

        /*
        if (next_alpha[ec] > alpha_limit) {
          maxChange = std::max(maxChange,std::fabs(next_alpha[ec]-alpha_[ec]) / next_alpha[ec]);
        }
        */
        // reassign alpha_ to next_alpha
        alpha_[ec] = next_alpha[ec];

        // clear all next_alpha values 0 for next iteration
        next_alpha[ec] = 0.0;
      }

      //std::cout << chcount << std::endl;
      if (chcount == 0 && i > min_rounds) {

        stopEM=true;
      }

      if (finalRound) {
        break;
      }

      // std::cout << maxChange << std::endl;
      if (stopEM) {
        finalRound = true;
        alpha_before_zeroes_.resize( alpha_.size() );
        for (int ec = 0; ec < num_trans_; ec++) {
          alpha_before_zeroes_[ec] = alpha_[ec];
          if (alpha_[ec] < alpha_limit/10.0) {
            alpha_[ec] = 0.0;
          }
        }
      }

    }

    // ran for the maximum number of iterations
    if (n_iter == i) {
      alpha_before_zeroes_.resize( alpha_.size() );
      for (int ec = 0; ec < num_trans_; ec++) {
        alpha_before_zeroes_[ec] = alpha_[ec];
      }
    }

    if (verbose) {
      std::cerr << " done" << std::endl;
      std::cerr << "[   em] the Expectation-Maximization algorithm ran for "
        << pretty_num(i) << " rounds";
      std::cerr << std::endl;
      std::cerr.flush();
    }

  }

  void compute_rho() {
    if (rho_set_) {
      // rho has already been set, let's clear it
      std::fill(rho_.begin(), rho_.end(), 0.0);
    }

    double total {0.0};
    for (auto i = 0; i < alpha_.size(); ++i) {
      if (eff_lens_[i] < TOLERANCE) {
        std::cerr << "Should actually never really get here... tid: "  << i <<
            std::endl;
        continue;
      }
      rho_[i] = alpha_[i] / eff_lens_[i];
      total += rho_[i];
    }

    for (auto& r : rho_) {
      r /= total;
    }

    rho_set_ = true;
  }

  // DEPRECATED:
  void write(const std::string& out_fname) const {
    std::ofstream out;
    out.open(out_fname, std::ios::out);

    if (!out.is_open()) {
      std::cerr << "Error opening '" << out_fname << "'" <<
          std::endl;
      exit(1);
    }

    out.precision(15);

    out <<
        "target_id" << "\t" <<
        "kallisto_id" << "\t" <<
        "rho" << "\t" <<
        "tpm" << "\t" <<
        "est_counts" <<
        std::endl;

    const double MILLION = 1e6;

    for (auto i = 0; i < rho_.size(); ++i) {
      out <<
          target_names_[i] << "\t" <<
          i << "\t" <<
          rho_[i] << "\t" <<
          rho_[i] * MILLION << "\t" <<
          alpha_[i] <<
          std::endl;
    }

    out.flush();
    out.close();
  }

  void set_start(const EMAlgorithm& em_start) {
    assert(em_start.alpha_before_zeroes_.size() == alpha_.size());
    double big = 1.0;
    double sum_counts = std::accumulate(counts_.begin(), counts_.end(), 0.0);
    double sum_big = 0.0;
    int count_big = 0;
    for (auto x : em_start.alpha_before_zeroes_) {
      if (x >= big) {
        sum_big += x;
        count_big++;
      }
    }
    int n = alpha_.size();
    for (auto i = 0; i < n; i++) {
      if (em_start.alpha_before_zeroes_[i] >= big) {
        alpha_[i] = em_start.alpha_before_zeroes_[i];
      } else {
        alpha_[i] = sum_counts/(n - count_big);
      }
    }

    //std::cout << sum_big << " " << count_big << " " << n << std::endl;

    std::copy(em_start.alpha_before_zeroes_.begin(), em_start.alpha_before_zeroes_.end(),
        alpha_.begin());
  }


  int num_trans_;
  const KmerIndex& index_;
  const MinCollector& tc_;
  const EcMapInv& ecmapinv_;
  const std::vector<uint32_t>& counts_;
  const std::vector<std::string>& target_names_;
  const std::vector<double>& all_fl_means;
  std::vector<double> eff_lens_;
  std::vector<double> post_bias_;
  WeightMap weight_map_;
  std::vector<double> alpha_;
  std::vector<double> alpha_before_zeroes_;
  std::vector<double> rho_;
  bool rho_set_;
  const ProgramOptions& opt;
};


#endif // KALLISTO_EMALGORITHM_H
