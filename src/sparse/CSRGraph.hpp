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
#ifndef CSRGRAPH_HPP
#define CSRGRAPH_HPP
#include <sstream>
#include <fstream>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <queue>

#include "misc/MPIWrapper.hpp"
#include "HSS/HSSPartitionTree.hpp"
#include "MetisReordering.hpp"

namespace strumpack {

  template<class integer_t>
  using Length2Edges = std::unordered_map<integer_t, std::vector<integer_t>>;

  /**
   * Compressed sparse row representation of a graph.
   *
   * This is also used as to store the local part of a 1-D block-row
   * distributed graph. So the column indices might not map to valid
   * nodes in this graph, ie, this graph contains outgoing edges to
   * nodes not part of this graph.
   */
  template<typename integer_t> class CSRGraph {
  public:
    CSRGraph() = default;
    CSRGraph(integer_t nr_vert, integer_t nr_edge);
    CSRGraph(std::vector<integer_t>&& ptr, std::vector<integer_t>&& ind);

    static CSRGraph<integer_t> deserialize(const std::vector<integer_t>& buf);
    static CSRGraph<integer_t> deserialize(const integer_t* buf);

    std::vector<integer_t> serialize() const;

    void print();
    integer_t size() const { return ptr_.size()-1; }
    integer_t vertices() const { return ptr_.size()-1; }
    integer_t edges() const { return ind_.size(); }

    const integer_t* ptr() const { return ptr_.data(); }
    const integer_t* ind() const { return ind_.data(); }
    integer_t* ptr() { return ptr_.data(); }
    integer_t* ind() { return ind_.data(); }
    const integer_t& ptr(integer_t i) const { assert(i <= vertices()); return ptr_[i]; }
    const integer_t& ind(integer_t i) const { assert(i < edges()); return ind_[i]; }
    integer_t& ptr(integer_t i) { assert(i <= vertices()); return ptr_[i]; }
    integer_t& ind(integer_t i) { assert(i < edges()); return ind_[i]; }

    void sort_rows();

    void permute(const integer_t* order, const integer_t* iorder);

    void permute_local
    (const std::vector<integer_t>& order,
     const std::vector<integer_t>& iorder,
     integer_t clo, integer_t chi);

    void permute_rows_local_cols_global
    (const std::vector<integer_t>& order,
     const std::vector<integer_t>& iorder,
     integer_t clo, integer_t chi);

    HSS::HSSPartitionTree recursive_bisection
    (int leaf, int conn_level, integer_t* order, integer_t* iorder,
     integer_t lo, integer_t sep_begin, integer_t sep_end) const;

    template<typename int_t>
    DenseMatrix<bool> admissibility(const std::vector<int_t>& tiles) const;

    void print_dense(const std::string& name) const;

  private:
    std::vector<integer_t> ptr_, ind_;

    Length2Edges<integer_t> length_2_edges(integer_t lo) const;

    void split_recursive
    (int leaf, int conn_level, integer_t lo,
     integer_t sep_begin, integer_t sep_end, integer_t* order,
     HSS::HSSPartitionTree& tree, integer_t& parts, integer_t part,
     integer_t count, const Length2Edges<integer_t>& l2) const;

    /**
     * Extract the separator from sep_begin to sep_end. Also add extra
     * length-2 edges if sep_order_level > 0.
     *
     * This only extracts the nodes i for which order[i] == part.
     */
    CSRGraph<integer_t> extract_subgraph
    (int order_level, integer_t lo, integer_t begin, integer_t end,
     integer_t part, const integer_t* order,
     const Length2Edges<integer_t>& o) const;
  };

  template<typename integer_t>
  CSRGraph<integer_t>::CSRGraph(integer_t nr_vert, integer_t nr_edge)
    : ptr_(nr_vert+1), ind_(nr_edge) {
  }

  template<typename integer_t>
  CSRGraph<integer_t>::CSRGraph
  (std::vector<integer_t>&& ptr, std::vector<integer_t>&& ind)
    : ptr_(std::move(ptr)), ind_(std::move(ind)) {
  }

