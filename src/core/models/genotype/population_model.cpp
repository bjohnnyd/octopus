// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "population_model.hpp"

#include <unordered_map>
#include <utility>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cassert>

#include "utils/maths.hpp"
#include "utils/select_top_k.hpp"
#include "constant_mixture_genotype_likelihood_model.hpp"
#include "hardy_weinberg_model.hpp"

namespace octopus { namespace model {

PopulationModel::PopulationModel(const PopulationPriorModel& prior_model,
                                 boost::optional<logging::DebugLogger> debug_log)
: prior_model_ {prior_model}
, debug_log_ {debug_log}
{}

PopulationModel::PopulationModel(const PopulationPriorModel& prior_model,
                                 Options options,
                                 boost::optional<logging::DebugLogger> debug_log)
: options_ {options}
, prior_model_ {prior_model}
, debug_log_ {debug_log}
{}

const PopulationPriorModel& PopulationModel::prior_model() const noexcept
{
    return prior_model_;
}

namespace {

using GenotypeLogLikelihoodVector  = std::vector<double>;
using GenotypeLogLikelihoodMatrix  = std::vector<GenotypeLogLikelihoodVector>;

struct GenotypeLogProbability
{
    const Genotype<Haplotype>& genotype;
    double log_probability;
};
using GenotypeLogMarginalVector = std::vector<GenotypeLogProbability>;

using GenotypeMarginalPosteriorVector  = std::vector<double>;
using GenotypeMarginalPosteriorMatrix  = std::vector<GenotypeMarginalPosteriorVector>; // for each sample

using InverseGenotypeTable = std::vector<std::vector<std::size_t>>;

auto make_inverse_genotype_table(const std::vector<Haplotype>& haplotypes,
                                 const std::vector<Genotype<Haplotype>>& genotypes)
{
    assert(!haplotypes.empty() && !genotypes.empty());
    using HaplotypeReference = std::reference_wrapper<const Haplotype>;
    std::unordered_map<HaplotypeReference, std::vector<std::size_t>> result_map {haplotypes.size()};
    const auto cardinality = element_cardinality_in_genotypes(static_cast<unsigned>(haplotypes.size()),
                                                              genotypes.front().ploidy());
    for (const auto& haplotype : haplotypes) {
        result_map[std::cref(haplotype)].reserve(cardinality);
    }
    for (std::size_t i {0}; i < genotypes.size(); ++i) {
        for (const auto& haplotype : genotypes[i]) {
            result_map.at(haplotype).emplace_back(i);
        }
    }
    InverseGenotypeTable result {};
    result.reserve(haplotypes.size());
    for (const auto& haplotype : haplotypes) {
        result.emplace_back(std::move(result_map.at(haplotype)));
    }
    return result;
}

auto make_inverse_genotype_table(const std::vector<GenotypeIndex>& genotype_indices, const std::size_t num_haplotypes)
{
    InverseGenotypeTable result(num_haplotypes);
    const auto num_genotypes = genotype_indices.size();
    for (auto& entry : result) entry.reserve(num_genotypes / 2);
    for (std::size_t genotype_idx {0}; genotype_idx < num_genotypes; ++genotype_idx) {
        for (auto idx : genotype_indices[genotype_idx]) {
            if (result[idx].empty() || result[idx].back() != genotype_idx) {
                result[idx].push_back(genotype_idx);
            }
        }
    }
    for (auto& entry : result) entry.shrink_to_fit();
    return result;
}

double calculate_frequency_update_norm(const std::size_t num_samples, const unsigned ploidy)
{
    return static_cast<double>(num_samples) * ploidy;
}

struct EMOptions
{
    unsigned max_iterations;
    double epsilon;
};

struct ModelConstants
{
    const std::vector<Haplotype>& haplotypes;
    const std::vector<Genotype<Haplotype>>& genotypes;
    const GenotypeLogLikelihoodMatrix& genotype_log_likilhoods;
    const unsigned ploidy;
    const double frequency_update_norm;
    const InverseGenotypeTable genotypes_containing_haplotypes;
    
