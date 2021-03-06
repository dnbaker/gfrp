#undef NDEBUG
#include "aesctr/wy.h"
#include "frp/linalg.h"
#include "frp/dist.h"
#include <iostream>

using namespace frp;
using namespace linalg;
using blaze::rowMajor;

// c = np.dot((X.T - np.mean(X, axis=1)).T, (X.T - np.mean(X, axis=1))) * np.true_divide(1, wsum[0] - 1)
// Python code above works.
// The key is take the matrix, sub
int main(int argc, char *argv[]) {
    unsigned nrows = argc == 1 ? 25: std::atoi(argv[1]), ncols = argc < 3 ? 10: std::atoi(argv[2]), ncomp = argc < 4 ? ncols: std::atoi(argv[3]);
    PCAAggregator<float> pcag(ncols);
    blaze::DynamicVector<float> v(ncols, 1);
    blaze::DynamicVector<float, blaze::rowVector> v2(ncols, 3);
    assert(v2.size() == ncols);
    assert(v.size() == ncols);
    blaze::DynamicMatrix<float> random_data(nrows * 20, ncols);
    unit_gaussian_fill(random_data, nrows * ncols + ncomp);
    pcag.add(v);
    pcag.add(v2);
    pcag.add(random_data);
    size_t NPERVEC = sizeof(vec::SIMDTypes<uint64_t>::VType) / sizeof(float);
    pcag.add(blaze::CustomMatrix<float, blaze::aligned, blaze::padded, rowMajor>(
        &random_data(0, 0), random_data.rows(), random_data.columns(), ((random_data.rows() + NPERVEC - 1) / NPERVEC) * NPERVEC));
    std::fprintf(stderr, "Added to pca aggregator\n");
    blaze::DynamicMatrix<float> mat(nrows, ncols);
    std::mt19937_64 mt;
    std::uniform_real_distribution<float> gen;
    for(size_t i = 0; i < mat.rows(); ++i) for(size_t j = 0; j < mat.columns(); ++j)
        mat(i, j) = gen(mt);
    std::fprintf(stderr, "Filled mat\n");
    auto c = naive_cov(mat);
    std::cerr << "naive cov\n";
    auto c2 = naive_cov(mat, false);
    std::cerr << "naive cov, falase\n";
    auto s = blaze::sum<blaze::columnwise>(mat);
    std::cout <<" mat \n" << mat << '\n';
    std::cout << "cov \n" << c << '\n';
    std::cout << "sum \n" << s << '\n';
    std::cout << "sample cov \n" << c2 << '\n';
    auto [x, y] = pca(mat, true, true, ncomp);
    std::fprintf(stderr, "Dims of x: %zu/%zu rows/columns\n", x.rows(), x.columns());
    std::fprintf(stderr, "Dims of mat: %zu/%zu rows/columns\n", mat.rows(), mat.columns());
    auto txdata = mat * x;
    std::cout << txdata << '\n';
    std::fprintf(stderr, "Dims of txdata: %zu/%zu rows/columns\n", txdata.rows(), txdata.columns());
    std::fprintf(stderr, "Subsample to 3\n");
}