  template<typename integer_t> CSRGraph<integer_t>
  CSRGraph<integer_t>::deserialize(const std::vector<integer_t>& buf) {
    return CSRGraph<integer_t>::deserialize(buf.data());
  }

  template<typename integer_t> CSRGraph<integer_t>
  CSRGraph<integer_t>::deserialize(const integer_t* buf) {
    CSRGraph<integer_t> g(buf[0]-1, buf[1]);
    std::copy(buf+2, buf+2+buf[0], g.ptr_.begin());
    std::copy(buf+2+buf[0], buf+2+buf[0]+buf[1], g.ind_.begin());
    return g;
  }

  template<typename integer_t> std::vector<integer_t>
  CSRGraph<integer_t>::serialize() const {
    std::vector<integer_t> buf(2+ptr_.size()+ind_.size());
    buf[0] = ptr_.size();
    buf[1] = ind_.size();
    std::copy(ptr_.begin(), ptr_.end(), buf.begin()+2);
    std::copy(ind_.begin(), ind_.end(), buf.begin()+2+ptr_.size());
    return buf;
  }

  template<typename integer_t> void
  CSRGraph<integer_t>::sort_rows() {
    auto n = size();
#pragma omp parallel for
    for (integer_t r=0; r<n; r++)
      std::sort(&ind_[ptr_[r]], &ind_[ptr_[r+1]]);
  }

  template<typename integer_t> void
  CSRGraph<integer_t>::print() {
    std::cout << "n=" << size() << " ";
    for (integer_t i=0; i<size(); i++)
      if (ptr_[i+1] != ptr_[i]) {
        std::cout << "r=" << i << ", ";
        for (integer_t j=ptr_[i]; j<ptr_[i+1]; j++)
          std::cout << ind_[j] << " ";
        std::cout << "| ";
      }
    std::cout << std::endl;
  }

  /**
   * order and iorder are of size this->size() == chi-clo.
   * order has elements in the range [clo, chi).
   * iorder had elements in the range [0, chi-clo).
   *
   * This applies permutation only in the diagonal block. This is
   * useful for the local subgraphs. The off-diagonal elements
   * represent the connections to the distributed separators, which
   * are not permuted, so this permutation can be done completely
   * local!
   */
  template<typename integer_t> void
  CSRGraph<integer_t>::permute_local
  (const std::vector<integer_t>& order, const std::vector<integer_t>& iorder,
   integer_t clo, integer_t chi) {
    std::vector<integer_t> ptr(ptr_.size()), ind(ind_.size());
    integer_t nnz = 0;
    auto n = size();
    for (integer_t i=0; i<n; i++) {
      auto lb = ptr_[iorder[i]];
      auto ub = ptr_[iorder[i]+1];
      for (integer_t j=lb; j<ub; j++) {
        auto c = ind_[j];
        ind[nnz++] = (c >= clo && c < chi) ? order[c-clo] : c;
      }
      ptr[i+1] = nnz;
    }
    std::swap(ptr_, ptr);
    std::swap(ind_, ind);
  }

  template<typename integer_t> void CSRGraph<integer_t>::permute
  (const integer_t* order, const integer_t* iorder) {
    std::vector<integer_t> ptr(ptr_.size()), ind(ind_.size());
    integer_t nnz = 0;
    auto n = size();
    for (integer_t i=0; i<n; i++) {
      auto ub = ptr_[iorder[i]+1];
      for (integer_t j=ptr_[iorder[i]]; j<ub; j++)
        ind[nnz++] = order[ind_[j]];
      ptr[i+1] = nnz;
    }
    std::swap(ptr_, ptr);
    std::swap(ind_, ind);
  }

