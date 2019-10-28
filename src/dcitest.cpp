#include "include/frp/dci.h"
#include <iostream>
#include <thread>
#include "omp.h"
#include "aesctr/wy.h"
#include <getopt.h>

using namespace frp;
using namespace dci;

// using UDMF = blaze::StrictlyUpperMatrix<blaze::DynamicMatrix<float>>;
// using UDMU = blaze::StrictlyUpperMatrix<blaze::DynamicMatrix<unsigned>>;
using UDMF = blaze::DynamicMatrix<FLOAT_TYPE>;
using UDMU = blaze::DynamicMatrix<unsigned>;


//StrictlyUpperMatrix

template<typename DCIType>
std::pair<UDMF,UDMU> nn_data(const DCIType &dc) {
    auto a = dc.begin();
    ssize_t dist = std::distance(a, dc.end());
    size_t n = dist;
    UDMF dists(dist, dist);
    _Pragma("omp parallel for")
    for(size_t i = 0; i < n; ++i) {
        const blaze::DynamicVector<FLOAT_TYPE> &vp1 = *dc[i];
        auto r1 = row(dists, i);
        for(size_t j = i + 1; j < n; ++j) {
            const blaze::DynamicVector<FLOAT_TYPE> &vp2 = *dc[j];
            auto d = blaze::norm(vp1 - vp2);
            r1[j] = d;
        }
    }
    UDMU labels(dist, dist);
    _Pragma("omp parallel for")
    for(size_t i = 0; i < n; ++i) {
        auto r = row(labels, i);
        auto mr = row(dists, i);
        std::iota(r.begin(), r.end(), 0u);
        sort(r.begin(), r.end(), [&mr](auto x, auto y) {return mr[x] < mr[y];});
#if 0
        for(auto x: r) {
            assert(x < mr.size());
            std::fprintf(stderr, "n = %zu speaking now with r %d/%f\n", i, x, float(mr[x]));
        }
#endif
    }
    //std::cerr << "Distances! " << dists << '\n';
    std::fprintf(stderr, "Return pair stuff\n");
    return std::make_pair(std::move(dists), std::move(labels));
}


template<typename T1, typename I=std::uint32_t>
auto distmat2nn(const T1 &mat, size_t k) {
    if(mat.columns() > std::numeric_limits<I>::max())
        throw std::runtime_error("Overflow: mat size too large");
    k = std::min(mat.columns(), k);
    blaze::DynamicMatrix<I> ret(mat.columns(), k);
    //#pragma omp parallel for
    for(size_t i = 0; i < mat.rows(); ++i) {
        //std::fprintf(stderr, "Label stuff2nn %zu\n", i);
        auto r = row(mat, i);
        //std::cerr << "Matrix row: " << r;
        auto func = [&r](size_t j, size_t k){return r[j] > r[k];};
        size_t heapsz = 0;
        auto pq(row(ret, i));
        assert(k == pq.size());
        size_t j;
        for(j = 0;j < mat.rows();++j) {
            auto pqp = &pq[0];
            if(heapsz < pq.size()) {
                pq[heapsz] = j;
                if(++heapsz == pq.size())
                    std::make_heap(pqp, pqp + heapsz, func);
            } else if(func(j, pq[0])) {
                assert(pq.size() >= heapsz);
                std::pop_heap(pqp, pqp + heapsz, func);
                pq[heapsz - 1] = j;
                std::push_heap(pqp, pqp + heapsz, func);
            }
        }
        for(auto it = pq.end();it != pq.begin();std::pop_heap(pq.begin(), it--, func));
        assert(std::is_sorted(pq.begin(), pq.end(), func));
        // std::cerr << pq << '\n';
#if 0
        for(auto v: pq) {
            if(r[v] > r[pq[0]]) {
                std::fprintf(stderr, "WOOOrv: %e. rpq: %e\n", r[v], r[pq[0]]);
            }
            else {
                std::fprintf(stderr, "NOOOrv: %e. rpq: %e\n", r[v], r[pq[0]]);
            }
        }
#endif
    }
    return ret;
}


void usage() {
    std::fprintf(stderr, "Usage: dcitest <flags>\n-d: dimension [400]\n-n: number of points [100000]"
                         "-l: Number of levels [15]\n"
                         "-m: Number of sublevels per level [5]\n"
                         "-k: Number of neighbors to retrieve [5]\n"
                         "-h: Usage (this menu)\n");
    std::exit(1);
}

