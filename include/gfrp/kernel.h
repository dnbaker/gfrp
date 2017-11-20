#ifndef _GFRP_KERNEL_H__
#define _GFRP_KERNEL_H__
#include "gfrp/spinner.h"

namespace gfrp {

namespace ff {

struct GaussianFinalizer {
    template<typename VecType>
    void apply(VecType &in) {
        assert((in.size() & (in.size() - 1)) == 0);
        for(u32 i(in.size()>>1); i; --i)
            in[(i<<1)-1] = (in[(i-1)<<1] = in[i-1]) + 0.5;
        in = cos(in);
        /* This can be accelerated using SLEEF.
           Sleef_sincosf4_u35, u10, u05 (sse), or 8 for avx2 or 16 for avx512
           The great thing about sleef is that it does not require the use of intel-only materials.
           This could be a nice addition to Blaze downstream.
        */
    }
};


template<typename FloatType, typename RandomScalingBlock=RandomGaussianScalingBlock<FloatType>,
         typename GaussianMatrixType=UnitGaussianScalingBlock<FloatType>,
         typename FirstSBlockType=HadamardBlock, typename LastSBlockType=FirstSBlockType>
class FastFoodKernelBlock {
    size_t final_output_size_; // This is twice the size passed to the Hadamard transforms
    using SpinTransformer =
        SpinBlockTransformer<FastFoodGaussianProductBlock<FloatType>,
                             RandomGaussianScalingBlock<FloatType>, LastSBlockType,
                             GaussianMatrixType, OnlineShuffler<size_t>, FirstSBlockType,
                             CompactRademacher>;
    SpinTransformer tx_;

public:
    using float_type = FloatType;
    FastFoodKernelBlock(size_t size, FloatType sigma=1., uint64_t seed=-1):
        final_output_size_(size),
        tx_(
            std::make_tuple(FastFoodGaussianProductBlock<FloatType>(sigma),
                   RandomGaussianScalingBlock<FloatType>(1., seed + seed * seed - size * size, transform_size()),
                   LastSBlockType(transform_size()),
                   GaussianMatrixType(seed * seed, transform_size()),
                   OnlineShuffler<size_t>(seed),
                   FirstSBlockType(transform_size()),
                   CompactRademacher(transform_size(), (seed ^ (size * size)) + seed)))
    {
        if(final_output_size_ & (final_output_size_ - 1))
            throw std::runtime_error((std::string(__PRETTY_FUNCTION__) + "'s size should be a power of two.").data());
        std::get<RandomGaussianScalingBlock<FloatType>>(tx_.get_tuple()).rescale(std::get<GaussianMatrixType>(tx_.get_tuple()).vec_norm());
    }
    size_t transform_size() const {return final_output_size_ >> 1;}
    template<typename InputType, typename OutputType>
    void apply(OutputType &out, const InputType &in) {
        if(out.size() != final_output_size_) {
            fprintf(stderr, "Warning: Output size was wrong (%zu, not %zu). Resizing\n", out.size(), final_output_size_);
        }
        if(roundup(in.size()) != transform_size()) throw std::runtime_error("ZOMG");
        blaze::reset(out);
        subvector(out, 0, in.size()) = in; // Copy input to output space.

        auto half_vector(subvector(out, 0, transform_size()));
        tx_.apply(half_vector);   
    }
};

template<typename KernelBlock,
         typename Finalizer=GaussianFinalizer>
class Kernel {
    std::vector<KernelBlock> blocks_;
    Finalizer             finalizer_;

public:
    using FloatType = typename KernelBlock::float_type;
    template<typename... Args>
    Kernel(size_t stacked_size, size_t input_size,
           FloatType sigma, uint64_t seed,
           Args &&... args):
        finalizer_(std::forward<Args>(args)...)
    {
        size_t input_ru = roundup(input_size);
        if(stacked_size <= input_ru << 1) throw std::runtime_error("ZOMG");
        stacked_size   += input_ru - (stacked_size & (input_ru - 1));
        stacked_size  <<= 1;
        size_t nblocks = (stacked_size >> 1) / input_ru;
        aes::AesCtr gen(seed);
        while(blocks_.size() < nblocks) {
            blocks_.emplace_back(input_ru, sigma, gen());
        }
    }
    template<typename InputType, typename OutputType>
    void apply(OutputType &out, const InputType &in) {
        size_t in_rounded(roundup(in.size()));
        if(out.size() != (blocks_.size() << 1) * in_rounded) {
            std::fprintf(stderr, "Resizing out block from %zu to %zu to match %zu input and %zu rounded up input.\n",
                         out.size(), blocks_.size(), in.size(), (size_t)roundup(in.size()));
            out.resize((blocks_.size() << 1) * in_rounded);
        }
        in_rounded <<= 1; // To account for the doubling for the sin/cos entry for each random projection.
#ifdef USE_OPENMP
        #pragma omp parallel for
#endif
        for(size_t i = 0; i < blocks_.size(); ++i) {
            auto sv(subvector(out, in_rounded * i, in_rounded));
            blocks_[i].apply(sv, in);
            finalizer_.apply(sv);
        }
    }
};

} // namespace ff


} // namespace gfrp

#endif // #ifndef _GFRP_KERNEL_H__