  /**
   * iorder is of size this->size() == this->vertices().
   * iorder had elements in the range [0, chi-clo).
   * order is of the global size.
   */
  template<typename integer_t> void
  CSRGraph<integer_t>::permute_rows_local_cols_global
  (const std::vector<integer_t>& order,
   const std::vector<integer_t>& iorder,
   integer_t clo, integer_t chi) {
    std::vector<integer_t> ptr(ptr_.size()), ind(ind_.size());
    integer_t nnz = 0;
    auto n = size();
    ptr[0] = 0;
    for (integer_t i=0; i<n; i++) {
      auto ub = ptr_[iorder[i]+1];
      for (integer_t j=ptr_[iorder[i]]; j<ub; j++)
        ind[nnz++] = order[ind_[j]];
      ptr[i+1] = nnz;
    }
    std::swap(ptr_, ptr);
    std::swap(ind_, ind);
  }

  template<typename integer_t> HSS::HSSPartitionTree
  CSRGraph<integer_t>::recursive_bisection
  (int leaf, int conn_level, integer_t* order, integer_t* iorder,
   integer_t lo, integer_t sep_begin, integer_t sep_end) const {
    integer_t dim_sep = sep_end - sep_begin;
    HSS::HSSPartitionTree tree(dim_sep);
    if (dim_sep > 2 * leaf) {
      std::fill(&order[sep_begin], &order[sep_end], integer_t(0));
      integer_t parts = 0;
      Length2Edges<integer_t> l2;
      if (conn_level == 1) l2 = length_2_edges(lo);
      split_recursive
        (leaf, conn_level, lo, sep_begin, sep_end, order,
         tree, parts, 0, 1, l2);
      for (integer_t part=0, count=sep_begin+lo;
           part<parts; part++)
        for (integer_t i=sep_begin; i<sep_end; i++)
          if (order[i] == part)
            order[i] = -count++;
      for (integer_t i=sep_begin; i<sep_end; i++)
        order[i] = -order[i];
    } else
      std::iota(order+sep_begin, order+sep_end, sep_begin+lo);
    if (iorder)
      for (integer_t i=sep_begin; i<sep_end; i++)
        iorder[order[i]-lo] = i;
    return tree;
  }

  template<typename integer_t> void CSRGraph<integer_t>::split_recursive
  (int leaf, int conn_level, integer_t lo,
   integer_t sep_begin, integer_t sep_end, integer_t* order,
   HSS::HSSPartitionTree& tree, integer_t& parts, integer_t part,
   integer_t count, const Length2Edges<integer_t>& l2) const {
    auto sg = extract_subgraph
      (conn_level, lo, sep_begin, sep_end, part, order+sep_begin, l2);
    idx_t edge_cut = 0, nvtxs = sg.size();
    std::vector<idx_t> partitioning(nvtxs);
    int info = WRAPPER_METIS_PartGraphRecursive
      (nvtxs, 1, sg.ptr(), sg.ind(), 2, edge_cut, partitioning);
    if (info != METIS_OK) {
      std::cerr << "METIS_PartGraphRecursive for separator"
        " reordering returned: " << info << std::endl;
      exit(1);
    }
    tree.c.resize(2);
    for (integer_t i=sep_begin, j=0; i<sep_end; i++)
      if (order[i] == part) {
        auto p = partitioning[j++];
        order[i] = -count - p;
        tree.c[p].size++;
      }
    for (integer_t p=0; p<2; p++) {
      if (tree.c[p].size > 2 * leaf)
        split_recursive
          (leaf, conn_level, lo, sep_begin, sep_end, order,
           tree.c[p], parts, -count-p, count+2, l2);
      else
        std::replace(order+sep_begin, order+sep_end, -count-p, parts++);
    }
  }

  template<typename integer_t> Length2Edges<integer_t>
  CSRGraph<integer_t>::length_2_edges(integer_t lo) const {
    // for all nodes not in this graph to which there is an outgoing
    //   edge, we keep a list of edges to/from that node
    Length2Edges<integer_t> l2;
    auto n = size();
    auto hi = lo + n;
    for (integer_t r=0; r<n; r++)
      for (integer_t j=ptr_[r]; j<ptr_[r+1]; j++) {
        auto c = ind_[j];
        if (c < lo || c >= hi) l2[c].push_back(r);
      }
    return l2;
  }

