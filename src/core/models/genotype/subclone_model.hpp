// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef subclone_model_hpp
#define subclone_model_hpp

#include <vector>
#include <unordered_map>
#include <utility>

#include <boost/optional.hpp>

#include "config/common.hpp"
#include "core/types/haplotype.hpp"
#include "core/types/genotype.hpp"
#include "core/types/cancer_genotype.hpp"
#include "core/models/haplotype_likelihood_array.hpp"
#include "exceptions/unimplemented_feature_error.hpp"
#include "variational_bayes_mixture_model.hpp"
#include "genotype_prior_model.hpp"
#include "cancer_genotype_prior_model.hpp"

namespace octopus { namespace model {

template <typename Genotype_, typename GenotypeIndex_, typename GenotypePriorModel_>
class SubcloneModelBase
{
public:
    struct AlgorithmParameters
    {
        unsigned max_iterations = 1000;
        double epsilon          = 0.05;
        unsigned max_seeds      = 12;
        boost::optional<MemoryFootprint> target_max_memory = boost::none;
        ExecutionPolicy execution_policy = ExecutionPolicy::seq;
    };
    
    struct Priors
    {
        using GenotypeMixturesDirichletAlphas   = std::vector<double>;
        using GenotypeMixturesDirichletAlphaMap = std::unordered_map<SampleName, GenotypeMixturesDirichletAlphas>;
        
        const GenotypePriorModel_& genotype_prior_model;
        GenotypeMixturesDirichletAlphaMap alphas;
    };
    
    struct Latents
    {
        using GenotypeMixturesDirichletAlphas   = std::vector<double>;
        using GenotypeMixturesDirichletAlphaMap = std::unordered_map<SampleName, GenotypeMixturesDirichletAlphas>;
        using ProbabilityVector                 = std::vector<double>;
        
        ProbabilityVector genotype_probabilities;
        GenotypeMixturesDirichletAlphaMap alphas;
    };
    
    struct InferredLatents
    {
        Latents posteriors;
        typename Latents::ProbabilityVector genotype_log_priors;
        double approx_log_evidence;
    };
    
    SubcloneModelBase() = delete;
    
    SubcloneModelBase(std::vector<SampleName> samples, Priors priors);
    SubcloneModelBase(std::vector<SampleName> samples, Priors priors, AlgorithmParameters parameters);
    
    SubcloneModelBase(const SubcloneModelBase&)            = default;
    SubcloneModelBase& operator=(const SubcloneModelBase&) = default;
    SubcloneModelBase(SubcloneModelBase&&)                 = default;
    SubcloneModelBase& operator=(SubcloneModelBase&&)      = default;
    
    ~SubcloneModelBase() = default;
    
    const Priors& priors() const noexcept;
    
    void prime(const std::vector<Haplotype>& haplotypes);
    void unprime() noexcept;
    bool is_primed() const noexcept;
    
    InferredLatents evaluate(const std::vector<Genotype_>& genotypes,
                             const HaplotypeLikelihoodArray& haplotype_likelihoods) const;
    
