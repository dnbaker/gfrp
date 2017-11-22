#ifndef _GFRP_KERNEL_H__
#define _GFRP_KERNEL_H__
#include "gfrp/spinner.h"

namespace gfrp {

namespace ff {

struct GaussianFinalizer {
    template<typename VecType>
    void apply(VecType &in) const {
        if((in.size() & (in.size() - 1))) std::fprintf(stderr, "in.size() [%zu] is not a power of 2.\n", in.size()), exit(1);
        for(u32 i(in.size()>>1); i; --i) {
            in[(i-1)<<1] = in[i-1];
            in[(i<<1)-1] = in[(i - 1)<<1] + 0.5;
            std::fprintf(stderr, "About to cosinify: %f, %f. Indices: from %u to %u, %u\n", in[(i<<1)-1], in[(i-1)<<1], i-1, (i-1)<<1, (i<<1)-1);
        }
        in = cos(in);
        /* This can be accelerated using SLEEF.
           Sleef_sincosf4_u35, u10, u05 (sse), or 8 for avx2 or 16 for avx512
           The great thing about sleef is that it does not require the use of intel-only materials.
           This could be a nice addition to Blaze downstream.
        */
    }
};


template<typename FloatType>
class FastFoodKernelBlock {
    size_t final_output_size_; // This is twice the size passed to the Hadamard transforms
    using RandomScalingBlock = RandomGaussianScalingBlock<FloatType>;
    using SpinTransformer =
        SpinBlockTransformer<FastFoodGaussianProductBlock<FloatType>,
                             RandomScalingBlock, HadamardBlock,
                             UnitGaussianScalingBlock<FloatType>, OnlineShuffler<size_t>, HadamardBlock,
                             CompactRademacher>;
    SpinTransformer tx_;

public:
    using float_type = FloatType;
    using GaussianMatrixType = UnitGaussianScalingBlock<FloatType>;
    FastFoodKernelBlock(size_t size, FloatType sigma=1., uint64_t seed=-1):
        final_output_size_(size << 1),
        tx_(
            std::make_tuple(FastFoodGaussianProductBlock<FloatType>(sigma),
                   RandomScalingBlock(seed + seed * seed - size * size, size),
                   HadamardBlock(size, false),
                   GaussianMatrixType(seed * seed, size),
                   OnlineShuffler<size_t>(seed),
                   HadamardBlock(size, false),
                   CompactRademacher(size, (seed ^ (size * size)) + seed)))
    {
        if(final_output_size_ & (final_output_size_ - 1))
            throw std::runtime_error((std::string(__PRETTY_FUNCTION__) + "'s size should be a power of two.").data());
        std::get<RandomGaussianScalingBlock<FloatType>>(tx_.get_tuple()).rescale(std::get<GaussianMatrixType>(tx_.get_tuple()).vec_norm());
#if 0
        std::fprintf(stderr, "Sizes: %zu, %zu, %zu, %zu, %zu, %zu, %zu\n", std::get<0>(tx_.get_tuple()).size(), 
                     std::get<1>(tx_.get_tuple()).size(),
                     std::get<2>(tx_.get_tuple()).size(),
                     std::get<3>(tx_.get_tuple()).size(),
                     std::get<4>(tx_.get_tuple()).size(),
                     std::get<5>(tx_.get_tuple()).size(),
                     std::get<6>(tx_.get_tuple()).size());
        static_assert(std::is_same<std::decay_t<decltype(std::get<1>(tx_.get_tuple()))>, RandomScalingBlock>::value, "I should have this correct.");
        static_assert(std::is_same<std::decay_t<decltype(std::get<3>(tx_.get_tuple()))>, UnitGaussianScalingBlock<FloatType>>::value, "I should have this correct.");
        if(std::get<1>(tx_.get_tuple()).size() == 0) throw std::runtime_error("Didn't it just say it was nonzero?");
        if(std::get<3>(tx_.get_tuple()).size() == 0) throw std::runtime_error("Didn't it just say 3's was nonzero?");
#endif
    }
    size_t transform_size() const {return final_output_size_ >> 1;}
    template<typename InputType, typename OutputType>
    void apply(OutputType &out, const InputType &in) {
        if(out.size() != final_output_size_) {
            fprintf(stderr, "Warning: Output size was wrong (%zu, not %zu). Resizing\n", out.size(), final_output_size_);
        }
        if(roundup(in.size()) != transform_size()) throw std::runtime_error("ZOMG");
        blaze::reset(out);
        auto copysv(subvector(out, 0, in.size()));
        //std::fprintf(stderr, "copying in to copysv. (Sizes: copysv - %zu, in - %zu)\n", copysv.size(), in.size());
        ks::string tmp;
        copysv = in; // Copy input to output space.
        tmp += '[';
        for(const auto el: copysv) tmp.sprintf("%e,", el);
        tmp.back() = ']';
        std::fprintf(stderr, "After copying input vector to output vector: %s\n", tmp.data());

        auto half_vector(subvector(out, 0, transform_size()));
        //std::fprintf(stderr, "half vector is size %zu out of out size %zu\n", half_vector.size(), out.size());
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
        stacked_size = std::max(stacked_size, input_ru << 1);
        if(stacked_size % input_ru)
            stacked_size = input_ru - (stacked_size % input_ru);
        if(stacked_size % input_ru) std::fprintf(stderr, "Stacked size is not evenly divisible.\n"), exit(1);
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
                         out.size(), (blocks_.size() << 1) * in_rounded, in.size(), (size_t)roundup(in.size()));
            out.resize((blocks_.size() << 1) * in_rounded);
        }
        in_rounded <<= 1; // To account for the doubling for the sin/cos entry for each random projection.
#ifdef USE_OPENMP
        #pragma omp parallel for
#endif
        ks::string tmp;
        for(size_t i = 0; i < blocks_.size(); ++i) {
            auto sv(subvector(out, in_rounded * i, in_rounded));
            tmp.puts("[Block ");
            tmp.putl(i);
            tmp.puts("] before: ");
            ksprint(sv, tmp);
            blocks_[i].apply(sv, in);
            tmp.sprintf("\nAfter block, before finalizer: ");
            ksprint(sv, tmp);
            finalizer_.apply(sv);
            tmp.puts("\nAfter finalizer: ");
            ksprint(sv, tmp);
            tmp.putc_('\n');
            tmp.write(stderr), tmp.clear();
        }
    }
};

} // namespace ff


} // namespace gfrp

#endif // #ifndef _GFRP_KERNEL_H__