  template<typename integer_t> CSRGraph<integer_t>
  CSRGraph<integer_t>::extract_subgraph
  (int order_level, integer_t lo, integer_t begin, integer_t end,
   integer_t part, const integer_t* order,
   const Length2Edges<integer_t>& l2) const {
    CSRGraph<integer_t> g;
    assert(order_level == 0 || order_level == 1);
    auto n = size();
    auto dim = end - begin;
    std::vector<bool> mark(dim);
    std::unique_ptr<integer_t[]> ind_to_part(new integer_t[dim]);
    integer_t count = 0;
    for (integer_t r=0; r<dim; r++)
      ind_to_part[r] = (order[r] == part) ? count++ : -1;
    g.ptr_.reserve(count+1);
    for (integer_t r=begin, edges=0; r<end; r++) {
      if (order[r-begin] == part) {
        g.ptr_.push_back(edges);
        std::fill(mark.begin(), mark.end(), false);
        for (integer_t j=ptr_[r]; j<ptr_[r+1]; j++) {
          auto c = ind_[j] - lo;
          if (c == r) continue;
          if (c >= 0 && c < n) {
            auto lc = c - begin;
            if (lc >= 0 && lc < dim && order[lc] == part) {
              if (!mark[lc]) {
                mark[lc] = true;
                g.ind_.push_back(ind_to_part[lc]);
                edges++;
              }
            } else {
              if (order_level > 0) {
                for (integer_t k=ptr_[c]; k<ptr_[c+1]; k++) {
                  auto cc = ind_[k] - lo;
                  auto lcc = cc - begin;
                  if (cc != r && lcc >= 0 && lcc < dim &&
                      order[lcc] == part && !mark[lcc]) {
                    mark[lcc] = true;
                    g.ind_.push_back(ind_to_part[lcc]);
                    edges++;
                  }
                }
              }
            }
          } else {
            if (order_level > 0) {
              for (auto cc : l2.at(c+lo)) {
                auto lcc = cc - begin;
                if (cc != r && lcc >= 0 &&
                    lcc < dim && order[lcc] == part && !mark[lcc]) {
                  mark[lcc] = true;
                  g.ind_.push_back(ind_to_part[lcc]);
                  edges++;
                }
              }
            }
          }
        }
      }
    }
    g.ptr_.push_back(g.ind_.size());
    return g;
  }

  template<typename integer_t> template<typename int_t> DenseMatrix<bool>
  CSRGraph<integer_t>::admissibility(const std::vector<int_t>& tiles) const {
    integer_t nt = tiles.size(), n = size();
    DenseMatrix<bool> adm(nt, nt);
    adm.fill(true);
    for (integer_t t=0; t<nt; t++)
      adm(t, t) = false;
    std::vector<integer_t> tile(n);
    for (integer_t t=0, ts=0; t<nt; t++) {
      for (integer_t i=ts; i<ts+tiles[t]; i++)
        tile[i] = t;
      ts += tiles[t];
    }
    for (integer_t i=0; i<n; i++) {
      // TODO do a BFS!!
      auto ti = tile[i];
      auto hij = ind() + ptr_[i+1];
      for (auto pj=ind()+ptr_[i]; pj!=hij; pj++) {
        auto j = *pj;
        auto tj = tile[j];
        if (ti != tj) adm(ti, tj) = adm(tj, ti) = false;
        auto hik = ind() + ptr_[j+1];
        for (auto pk=ind()+ptr_[j]; pk!=hik; pk++) {
          auto tk = tile[*pk];
          if (ti != tk) adm(ti, tk) = adm(tk, ti) = false;
        }
      }
    }
    return adm;
  }