int main(int argc, char *argv[]) {
    int c, nd = 400, npoints = 100000, k = 10, l = 15, m = 5;
    while((c = getopt(argc, argv, "d:n:k:l:m:h")) >= 0) {
         switch(c) {
             case 'd': nd = std::atoi(optarg); break;
             case 'n': npoints = std::atoi(optarg); break;
             case 'k': k = std::atoi(optarg); break;
             case 'l': l = std::atoi(optarg); break;
             case 'm': m = std::atoi(optarg); break;
             case 'h': case '?': usage();
         }
    }
    std::fprintf(stderr, "nd: %d. np: %d. n: %d\n", nd, npoints, k);
    DCI<blaze::DynamicVector<FLOAT_TYPE>> dci(m, l, nd, 1e-5, true);
#if 0
    {
        // make sure it works with < nd
        DCI<blaze::DynamicVector<FLOAT_TYPE>, uint32_t, float, std::deque> dcid(4, 20, nd, 1e-5, false);
        DCI<blaze::DynamicVector<FLOAT_TYPE>> tmp(4, 3, nd, 1e-5, true);
    }
#endif
    std::cerr << "made dci\n";
    std::vector<blaze::DynamicVector<FLOAT_TYPE>> ls;
    wy::WyHash<uint64_t, 8> mt;
    std::normal_distribution<FLOAT_TYPE> gen(2.5, std::sqrt(2.5));
    gen.reset();
    omp_set_num_threads(std::thread::hardware_concurrency());
    for(ssize_t i = 0; i < npoints; ++i) {
        ls.emplace_back(nd);
        for(auto &x: ls.back())
            x = gen(mt);
    }
    std::fprintf(stderr, "Generated\n");
    //OMP_PRAGMA("omp parallel for")
    for(size_t i = 0; i < ls.size(); ++i)
        dci.add_item(ls[i]);
    std::fprintf(stderr, "Added\n");
    //std::cerr << nnmat << '\n';
    auto topn = dci.query(ls[0], k);
    std::fprintf(stderr, "topn, where n is %zu: \n\n", topn.size());
    std::reverse(topn.begin(), topn.end());
    auto tnbeg = topn.begin();
    double mv = norm(ls[tnbeg++->id()] - ls[0]);
    std::fprintf(stderr, "first dist: %le\n", mv);
    do {
        auto id = tnbeg->id();
        blaze::DynamicVector<FLOAT_TYPE> &rl(ls[id]);
        blaze::DynamicVector<FLOAT_TYPE> &rr(ls[0]);
        double newv = norm(rl - rr);
        std::fprintf(stderr, "dist: %f, id %u\n", newv, unsigned(id));
    } while(++tnbeg != topn.end());
    std::reverse(topn.begin(), topn.end());
    assert(std::is_sorted(topn.begin(), topn.end()));
    auto dcid2 = dci.template cvt<>();
    std::fprintf(stderr, "Doing exact, feel free to skip\n");
    //auto [x, y] = nn_data(dci);
    //std::fprintf(stderr, "nn\n");
    //auto nnmat = distmat2nn(x, std::max(n, nd - 15));
    std::priority_queue<frp::dci::ProjID<FLOAT_TYPE, int>> pqs;
    std::fprintf(stderr, "Beginning exact calculation\n");
    #pragma omp parallel for schedule(static, 32)
    for(int i = 0; i < npoints; ++i) {
        const auto v = norm(ls[0] - ls[i]);
        if(pqs.empty() || v < pqs.top().f()) {
            const auto tmp = frp::dci::ProjID<FLOAT_TYPE, int>(v, i);
            #pragma omp critical
            {
                pqs.push(tmp);
                if(pqs.size() > unsigned(k)) {
                    //std::fprintf(stderr, "Last thing: %f\n", pqs.top().f());
                    pqs.pop();
                }
            }
        }
    }
    std::vector<frp::dci::ProjID<FLOAT_TYPE, int>> exact_topn;
    while(pqs.size()) {
        auto p = pqs.top();
        exact_topn.push_back(p);
        std::fprintf(stderr, "exact %zu: %f, %d\n", pqs.size(), p.f(), p.id());
        pqs.pop();
    }
}
