/*
 * STRUMPACK -- STRUctured Matrices PACKage, Copyright (c) 2014, The
 * Regents of the University of California, through Lawrence Berkeley
 * National Laboratory (subject to receipt of any required approvals
 * from the U.S. Dept. of Energy).  All rights reserved.
 *
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Technology Transfer
 * Department at TTD@lbl.gov.
 *
 * NOTICE. This software is owned by the U.S. Department of Energy. As
 * such, the U.S. Government has been granted for itself and others
 * acting on its behalf a paid-up, nonexclusive, irrevocable,
 * worldwide license in the Software to reproduce, prepare derivative
 * works, and perform publicly and display publicly.  Beginning five
 * (5) years after the date permission to assert copyright is obtained
 * from the U.S. Department of Energy, and subject to any subsequent
 * five (5) year renewals, the U.S. Government is granted for itself
 * and others acting on its behalf a paid-up, nonexclusive,
 * irrevocable, worldwide license in the Software to reproduce,
 * prepare derivative works, distribute copies to the public, perform
 * publicly and display publicly, and to permit others to do so.
 *
 * Developers: Pieter Ghysels, Francois-Henry Rouet, Xiaoye S. Li.
 *             (Lawrence Berkeley National Lab, Computational Research
 *             Division).
 *
 */
#ifndef FRONT_FACTORY_HPP
#define FRONT_FACTORY_HPP

#include <iostream>
#include <algorithm>
#include "CSRGraph.hpp"
#include "FrontalMatrixDense.hpp"
#include "FrontalMatrixHSS.hpp"
#include "FrontalMatrixBLR.hpp"
#if defined(STRUMPACK_USE_BPACK)
#include "FrontalMatrixHODLR.hpp"
#endif
#if defined(STRUMPACK_USE_MPI)
#include "FrontalMatrixDenseMPI.hpp"
#include "FrontalMatrixHSSMPI.hpp"
#include "FrontalMatrixBLRMPI.hpp"
#if defined(STRUMPACK_USE_BPACK)
#include "FrontalMatrixHODLRMPI.hpp"
#endif
#endif
#if defined(STRUMPACK_USE_CUBLAS)
#include "FrontalMatrixCUBLAS.hpp"
#endif
#if defined(STRUMPACK_USE_ZFP)
#include "FrontalMatrixLossy.hpp"
#endif

namespace strumpack {

  struct FrontCounter {
    int dense, HSS, BLR, HODLR, lossy;
    FrontCounter() : dense(0), HSS(0), BLR(0), HODLR(0), lossy(0) {}
    FrontCounter(int* c) :
      dense(c[0]), HSS(c[1]), BLR(c[2]), HODLR(c[3]), lossy(c[4]) {}
#if defined(STRUMPACK_USE_MPI)
    FrontCounter reduce(const MPIComm& comm) const {
      std::array<int,5> w = {dense, HSS, BLR, HODLR, lossy};
      comm.reduce(w.data(), w.size(), MPI_SUM);
      return FrontCounter(w.data());
    }
#endif
  };

  template<typename scalar_t> bool is_CUBLAS
  (const SPOptions<scalar_t>& opts) {
#if defined(STRUMPACK_USE_CUBLAS)
    return opts.use_gpu() && opts.compression() == CompressionType::NONE;
#else
    return false;
#endif
  }
  template<typename scalar_t> bool is_HSS
  (int dsep, int dupd, bool compressed_parent,
   const SPOptions<scalar_t>& opts) {
    return opts.compression() == CompressionType::HSS &&
      compressed_parent &&
      (dsep >= opts.compression_min_sep_size() ||
       dsep + dupd >= opts.compression_min_front_size());
  }
  template<typename scalar_t> bool is_BLR
  (int dsep, int dupd, bool compressed_parent,
   const SPOptions<scalar_t>& opts) {
    return opts.compression() == CompressionType::BLR &&
      (dsep >= opts.compression_min_sep_size() ||
       dsep + dupd >= opts.compression_min_front_size());
  }
  template<typename scalar_t> bool is_HODLR
  (int dsep, int dupd, bool compressed_parent,
   const SPOptions<scalar_t>& opts) {
#if defined(STRUMPACK_USE_BPACK)
    return opts.compression() == CompressionType::HODLR &&
      (dsep >= opts.compression_min_sep_size() ||
       dsep + dupd >= opts.compression_min_front_size());
#else
    return false;
#endif
  }
  template<typename scalar_t> bool is_lossy
  (int dsep, int dupd, bool, const SPOptions<scalar_t>& opts) {
#if defined(STRUMPACK_USE_ZFP)
    return opts.compression() == CompressionType::LOSSY &&
      (dsep >= opts.compression_min_sep_size() ||
       dsep + dupd >= opts.compression_min_front_size());
#else
    return false;
#endif
  }
  template<typename scalar_t> bool is_compressed
  (int dsep, int dupd, bool compressed_parent,
   const SPOptions<scalar_t>& opts) {
    return opts.compression() != CompressionType::NONE &&
      (is_HSS(dsep, dupd, compressed_parent, opts) ||
       is_BLR(dsep, dupd, compressed_parent, opts) ||
       is_HODLR(dsep, dupd, compressed_parent, opts) ||
       is_lossy(dsep, dupd, compressed_parent, opts));
  }