  template<typename integer_t, typename int_t> DenseMatrix<bool>
  admissibility(const CSRGraph<integer_t>& g11,
                const CSRGraph<integer_t>& g12,
                const CSRGraph<integer_t>& g22,
                const std::vector<int_t>& rtiles,
                const std::vector<int_t>& ctiles, int knn) {
    integer_t nrt = rtiles.size(), nct = ctiles.size();
    integer_t nr = g12.size(), nc = g22.size();
    DenseMatrix<bool> adm12(nrt, nct);
    adm12.fill(true);
    std::vector<integer_t> rtile(nr), ctile(nc);
    for (integer_t t=0, ts=0; t<nrt; t++) {
      for (integer_t i=ts; i<ts+rtiles[t]; i++) rtile[i] = t;
      ts += rtiles[t];
    }
    for (integer_t t=0, ts=0; t<nct; t++) {
      for (integer_t i=ts; i<ts+ctiles[t]; i++) ctile[i] = t;
      ts += ctiles[t];
    }
    if (knn <= 2) {
      for (integer_t i=0; i<nr; i++) {
        auto rti = rtile[i];
        auto hij = g12.ind() + g12.ptr(i+1);
        for (auto pj=g12.ind()+g12.ptr(i); pj!=hij; pj++) {
          auto j = *pj;
          auto ctj = ctile[j];
          adm12(rti, ctj) = false;
          if (knn == 2) {
            auto hik = g22.ind() + g22.ptr(j+1);
            for (auto pk=g22.ind()+g22.ptr(j); pk!=hik; pk++)
              adm12(rti, ctile[*pk]) = false;
            hik = g11.ind() + g11.ptr(i+1);
            for (auto pk=g11.ind()+g11.ptr(i); pk!=hik; pk++)
              adm12(rtile[*pk], ctj) = false;
          }
        }
      }
    } else {
      const int max_dist = knn;
      std::vector<int> rmark(nr, -1), cmark(nc, -1);
      std::queue<int> rq, cq;
      for (integer_t i=0; i<nr; i++) {
        std::fill(rmark.begin(), rmark.end(), -1);
        std::fill(cmark.begin(), cmark.end(), -1);
        rq.push(i);
        rmark[i] = 0;
        while (!rq.empty() || !cq.empty()) {
          if (!rq.empty()) {
            auto k = rq.front();
            rq.pop();
            if (rmark[k] == max_dist) continue;
            auto hi = g12.ind() + g12.ptr(k+1);
            for (auto pl=g12.ind()+g12.ptr(k); pl!=hi; pl++) {
              auto l = *pl;
              if (cmark[l] == -1) {
                adm12(rtile[i], ctile[l]) = false;
                cq.push(l);
                cmark[l] = rmark[k] + 1;
              }
            }
            hi = g11.ind() + g11.ptr(k+1);
            for (auto pl=g11.ind()+g11.ptr(k); pl!=hi; pl++) {
              auto l = *pl;
              if (rmark[l] == -1) {
                rq.push(l);
                rmark[l] = rmark[k] + 1;
              }
            }
          }
          if (!cq.empty()) {
            auto k = cq.front();
            cq.pop();
            if (cmark[k] == max_dist) continue;
            auto hi = g22.ind() + g22.ptr(k+1);
            for (auto pl=g22.ind()+g22.ptr(k); pl!=hi; pl++) {
              auto l = *pl;
              adm12(rtile[i], ctile[l]) = false;
              if (cmark[l] == -1) {
                cq.push(l);
                cmark[l] = cmark[k] + 1;
              }
            }
          }
        }
      }
    }
    return adm12;
  }

  template<typename integer_t> void
  CSRGraph<integer_t>::print_dense(const std::string& name) const {
    auto n = size();
    std::unique_ptr<char[]> M(new char[n*n]);
    std::fill(M.get(), M.get()+n*n, '0');
    for (integer_t row=0; row<n; row++)
      for (integer_t j=ptr_[row]; j<ptr_[row+1]; j++)
        M[row + ind_[j]*n] = '1';
    std::cout << name << " = [" << std::endl;
    for (integer_t row=0; row<n; row++) {
      for (integer_t col=0; col<n; col++)
        std::cout << M[row + n*col] << " ";
      std::cout << ";" << std::endl;
    }
    std::cout << "];" << std::endl;
  }

} // end namespace strumpack

#endif //CSRGRAPH_HPP