    InferredLatents evaluate(const std::vector<Genotype_>& genotypes,
                             const std::vector<GenotypeIndex_>& genotype_indices,
                             const HaplotypeLikelihoodArray& haplotype_likelihoods) const;
    
private:
    std::vector<SampleName> samples_;
    Priors priors_;
    AlgorithmParameters parameters_;
    const std::vector<Haplotype>* haplotypes_;
};

using SubcloneModel = SubcloneModelBase<Genotype<Haplotype>, GenotypeIndex, GenotypePriorModel>;
using SomaticSubcloneModel = SubcloneModelBase<CancerGenotype<Haplotype>, CancerGenotypeIndex, CancerGenotypePriorModel>;

template <typename G, typename GI, typename GPM>
SubcloneModelBase<G, GI, GPM>::SubcloneModelBase(std::vector<SampleName> samples, Priors priors)
: SubcloneModelBase {std::move(samples), std::move(priors), AlgorithmParameters {}}
{}

template <typename G, typename GI, typename GPM>
SubcloneModelBase<G, GI, GPM>::SubcloneModelBase(std::vector<SampleName> samples, Priors priors, AlgorithmParameters parameters)
: samples_ {std::move(samples)}
, priors_ {std::move(priors)}
, parameters_ {parameters}
{}

template <typename G, typename GI, typename GPM>
const typename SubcloneModelBase<G, GI, GPM>::Priors& SubcloneModelBase<G, GI, GPM>::priors() const noexcept
{
    return priors_;
}

template <typename G, typename GI, typename GPM>
void SubcloneModelBase<G, GI, GPM>::prime(const std::vector<Haplotype>& haplotypes)
{
    haplotypes_ = std::addressof(haplotypes);
}

template <typename G, typename GI, typename GPM>
void SubcloneModelBase<G, GI, GPM>::unprime() noexcept
{
    haplotypes_ = nullptr;
}

template <typename G, typename GI, typename GPM>
bool SubcloneModelBase<G, GI, GPM>::is_primed() const noexcept
{
    return haplotypes_;
}

namespace detail {

template <typename GI>
struct IndexData
{
    const std::vector<GI>& genotype_indices;
    const std::vector<Haplotype>* haplotypes;
};

template <typename G, typename GI, typename GPM>
auto evaluate_genotype_priors(const std::vector<G>& genotypes,
                              const typename SubcloneModelBase<G, GI, GPM>::Priors& priors,
                              const boost::optional<IndexData<GI>> index_data)
{
    if (index_data) {
        return evaluate(index_data->genotype_indices, priors.genotype_prior_model);
    } else {
        return evaluate(genotypes, priors.genotype_prior_model);
    }
}

std::vector<LogProbabilityVector>
generate_seeds(const std::vector<SampleName>& samples,
               const std::vector<Genotype<Haplotype>>& genotypes,
               const LogProbabilityVector& genotype_log_priors,
               const HaplotypeLikelihoodArray& haplotype_log_likelihoods,
               const SubcloneModel::Priors& priors,
               std::size_t max_seeds,
               boost::optional<IndexData<GenotypeIndex>> index_data = boost::none);

std::vector<LogProbabilityVector>
generate_seeds(const std::vector<SampleName>& samples,
               const std::vector<CancerGenotype<Haplotype>>& genotypes,
               const LogProbabilityVector& genotype_log_priors,
               const HaplotypeLikelihoodArray& haplotype_log_likelihoods,
               const SomaticSubcloneModel::Priors& priors,
               std::size_t max_seeds,
               boost::optional<IndexData<CancerGenotypeIndex>> index_data = boost::none);

template <std::size_t K, typename G, typename GI, typename GPM>
VBAlpha<K> flatten(const typename SubcloneModelBase<G, GI, GPM>::Priors::GenotypeMixturesDirichletAlphas& alpha)
{
    VBAlpha<K> result {};
    std::copy_n(std::cbegin(alpha), K, std::begin(result));
    return result;
}

template <std::size_t K, typename G, typename GI, typename GPM>
VBAlphaVector<K> flatten(const typename SubcloneModelBase<G, GI, GPM>::Priors::GenotypeMixturesDirichletAlphaMap& alphas,
                         const std::vector<SampleName>& samples)
{
    VBAlphaVector<K> result(samples.size());
    std::transform(std::cbegin(samples), std::cend(samples), std::begin(result),
                   [&alphas] (const auto& sample) { return flatten<K, G, GI, GPM>(alphas.at(sample)); });
    return result;
}

template <std::size_t K>
VBGenotype<K>
flatten(const Genotype<Haplotype>& genotype, const SampleName& sample,
        const HaplotypeLikelihoodArray& haplotype_likelihoods)
{
    VBGenotype<K> result {};
    std::transform(std::cbegin(genotype), std::cend(genotype), std::begin(result),
                   [&sample, &haplotype_likelihoods] (const Haplotype& haplotype)
                   -> std::reference_wrapper<const VBReadLikelihoodArray::BaseType> {
                       return std::cref(haplotype_likelihoods(sample, haplotype));
                   });
    return result;
}

template <std::size_t K>
auto copy_cref(const Genotype<Haplotype>& genotype, const SampleName& sample,
               const HaplotypeLikelihoodArray& haplotype_likelihoods,
               typename VBGenotype<K>::iterator result_itr)
{
    return std::transform(std::cbegin(genotype), std::cend(genotype), result_itr,
                          [&sample, &haplotype_likelihoods] (const Haplotype& haplotype)
                          -> std::reference_wrapper<const VBReadLikelihoodArray::BaseType> {
                              return std::cref(haplotype_likelihoods(sample, haplotype));
                          });
}

template <std::size_t K>
VBGenotype<K>
flatten(const CancerGenotype<Haplotype>& genotype, const SampleName& sample,
        const HaplotypeLikelihoodArray& haplotype_likelihoods)
{
    VBGenotype<K> result {};
    assert(genotype.ploidy() == K);
    auto itr = copy_cref<K>(genotype.germline(), sample, haplotype_likelihoods, std::begin(result));
    copy_cref<K>(genotype.somatic(), sample, haplotype_likelihoods, itr);
    return result;
}

template <std::size_t K, typename G>
VBGenotypeVector<K>
flatten(const std::vector<G>& genotypes, const SampleName& sample,
        const HaplotypeLikelihoodArray& haplotype_likelihoods)
{
    VBGenotypeVector<K> result(genotypes.size());
    std::transform(std::cbegin(genotypes), std::cend(genotypes), std::begin(result),
                   [&sample, &haplotype_likelihoods] (const auto& genotype) {
                       return flatten<K>(genotype, sample, haplotype_likelihoods);
                   });
    return result;
}

template <std::size_t K, typename G>
VBReadLikelihoodMatrix<K>
flatten(const std::vector<G>& genotypes,
        const std::vector<SampleName>& samples,
        const HaplotypeLikelihoodArray& haplotype_likelihoods)
{
    VBReadLikelihoodMatrix<K> result {};
    result.reserve(samples.size());
    std::transform(std::cbegin(samples), std::cend(samples), std::back_inserter(result),
                   [&genotypes, &haplotype_likelihoods] (const auto& sample) {
                       return flatten<K>(genotypes, sample, haplotype_likelihoods);
                   });
    return result;
}

template <std::size_t K, typename G, typename GI, typename GPM>
auto expand(VBAlpha<K>& alpha)
{
    return typename SubcloneModelBase<G, GI, GPM>::Latents::GenotypeMixturesDirichletAlphas(std::begin(alpha), std::end(alpha));
}

template <std::size_t K, typename G, typename GI, typename GPM>
auto expand(const std::vector<SampleName>& samples, VBAlphaVector<K>&& alphas)
{
    typename SubcloneModelBase<G, GI, GPM>::Latents::GenotypeMixturesDirichletAlphaMap result {};
    std::transform(std::cbegin(samples), std::cend(samples), std::begin(alphas),
                   std::inserter(result, std::begin(result)),
                   [] (const auto& sample, auto&& vb_alpha) {
                       return std::make_pair(sample, expand<K, G, GI, GPM>(vb_alpha));
                   });
    return result;
}

template <std::size_t K, typename G, typename GI, typename GPM>
typename SubcloneModelBase<G, GI, GPM>::InferredLatents
expand(const std::vector<SampleName>& samples, VBLatents<K>&& inferred_latents, LogProbabilityVector genotype_log_priors, double evidence)
{
    typename SubcloneModelBase<G, GI, GPM>::Latents posterior_latents {std::move(inferred_latents.genotype_posteriors),
                                                                       expand<K, G, GI, GPM>(samples, std::move(inferred_latents.alphas))};
    return {std::move(posterior_latents), std::move(genotype_log_priors), evidence};
}

template <std::size_t K, typename G, typename GI, typename GPM>
typename SubcloneModelBase<G, GI, GPM>::InferredLatents
run_variational_bayes_helper(const std::vector<SampleName>& samples,
                             const std::vector<G>& genotypes,
                             const typename SubcloneModelBase<G, GI, GPM>::Priors::GenotypeMixturesDirichletAlphaMap& prior_alphas,
                             const LogProbabilityVector& genotype_log_priors,
                             const HaplotypeLikelihoodArray& haplotype_log_likelihoods,
                             const typename SubcloneModelBase<G, GI, GPM>::AlgorithmParameters& params,
                             std::vector<LogProbabilityVector>&& seeds)
{
    VariationalBayesParameters vb_params {params.epsilon, params.max_iterations};
    if (params.target_max_memory) {
        const auto estimated_memory_default = estimate_memory_requirement<K>(samples, haplotype_log_likelihoods, genotypes.size(), vb_params);
        if (estimated_memory_default > *params.target_max_memory) {
            vb_params.save_memory = true;
        }
    }
    if (params.execution_policy == ExecutionPolicy::par) {
        vb_params.parallel_execution = true;
    }
    const auto vb_prior_alphas = flatten<K, G, GI, GPM>(prior_alphas, samples);
    const auto log_likelihoods = flatten<K>(genotypes, samples, haplotype_log_likelihoods);
    auto p = octopus::model::run_variational_bayes(vb_prior_alphas, genotype_log_priors, log_likelihoods, vb_params, std::move(seeds));
    return expand<K, G, GI, GPM>(samples, std::move(p.first), std::move(genotype_log_priors), p.second);
}

template <typename G, typename GI, typename GPM>
typename SubcloneModelBase<G, GI, GPM>::InferredLatents
run_variational_bayes_helper(const std::vector<SampleName>& samples,
                             const std::vector<G>& genotypes,
                             const typename SubcloneModelBase<G, GI, GPM>::Priors::GenotypeMixturesDirichletAlphaMap& prior_alphas,
                             LogProbabilityVector genotype_log_priors,
                             const HaplotypeLikelihoodArray& haplotype_log_likelihoods,
                             const typename SubcloneModelBase<G, GI, GPM>::AlgorithmParameters& params,
                             std::vector<LogProbabilityVector>&& seeds)
{
    using std::move;
    switch (genotypes.front().ploidy()) {
        case 1: return run_variational_bayes_helper<1, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 2: return run_variational_bayes_helper<2, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 3: return run_variational_bayes_helper<3, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 4: return run_variational_bayes_helper<4, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 5: return run_variational_bayes_helper<5, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 6: return run_variational_bayes_helper<6, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 7: return run_variational_bayes_helper<7, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 8: return run_variational_bayes_helper<8, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 9: return run_variational_bayes_helper<9, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                   haplotype_log_likelihoods, params, move(seeds));
        case 10: return run_variational_bayes_helper<10, G, GI, GPM>(samples, genotypes, prior_alphas, move(genotype_log_priors),
                                                                     haplotype_log_likelihoods, params, move(seeds));
        default: throw UnimplementedFeatureError {"ploidies above 10", "SubcloneModel"};
    }
}

template <typename G, typename GI, typename GPM>
typename SubcloneModelBase<G, GI, GPM>::InferredLatents
run_variational_bayes(const std::vector<SampleName>& samples,
                      const std::vector<G>& genotypes,
                      const typename SubcloneModelBase<G, GI, GPM>::Priors& priors,
                      const HaplotypeLikelihoodArray& haplotype_log_likelihoods,
                      const typename SubcloneModelBase<G, GI, GPM>::AlgorithmParameters& params,
                      boost::optional<IndexData<GI>> index_data = boost::none)
{
    auto genotype_log_priors = evaluate_genotype_priors<G, GI, GPM>(genotypes, priors, index_data);
    auto seeds = generate_seeds(samples, genotypes, genotype_log_priors, haplotype_log_likelihoods, priors, params.max_seeds, index_data);
    return run_variational_bayes_helper<G, GI, GPM>(samples, genotypes, priors.alphas, std::move(genotype_log_priors),
                                                    haplotype_log_likelihoods, params, std::move(seeds));
}

} // namespace detail

template <typename G, typename GI, typename GPM>
typename SubcloneModelBase<G, GI, GPM>::InferredLatents
SubcloneModelBase<G, GI, GPM>::evaluate(const std::vector<G>& genotypes,
                                        const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    assert(!genotypes.empty());
    return detail::run_variational_bayes<G, GI, GPM>(samples_, genotypes, priors_, haplotype_likelihoods, parameters_);
}

template <typename G, typename GI, typename GPM>
typename SubcloneModelBase<G, GI, GPM>::InferredLatents
SubcloneModelBase<G, GI, GPM>::evaluate(const std::vector<G>& genotypes,
                                        const std::vector<GI>& genotype_indices,
                                        const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    assert(!genotypes.empty());
    assert(genotypes.size() == genotype_indices.size());
    const detail::IndexData<GI> index_data {genotype_indices, haplotypes_};
    return detail::run_variational_bayes<G, GI, GPM>(samples_, genotypes, priors_, haplotype_likelihoods, parameters_, index_data);
}

} // namespace model
} // namespace octopus

#endif