  template<typename scalar_t, typename integer_t>
  std::unique_ptr<FrontalMatrix<scalar_t,integer_t>> create_frontal_matrix
  (const SPOptions<scalar_t>& opts, integer_t s, integer_t sbegin,
   integer_t send, std::vector<integer_t>& upd, bool compressed_parent,
   int level, FrontCounter& fc, bool root=true) {
    auto dsep = send - sbegin;
    auto dupd = upd.size();
    std::unique_ptr<FrontalMatrix<scalar_t,integer_t>> front;
    switch (opts.compression()) {
    case CompressionType::NONE: {
      if (is_CUBLAS(opts)) {
#if defined(STRUMPACK_USE_CUBLAS)
        front.reset
          (new FrontalMatrixCUBLAS<scalar_t,integer_t>(s, sbegin, send, upd));
        if (root) fc.dense++;
#endif
      }
    } break;
    case CompressionType::HSS: {
      if (is_HSS(dsep, dupd, compressed_parent, opts)) {
        front.reset
          (new FrontalMatrixHSS<scalar_t,integer_t>(s, sbegin, send, upd));
        if (root) fc.HSS++;
      }
    } break;
    case CompressionType::BLR: {
      if (is_BLR(dsep, dupd, compressed_parent, opts)) {
        front.reset
          (new FrontalMatrixBLR<scalar_t,integer_t>(s, sbegin, send, upd));
        if (root) fc.BLR++;
      }
    } break;
    case CompressionType::HODLR: {
      if (is_HODLR(dsep, dupd, compressed_parent, opts)) {
#if defined(STRUMPACK_USE_BPACK)
        front.reset
          (new FrontalMatrixHODLR<scalar_t,integer_t>(s, sbegin, send, upd));
        if (root) fc.HODLR++;
#endif
      }
    } break;
    case CompressionType::LOSSY: {
      if (is_lossy(dsep, dupd, compressed_parent, opts)) {
#if defined(STRUMPACK_USE_ZFP)
        front.reset
          (new FrontalMatrixLossy<scalar_t,integer_t>(s, sbegin, send, upd));
        if (root) fc.lossy++;
#endif
      }
    }
    }
    if (!front) {
      front.reset
        (new FrontalMatrixDense<scalar_t,integer_t>(s, sbegin, send, upd));
      if (root) fc.dense++;
    }
    return front;
  }

#if defined(STRUMPACK_USE_MPI)
  template<typename scalar_t, typename integer_t>
  std::unique_ptr<FrontalMatrix<scalar_t,integer_t>> create_frontal_matrix
  (const SPOptions<scalar_t>& opts, integer_t s,
   integer_t sbegin, integer_t send, std::vector<integer_t>& upd,
   bool compressed_parent, int level, FrontCounter& fc,
   const MPIComm& comm, int P, bool root) {
    auto dsep = send - sbegin;
    auto dupd = upd.size();
    std::unique_ptr<FrontalMatrix<scalar_t,integer_t>> front;
    switch (opts.compression()) {
    case CompressionType::HSS: {
      if (is_HSS(dsep, dupd, compressed_parent, opts)) {
        using FHSSMPI_t = FrontalMatrixHSSMPI<scalar_t,integer_t>;
        front.reset(new FHSSMPI_t(s, sbegin, send, upd, comm, P));
        if (root) fc.HSS++;
      }
    } break;
    case CompressionType::BLR: {
      if (is_BLR(dsep, dupd, compressed_parent, opts)) {
        using FBLRMPI_t = FrontalMatrixBLRMPI<scalar_t,integer_t>;
        front.reset(new FBLRMPI_t(s, sbegin, send, upd, comm, P));
        if (root) fc.BLR++;
      }
    } break;
    case CompressionType::HODLR: {
      if (is_HODLR(dsep, dupd, compressed_parent, opts)) {
#if defined(STRUMPACK_USE_BPACK)
        using FHODLRMPI_t = FrontalMatrixHODLRMPI<scalar_t,integer_t>;
        front.reset(new FHODLRMPI_t(s, sbegin, send, upd, comm, P));
        if (root) fc.HODLR++;
#endif
      }
    } break;
      //case CompressionType::LOSSY: // TODO not implemented yet!
    }
    if (!front) {
      using FDMPI_t = FrontalMatrixDenseMPI<scalar_t,integer_t>;
      front.reset(new FDMPI_t(s, sbegin, send, upd, comm, P));
      if (root) fc.dense++;
    }

    // TODO add lossy/lossless compression!!

    return front;
  }
#endif

} // end namespace strumpack

#endif // FRONT_FACTORY_HPP