    ModelConstants(const std::vector<Haplotype>& haplotypes,
                   const std::vector<Genotype<Haplotype>>& genotypes,
                   const GenotypeLogLikelihoodMatrix& genotype_log_likilhoods)
    : haplotypes {haplotypes}
    , genotypes {genotypes}
    , genotype_log_likilhoods {genotype_log_likilhoods}
    , ploidy {genotypes.front().ploidy()}
    , frequency_update_norm {calculate_frequency_update_norm(genotype_log_likilhoods.size(), ploidy)}
    , genotypes_containing_haplotypes {make_inverse_genotype_table(haplotypes, genotypes)}
    {}
    ModelConstants(const std::vector<Haplotype>& haplotypes,
                   const std::vector<Genotype<Haplotype>>& genotypes,
                   const std::vector<GenotypeIndex>& genotype_indices,
                   const GenotypeLogLikelihoodMatrix& genotype_log_likilhoods)
    : haplotypes {haplotypes}
    , genotypes {genotypes}
    , genotype_log_likilhoods {genotype_log_likilhoods}
    , ploidy {genotypes.front().ploidy()}
    , frequency_update_norm {calculate_frequency_update_norm(genotype_log_likilhoods.size(), ploidy)}
    , genotypes_containing_haplotypes {make_inverse_genotype_table(genotype_indices, haplotypes.size())}
    {}
};

HardyWeinbergModel make_hardy_weinberg_model(const ModelConstants& constants)
{
    HardyWeinbergModel::HaplotypeFrequencyMap frequencies {constants.haplotypes.size()};
    for (const auto& haplotype : constants.haplotypes) {
        frequencies.emplace(haplotype, 1.0 / constants.haplotypes.size());
    }
    return HardyWeinbergModel {std::move(frequencies)};
}

GenotypeLogLikelihoodMatrix
compute_genotype_log_likelihoods(const std::vector<SampleName>& samples,
                                 const std::vector<Genotype<Haplotype>>& genotypes,
                                 const HaplotypeLikelihoodArray& haplotype_likelihoods)
{
    assert(!genotypes.empty());
    ConstantMixtureGenotypeLikelihoodModel likelihood_model {haplotype_likelihoods};
    GenotypeLogLikelihoodMatrix result {};
    result.reserve(samples.size());
    std::transform(std::cbegin(samples), std::cend(samples), std::back_inserter(result),
                   [&genotypes, &haplotype_likelihoods, &likelihood_model] (const auto& sample) {
                       GenotypeLogLikelihoodVector likelihoods(genotypes.size());
                       haplotype_likelihoods.prime(sample);
                       std::transform(std::cbegin(genotypes), std::cend(genotypes), std::begin(likelihoods),
                                      [&likelihood_model] (const auto& genotype) {
                                          return likelihood_model.evaluate(genotype);
                                      });
                       return likelihoods;
                   });
    return result;
}

GenotypeLogMarginalVector
init_genotype_log_marginals(const std::vector<Genotype<Haplotype>>& genotypes,
                            const HardyWeinbergModel& hw_model)
{
    GenotypeLogMarginalVector result {};
    result.reserve(genotypes.size());
    for (const auto& genotype : genotypes) {
        result.push_back({genotype, hw_model.evaluate(genotype)});
    }
    return result;
}

void update_genotype_log_marginals(GenotypeLogMarginalVector& current_log_marginals,
                                   const HardyWeinbergModel& hw_model)
{
    std::for_each(std::begin(current_log_marginals), std::end(current_log_marginals),
                  [&hw_model] (auto& p) { p.log_probability = hw_model.evaluate(p.genotype); });
}

GenotypeMarginalPosteriorMatrix
init_genotype_posteriors(const GenotypeLogMarginalVector& genotype_log_marginals,
                         const GenotypeLogLikelihoodMatrix& genotype_log_likilhoods)
{
    GenotypeMarginalPosteriorMatrix result {};
    result.reserve(genotype_log_likilhoods.size());
    for (const auto& sample_genotype_log_likilhoods : genotype_log_likilhoods) {
        GenotypeMarginalPosteriorVector posteriors(genotype_log_marginals.size());
        std::transform(std::cbegin(genotype_log_marginals), std::cend(genotype_log_marginals),
                       std::cbegin(sample_genotype_log_likilhoods), std::begin(posteriors),
                       [] (const auto& genotype_log_marginal, const auto genotype_log_likilhood) {
                           return genotype_log_marginal.log_probability + genotype_log_likilhood;
                       });
        maths::normalise_exp(posteriors);
        result.emplace_back(std::move(posteriors));
    }
    return result;
}

void update_genotype_posteriors(GenotypeMarginalPosteriorMatrix& current_genotype_posteriors,
                                const GenotypeLogMarginalVector& genotype_log_marginals,
                                const GenotypeLogLikelihoodMatrix& genotype_log_likilhoods)
{
    auto likelihood_itr = std::cbegin(genotype_log_likilhoods);
    for (auto& sample_genotype_posteriors : current_genotype_posteriors) {
        std::transform(std::cbegin(genotype_log_marginals), std::cend(genotype_log_marginals),
                       std::cbegin(*likelihood_itr++), std::begin(sample_genotype_posteriors),
                       [] (const auto& log_marginal, const auto& log_likeilhood) {
                           return log_marginal.log_probability + log_likeilhood;
                       });
        maths::normalise_exp(sample_genotype_posteriors);
    }
}

auto collapse_genotype_posteriors(const GenotypeMarginalPosteriorMatrix& genotype_posteriors)
{
    assert(!genotype_posteriors.empty());
    std::vector<double> result(genotype_posteriors.front().size());
    for (const auto& sample_posteriors : genotype_posteriors) {
        std::transform(std::cbegin(result), std::cend(result), std::cbegin(sample_posteriors), std::begin(result),
                       [] (const auto curr, const auto p) { return curr + p; });
    }
    return result;
}

double update_haplotype_frequencies(const std::vector<Haplotype>& haplotypes,
                                    HardyWeinbergModel& hw_model,
                                    const GenotypeMarginalPosteriorMatrix& genotype_posteriors,
                                    const InverseGenotypeTable& genotypes_containing_haplotypes,
                                    const double frequency_update_norm)
{
    const auto collaped_posteriors = collapse_genotype_posteriors(genotype_posteriors);
    double max_frequency_change {0};
    auto& current_haplotype_frequencies = hw_model.frequencies();
    for (std::size_t i {0}; i < haplotypes.size(); ++i) {
        auto& current_frequency = current_haplotype_frequencies.at(haplotypes[i]);
        double new_frequency {0};
        for (const auto& genotype_index : genotypes_containing_haplotypes[i]) {
            new_frequency += collaped_posteriors[genotype_index];
        }
        new_frequency /= frequency_update_norm;
        const auto frequency_change = std::abs(current_frequency - new_frequency);
        if (frequency_change > max_frequency_change) {
            max_frequency_change = frequency_change;
        }
        current_frequency = new_frequency;
    }
    return max_frequency_change;
}

double do_em_iteration(GenotypeMarginalPosteriorMatrix& genotype_posteriors,
                       HardyWeinbergModel& hw_model,
                       GenotypeLogMarginalVector& genotype_log_marginals,
                       const ModelConstants& constants)
{
    const auto max_change = update_haplotype_frequencies(constants.haplotypes,
                                                         hw_model,
                                                         genotype_posteriors,
                                                         constants.genotypes_containing_haplotypes,
                                                         constants.frequency_update_norm);
    update_genotype_log_marginals(genotype_log_marginals, hw_model);
    update_genotype_posteriors(genotype_posteriors, genotype_log_marginals, constants.genotype_log_likilhoods);
    return max_change;
}

void run_em(GenotypeMarginalPosteriorMatrix& genotype_posteriors,
            HardyWeinbergModel& hw_model,
            GenotypeLogMarginalVector& genotype_log_marginals,
            const ModelConstants& constants, const EMOptions options,
            boost::optional<logging::TraceLogger> trace_log = boost::none)
{
    for (unsigned n {1}; n <= options.max_iterations; ++n) {
        const auto max_change = do_em_iteration(genotype_posteriors, hw_model, genotype_log_marginals,constants);
        if (max_change <= options.epsilon) break;
    }
}

auto compute_approx_genotype_marginal_posteriors(const std::vector<Haplotype>& haplotypes,
                                                 const std::vector<Genotype<Haplotype>>& genotypes,
                                                 const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
                                                 const EMOptions options)
{
    const ModelConstants constants {haplotypes, genotypes, genotype_likelihoods};
    auto hw_model = make_hardy_weinberg_model(constants);
    auto genotype_log_marginals = init_genotype_log_marginals(genotypes, hw_model);
    auto result = init_genotype_posteriors(genotype_log_marginals, genotype_likelihoods);
    run_em(result, hw_model, genotype_log_marginals, constants, options);
    return result;
}

auto compute_approx_genotype_marginal_posteriors(const std::vector<Haplotype>& haplotypes,
                                                 const std::vector<Genotype<Haplotype>>& genotypes,
                                                 const std::vector<GenotypeIndex>& genotype_indices,
                                                 const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
                                                 const EMOptions options)
{
    const ModelConstants constants {haplotypes, genotypes, genotype_indices, genotype_likelihoods};
    auto hw_model = make_hardy_weinberg_model(constants);
    auto genotype_log_marginals = init_genotype_log_marginals(genotypes, hw_model);
    auto result = init_genotype_posteriors(genotype_log_marginals, genotype_likelihoods);
    run_em(result, hw_model, genotype_log_marginals, constants, options);
    return result;
}

auto compute_approx_genotype_marginal_posteriors(const std::vector<Genotype<Haplotype>>& genotypes,
                                                 const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
                                                 const EMOptions options)
{
    const auto haplotypes = extract_unique_elements(genotypes);
    return compute_approx_genotype_marginal_posteriors(haplotypes, genotypes, genotype_likelihoods, options);
}

using GenotypeCombinationVector = std::vector<std::size_t>;
using GenotypeCombinationMatrix = std::vector<GenotypeCombinationVector>;

auto log(std::size_t base, std::size_t x)
{
    return std::log2(x) / std::log2(base);
}

auto num_combinations(const std::size_t num_genotypes, const std::size_t num_samples)
{
    static constexpr auto max_combinations = std::numeric_limits<std::size_t>::max();
    if (num_samples <= log(num_genotypes, max_combinations)) {
        return static_cast<std::size_t>(std::pow(num_genotypes, num_samples));
    } else {
        return max_combinations;
    }
}

auto generate_all_genotype_combinations(const std::size_t num_genotypes, const std::size_t num_samples)
{
    GenotypeCombinationMatrix result {};
    result.reserve(num_combinations(num_genotypes, num_samples));
    GenotypeCombinationVector tmp(num_samples);
    std::vector<bool> v(num_genotypes * num_samples);
    std::fill(std::begin(v), std::next(std::begin(v), num_samples), true);
    do {
        bool good {true};
        for (std::size_t i {0}, k {0}; k < num_samples; ++i) {
            if (v[i]) {
                if (i / num_genotypes == k) {
                    tmp[k++] = i - num_genotypes * (i / num_genotypes);
                } else {
                    good = false;
                    k = num_samples;
                }
            }
        }
        if (good) result.push_back(tmp);
    } while (std::prev_permutation(std::begin(v), std::end(v)));
    return result;
}

bool is_homozygous_reference(const Genotype<Haplotype>& genotype)
{
    assert(genotype.ploidy() > 0);
    return genotype.is_homozygous() && is_reference(genotype[0]);
}

boost::optional<std::size_t> find_hom_ref_idx(const std::vector<Genotype<Haplotype>>& genotypes)
{
    auto itr = std::find_if(std::cbegin(genotypes), std::cend(genotypes),
                            [] (const auto& g) { return is_homozygous_reference(g); });
    if (itr != std::cend(genotypes)) {
        return std::distance(std::cbegin(genotypes), itr);
    } else {
        return boost::none;
    }
}

template <typename T>
auto zip_index(const std::vector<T>& v)
{
    std::vector<std::pair<T, unsigned>> result(v.size());
    for (unsigned idx {0}; idx < v.size(); ++idx) {
        result[idx] = std::make_pair(v[idx], idx);
    }
    return result;
}

std::vector<unsigned>
select_top_k_genotypes(const std::vector<Genotype<Haplotype>>& genotypes,
                       const GenotypeMarginalPosteriorMatrix& em_genotype_marginals,
                       const std::size_t k)
{
    if (genotypes.size() <= k) {
        std::vector<unsigned> result(genotypes.size());
        std::iota(std::begin(result), std::end(result), 0);
        return result;
    } else {
        std::vector<std::vector<std::pair<double, unsigned>>> indexed_marginals {};
        indexed_marginals.reserve(em_genotype_marginals.size());
        for (const auto& marginals : em_genotype_marginals) {
            auto tmp = zip_index(marginals);
            std::nth_element(std::begin(tmp), std::next(std::begin(tmp), k), std::end(tmp), std::greater<> {});
            indexed_marginals.push_back(std::move(tmp));
        }
        std::vector<unsigned> result {}, top(genotypes.size(), 0u);
        result.reserve(k);
        for (std::size_t j {0}; j <= k; ++j) {
            for (const auto& marginals : indexed_marginals) {
                ++top[marginals.front().second];
            }
            const auto max_itr = std::max_element(std::begin(top), std::end(top));
            const auto max_idx = static_cast<unsigned>(std::distance(std::begin(top), max_itr));
            if (std::find(std::cbegin(result), std::cend(result), max_idx) == std::cend(result)) {
                result.push_back(max_idx);
            }
            *max_itr = 0;
            for (auto& marginals : indexed_marginals) {
                if (marginals.front().second == max_idx) {
                    marginals.erase(std::cbegin(marginals));
                }
            }
        }
        return result;
    }
}

auto propose_joint_genotypes(const std::vector<Genotype<Haplotype>>& genotypes,
                             const GenotypeMarginalPosteriorMatrix& em_genotype_marginals,
                             const std::size_t max_joint_genotypes)
{
    const auto num_samples = em_genotype_marginals.size();
    assert(max_joint_genotypes >= num_samples * genotypes.size());
    const auto num_joint_genotypes = num_combinations(genotypes.size(), num_samples);
    if (num_joint_genotypes <= max_joint_genotypes) {
        return generate_all_genotype_combinations(genotypes.size(), num_samples);
    }
    auto result = select_top_k_tuples(em_genotype_marginals, max_joint_genotypes);
    const auto top_k_genotype_indices = select_top_k_genotypes(genotypes, em_genotype_marginals, num_samples / 2);
    for (const auto genotype_idx : top_k_genotype_indices) {
        for (std::size_t sample_idx {0}; sample_idx < num_samples; ++sample_idx) {
            if (result.front()[sample_idx] != genotype_idx) {
                auto tmp = result.front();
                tmp[sample_idx] = genotype_idx;
                if (std::find(std::cbegin(result), std::cend(result), tmp) == std::cend(result)) {
                    result.push_back(std::move(tmp));
                }
            }
        }
    }
    const auto hom_ref_idx = find_hom_ref_idx(genotypes);
    if (hom_ref_idx) {
        std::vector<std::size_t> ref_indices(num_samples, *hom_ref_idx);
        if (std::find(std::cbegin(result), std::cend(result), ref_indices) == std::cend(result)) {
            result.back() = std::move(ref_indices);
        }
    }
    return result;
}

template <typename Container>
auto sum(const Container& values)
{
    return std::accumulate(std::cbegin(values), std::cend(values), 0.0);
}

void fill(const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
          const GenotypeCombinationVector& indices,
          GenotypeLogLikelihoodVector& result)
{
    assert(result.size() == indices.size());
    for (std::size_t s {0}; s < indices.size(); ++s) {
        result[s] = genotype_likelihoods[s][indices[s]];
    }
}

using GenotypeReferenceVector = std::vector<std::reference_wrapper<const Genotype<Haplotype>>>;

template <typename T, typename V>
void fill(const std::vector<T>& genotypes, const GenotypeCombinationVector& indices, V& result)
{
    result.clear();
    std::transform(std::cbegin(indices), std::cend(indices), std::back_inserter(result),
                   [&genotypes] (const auto index) { return std::cref(genotypes[index]); });
}

auto calculate_posteriors(const std::vector<Genotype<Haplotype>>& genotypes,
                          const GenotypeCombinationMatrix& joint_genotypes,
                          const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
                          const PopulationPriorModel& prior_model)
{
    std::vector<double> result {};
    GenotypeLogLikelihoodVector likelihoods_buffer(genotype_likelihoods.size());
    GenotypeReferenceVector genotypes_refs {};
    for (const auto& indices : joint_genotypes) {
        fill(genotype_likelihoods, indices, likelihoods_buffer);
        fill(genotypes, indices, genotypes_refs);
        result.push_back(prior_model.evaluate(genotypes_refs) + sum(likelihoods_buffer));
    }
    const auto norm = maths::normalise_exp(result);
    return std::make_pair(std::move(result), norm);
}

using GenotypeIndexRefVector = std::vector<PopulationPriorModel::GenotypeIndiceVectorReference>;

auto calculate_posteriors(const std::vector<GenotypeIndex>& genotype_indices,
                          const GenotypeCombinationMatrix& joint_genotypes,
                          const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
                          const PopulationPriorModel& prior_model)
{
    std::vector<double> result {};
    GenotypeLogLikelihoodVector likelihoods_buffer(genotype_likelihoods.size());
    GenotypeIndexRefVector genotypes_index_refs {};
    for (const auto& indices : joint_genotypes) {
        fill(genotype_likelihoods, indices, likelihoods_buffer);
        fill(genotype_indices, indices, genotypes_index_refs);
        result.push_back(prior_model.evaluate(genotypes_index_refs) + sum(likelihoods_buffer));
    }
    const auto norm = maths::normalise_exp(result);
    return std::make_pair(std::move(result), norm);
}

void set_posterior_marginals(const GenotypeCombinationMatrix& joint_genotypes,
                             const std::vector<double>& joint_posteriors,
                             const std::size_t num_genotypes, const std::size_t num_samples,
                             PopulationModel::InferredLatents& result)
{
    assert(joint_posteriors.size() == joint_genotypes.size());
    std::vector<std::vector<double>> marginals(num_samples, std::vector<double>(num_genotypes, 0.0));
    for (std::size_t i {0}; i < joint_genotypes.size(); ++i) {
        assert(joint_genotypes[i].size() == num_samples);
        for (std::size_t s {0}; s < num_samples; ++s) {
            marginals[s][joint_genotypes[i][s]] += joint_posteriors[i];
        }
    }
    result.posteriors.marginal_genotype_probabilities = std::move(marginals);
}

template <typename T>
void calculate_posterior_marginals(const std::vector<T>& genotypes,
                                   const GenotypeCombinationMatrix& joint_genotypes,
                                   const GenotypeLogLikelihoodMatrix& genotype_likelihoods,
                                   const PopulationPriorModel& prior_model,
                                   PopulationModel::InferredLatents& result)
{
    std::vector<double> joint_posteriors; double norm;
    std::tie(joint_posteriors, norm) = calculate_posteriors(genotypes, joint_genotypes, genotype_likelihoods, prior_model);
    const auto num_samples = genotype_likelihoods.size();
    set_posterior_marginals(joint_genotypes, joint_posteriors, genotypes.size(), num_samples, result);
    result.log_evidence = norm;
}

} // namespace

PopulationModel::InferredLatents
PopulationModel::evaluate(const SampleVector& samples, const GenotypeVector& genotypes,
                          const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    assert(!genotypes.empty());
    const auto genotype_log_likelihoods = compute_genotype_log_likelihoods(samples, genotypes, haplotype_likelihoods);
    const auto num_joint_genotypes = num_combinations(genotypes.size(), samples.size());
    InferredLatents result;
    if (num_joint_genotypes <= options_.max_joint_genotypes) {
        const auto joint_genotypes = generate_all_genotype_combinations(genotypes.size(), samples.size());
        calculate_posterior_marginals(genotypes, joint_genotypes, genotype_log_likelihoods, prior_model_, result);
    } else {
        const EMOptions em_options {options_.max_em_iterations, options_.em_epsilon};
        const auto em_genotype_marginals = compute_approx_genotype_marginal_posteriors(genotypes, genotype_log_likelihoods, em_options);
        const auto joint_genotypes = propose_joint_genotypes(genotypes, em_genotype_marginals, options_.max_joint_genotypes);
        calculate_posterior_marginals(genotypes, joint_genotypes, genotype_log_likelihoods, prior_model_, result);
    }
    return result;
}

PopulationModel::InferredLatents
PopulationModel::evaluate(const SampleVector& samples,
                          const GenotypeVector& genotypes,
                          const std::vector<GenotypeIndex>& genotype_indices,
                          const std::vector<Haplotype>& haplotypes,
                          const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    assert(!genotypes.empty());
    const auto genotype_log_likelihoods = compute_genotype_log_likelihoods(samples, genotypes, haplotype_likelihoods);
    const auto num_joint_genotypes = num_combinations(genotypes.size(), samples.size());
    InferredLatents result;
    if (num_joint_genotypes <= options_.max_joint_genotypes) {
        const auto joint_genotypes = generate_all_genotype_combinations(genotypes.size(), samples.size());
        calculate_posterior_marginals(genotypes, joint_genotypes, genotype_log_likelihoods, prior_model_, result);
    } else {
        const EMOptions em_options {options_.max_em_iterations, options_.em_epsilon};
        const auto em_genotype_marginals = compute_approx_genotype_marginal_posteriors(haplotypes, genotypes, genotype_indices,
                                                                                       genotype_log_likelihoods, em_options);
        const auto joint_genotypes = propose_joint_genotypes(genotypes, em_genotype_marginals, options_.max_joint_genotypes);
        calculate_posterior_marginals(genotype_indices, joint_genotypes, genotype_log_likelihoods, prior_model_, result);
    }
    return result;
}

PopulationModel::InferredLatents
PopulationModel::evaluate(const SampleVector& samples,
                          const std::vector<GenotypeVectorReference>& genotypes,
                          const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    return InferredLatents {};
}

namespace debug {
    
} // namespace debug

} // namespace model
} // namespace octopus
