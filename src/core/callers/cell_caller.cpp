// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "cell_caller.hpp"

#include <typeinfo>
#include <unordered_map>
#include <deque>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <utility>
#include <stdexcept>
#include <iostream>

#include <boost/iterator/zip_iterator.hpp>
#include <boost/tuple/tuple.hpp>

#include "basics/genomic_region.hpp"
#include "containers/probability_matrix.hpp"
#include "core/types/allele.hpp"
#include "core/types/variant.hpp"
#include "core/types/phylogeny.hpp"
#include "core/types/calls/cell_variant_call.hpp"
#include "core/types/calls/reference_call.hpp"
#include "core/models/genotype/uniform_genotype_prior_model.hpp"
#include "core/models/genotype/coalescent_genotype_prior_model.hpp"
#include "core/models/mutation/denovo_model.hpp"
#include "core/models/genotype/single_cell_prior_model.hpp"
#include "utils/maths.hpp"
#include "logging/logging.hpp"

namespace octopus {

CellCaller::CellCaller(Caller::Components&& components,
                       Caller::Parameters general_parameters,
                       Parameters specific_parameters)
: Caller {std::move(components), std::move(general_parameters)}
, parameters_{std::move(specific_parameters)}
{}

std::string CellCaller::do_name() const
{
    return "cell";
}

CellCaller::CallTypeSet CellCaller::do_call_types() const
{
    return {std::type_index(typeid(CellVariantCall))};
}

unsigned CellCaller::do_min_callable_ploidy() const
{
    return parameters_.ploidy;
}

unsigned CellCaller::do_max_callable_ploidy() const
{
    return parameters_.ploidy;
}

std::size_t CellCaller::do_remove_duplicates(std::vector<Haplotype>& haplotypes) const
{
    if (parameters_.deduplicate_haplotypes_with_prior_model) {
        if (haplotypes.size() < 2) return 0;
        CoalescentModel::Parameters model_params {};
        if (parameters_.prior_model_params) model_params = *parameters_.prior_model_params;
        Haplotype reference {mapped_region(haplotypes.front()), reference_.get()};
        CoalescentModel model {std::move(reference), model_params, haplotypes.size(), CoalescentModel::CachingStrategy::none};
        const CoalescentProbabilityGreater cmp {std::move(model)};
        return octopus::remove_duplicates(haplotypes, cmp);
    } else {
        return Caller::do_remove_duplicates(haplotypes);
    }
}

// CellCaller::Latents public methods

CellCaller::Latents::Latents(const CellCaller& caller,
                             std::vector<Haplotype> haplotypes,
                             std::vector<Genotype<Haplotype>> genotypes,
                             std::vector<model::SingleCellModel::Inferences> inferences)
: caller_ {caller}
, haplotypes_ {std::move(haplotypes)}
, genotypes_ {std::move(genotypes)}
, phylogeny_inferences_ {std::move(inferences)}
{
    phylogeny_posteriors_.resize(phylogeny_inferences_.size());
    std::transform(std::cbegin(phylogeny_inferences_), std::cend(phylogeny_inferences_), std::begin(phylogeny_posteriors_),
                   [] (const auto& inferences) { return inferences.log_evidence; });
    maths::normalise_exp(phylogeny_posteriors_);
}

namespace {

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
        auto itr = result_map.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(std::cref(haplotype)),
                                      std::forward_as_tuple());
        itr.first->second.reserve(cardinality);
    }
    for (std::size_t i {0}; i < genotypes.size(); ++i) {
        for (const auto& haplotype : genotypes[i]) {
            result_map.at(haplotype).emplace_back(i);
        }
    }
    InverseGenotypeTable result {};
    result.reserve(haplotypes.size());
    for (const auto& haplotype : haplotypes) {
        auto& indices = result_map.at(haplotype);
        std::sort(std::begin(indices), std::end(indices));
        indices.erase(std::unique(std::begin(indices), std::end(indices)), std::end(indices));
        result.emplace_back(std::move(indices));
    }
    return result;
}

using GenotypeMarginalPosteriorVector = std::vector<double>;
using GenotypeMarginalPosteriorMatrix = std::vector<GenotypeMarginalPosteriorVector>;

auto calculate_haplotype_posteriors(const std::vector<Haplotype>& haplotypes,
                                    const std::vector<Genotype<Haplotype>>& genotypes,
                                    const ProbabilityMatrix<Genotype<Haplotype>>& genotype_posteriors,
                                    const InverseGenotypeTable& inverse_genotypes)
{
    std::unordered_map<std::reference_wrapper<const Haplotype>, double> result {haplotypes.size()};
    auto itr = std::cbegin(inverse_genotypes);
    std::vector<std::size_t> genotype_indices(genotypes.size());
    std::iota(std::begin(genotype_indices), std::end(genotype_indices), 0);
    // noncontaining genotypes are genotypes that do not contain a particular haplotype.
    const auto num_noncontaining_genotypes = genotypes.size() - itr->size();
    std::vector<std::size_t> noncontaining_genotype_indices(num_noncontaining_genotypes);
    for (const auto& haplotype : haplotypes) {
        std::set_difference(std::cbegin(genotype_indices), std::cend(genotype_indices),
                            std::cbegin(*itr), std::cend(*itr),
                            std::begin(noncontaining_genotype_indices));
        double prob_not_observed {1};
        for (const auto& p : genotype_posteriors) {
            const auto slice = genotype_posteriors(p.first);
            std::vector<double> sample_genotype_posteriors {std::cbegin(slice), std::cend(slice)};
            prob_not_observed *= std::accumulate(std::cbegin(noncontaining_genotype_indices),
                                                 std::cend(noncontaining_genotype_indices),
                                                 0.0, [&sample_genotype_posteriors]
                                                 (const auto curr, const auto i) {
                return curr + sample_genotype_posteriors[i];
            });
        }
        result.emplace(haplotype, 1.0 - prob_not_observed);
        ++itr;
    }
    return result;
}

auto calculate_haplotype_posteriors(const std::vector<Haplotype>& haplotypes,
                                    const std::vector<Genotype<Haplotype>>& genotypes,
                                    const ProbabilityMatrix<Genotype<Haplotype>>& genotype_posteriors)
{
    const auto inverse_genotypes = make_inverse_genotype_table(haplotypes, genotypes);
    return calculate_haplotype_posteriors(haplotypes, genotypes, genotype_posteriors, inverse_genotypes);
}

} // namespace

std::shared_ptr<CellCaller::Latents::HaplotypeProbabilityMap>
CellCaller::Latents::haplotype_posteriors() const noexcept
{
    if (haplotype_posteriors_ == nullptr) {
        const auto marginal_genotype_posteriors = this->genotype_posteriors();
        haplotype_posteriors_ = std::make_unique<HaplotypeProbabilityMap>(calculate_haplotype_posteriors(haplotypes_, genotypes_, *marginal_genotype_posteriors));
    }
    return haplotype_posteriors_;
}

namespace {

template <typename... T>
auto zip(const T&... containers) -> boost::iterator_range<boost::zip_iterator<decltype(boost::make_tuple(std::begin(containers)...))>>
{
    auto zip_begin = boost::make_zip_iterator(boost::make_tuple(std::begin(containers)...));
    auto zip_end   = boost::make_zip_iterator(boost::make_tuple(std::end(containers)...));
    return boost::make_iterator_range(zip_begin, zip_end);
}

} // namespace

std::shared_ptr<CellCaller::Latents::GenotypeProbabilityMap>
CellCaller::Latents::genotype_posteriors() const noexcept
{
    if (genotype_posteriors_ == nullptr) {
        genotype_posteriors_ = std::make_unique<GenotypeProbabilityMap>(std::cbegin(genotypes_), std::cend(genotypes_));
        for (std::size_t sample_idx {0}; sample_idx < caller_.samples_.size(); ++sample_idx) {
            std::vector<double> marginal_genotype_posteriors(genotypes_.size());
            for (const auto& p : zip(phylogeny_inferences_, phylogeny_posteriors_)) {
                const auto& phylogeny = p.get<0>().phylogeny;
                for (unsigned t {0}; t < phylogeny.size(); ++t) {
                    const auto& group = phylogeny.group(t).value;
                    for (std::size_t genotype_idx {0}; genotype_idx < group.genotype_posteriors.size(); ++genotype_idx) {
                        marginal_genotype_posteriors[genotype_idx] += p.get<1>()
                                * group.sample_attachment_posteriors[sample_idx]
                                * group.genotype_posteriors[genotype_idx];
                    }
                }
            }
            insert_sample(caller_.samples_[sample_idx], std::move(marginal_genotype_posteriors), *genotype_posteriors_);
        }
    }
    return genotype_posteriors_;
}

// CellCaller::Latents private methods

template <typename S>
void log(const model::SingleCellModel::Inferences& inferences,
         const std::vector<SampleName>& samples,
         const std::vector<Genotype<Haplotype>>& genotypes,
         S&& logger)
{
    std::vector<std::size_t> map_genotypes {};
    map_genotypes.reserve(inferences.phylogeny.size());
    std::vector<std::pair<std::size_t, double>> map_sample_assignments(samples.size());
    for (std::size_t group_id {0}; group_id < inferences.phylogeny.size(); ++group_id) {
        const auto& group = inferences.phylogeny.group(group_id).value;
        auto map_itr = std::max_element(std::cbegin(group.genotype_posteriors), std::cend(group.genotype_posteriors));
        auto map_idx = static_cast<std::size_t>(std::distance(std::cbegin(group.genotype_posteriors), map_itr));
        map_genotypes.push_back(map_idx);
        for (std::size_t sample_idx {0}; sample_idx < samples.size(); ++sample_idx) {
            if (group.sample_attachment_posteriors[sample_idx] > map_sample_assignments[sample_idx].second) {
                map_sample_assignments[sample_idx].first = group_id;
                map_sample_assignments[sample_idx].second = group.sample_attachment_posteriors[sample_idx];
            }
        }
    }
    logger << "MAP genotypes: " << '\n';
    for (std::size_t group_id {0}; group_id < map_genotypes.size(); ++group_id) {
        logger << group_id << ": "; debug::print_variant_alleles(logger, genotypes[map_genotypes[group_id]]); logger << '\n';
    }
    logger << "Sample MAP assignments:" << '\n';
    for (std::size_t sample_idx {0}; sample_idx < samples.size(); ++sample_idx) {
        logger << samples[sample_idx] << ": " << map_sample_assignments[sample_idx].first
               << " (" << map_sample_assignments[sample_idx].second << ")\n";
    }
    logger << "Evidence: " << inferences.log_evidence << '\n';
}

void log(const model::SingleCellModel::Inferences& inferences,
         const std::vector<SampleName>& samples,
         const std::vector<Genotype<Haplotype>>& genotypes,
         boost::optional<logging::DebugLogger>& logger)
{
    if (logger) {
        log(inferences, samples, genotypes, stream(*logger));
    }
}

std::unique_ptr<CellCaller::Caller::Latents>
CellCaller::infer_latents(const std::vector<Haplotype>& haplotypes, const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    std::vector<GenotypeIndex> genotype_indices {};
    auto genotypes = generate_all_genotypes(haplotypes, parameters_.ploidy, genotype_indices);
    if (debug_log_) stream(*debug_log_) << "There are " << genotypes.size() << " candidate genotypes";
    const auto genotype_prior_model = make_prior_model(haplotypes);
    DeNovoModel mutation_model {parameters_.mutation_model_parameters};
    model::SingleCellPriorModel::Parameters cell_prior_params {};
    cell_prior_params.copy_number_log_probability = std::log(1e-6);
    model::SingleCellModel::Parameters model_parameters {};
    model_parameters.dropout_concentration = parameters_.dropout_concentration;
    model_parameters.group_concentration = 1.0;
    model::SingleCellModel::AlgorithmParameters config {};
    config.max_genotype_combinations = parameters_.max_joint_genotypes;
    if (parameters_.max_vb_seeds) config.max_seeds = *parameters_.max_vb_seeds;
    
    using CellPhylogeny =  model::SingleCellPriorModel::CellPhylogeny;
    CellPhylogeny single_group_phylogeny {CellPhylogeny::Group {0}};
    model::SingleCellPriorModel single_group_prior_model {std::move(single_group_phylogeny), *genotype_prior_model, mutation_model, cell_prior_params};
    model::SingleCellModel single_group_model {samples_, std::move(single_group_prior_model), model_parameters, config};
    auto single_group_inferences = single_group_model.evaluate(genotypes, haplotype_likelihoods);
    
    CellPhylogeny two_group_phylogeny {CellPhylogeny::Group {0}};
    two_group_phylogeny.add_descendant(CellPhylogeny::Group {1}, 0);
    model::SingleCellPriorModel two_group_prior_model {std::move(two_group_phylogeny), *genotype_prior_model, mutation_model, cell_prior_params};
    model::SingleCellModel two_group_model {samples_, std::move(two_group_prior_model), model_parameters, config};
    auto two_group_inferences = two_group_model.evaluate(genotypes, haplotype_likelihoods);
    
    log(single_group_inferences, samples_, genotypes, debug_log_);
    log(two_group_inferences, samples_, genotypes, debug_log_);
    
    std::vector<model::SingleCellModel::Inferences> inferences {std::move(single_group_inferences), std::move(two_group_inferences)};
    return std::make_unique<Latents>(*this, haplotypes, std::move(genotypes), std::move(inferences));
}

boost::optional<double>
CellCaller::calculate_model_posterior(const std::vector<Haplotype>& haplotypes,
                                      const HaplotypeLikelihoodArray& haplotype_likelihoods,
                                      const Caller::Latents& latents) const
{
    return calculate_model_posterior(haplotypes, haplotype_likelihoods, dynamic_cast<const Latents&>(latents));
}

boost::optional<double>
CellCaller::calculate_model_posterior(const std::vector<Haplotype>& haplotypes,
                                      const HaplotypeLikelihoodArray& haplotype_likelihoods,
                                      const Latents& latents) const
{
    return boost::none;
}

std::vector<std::unique_ptr<octopus::VariantCall>>
CellCaller::call_variants(const std::vector<Variant>& candidates, const Caller::Latents& latents) const
{
    return call_variants(candidates, dynamic_cast<const Latents&>(latents));
}

namespace {

using GenotypeProbabilityMap = ProbabilityMatrix<Genotype<Haplotype>>::InnerMap;
using PopulationGenotypeProbabilityMap = ProbabilityMatrix<Genotype<Haplotype>>;

using VariantReference = std::reference_wrapper<const Variant>;
using VariantPosteriorVector = std::vector<std::pair<VariantReference, std::vector<Phred<double>>>>;

struct VariantCall : Mappable<VariantCall>
{
    VariantCall() = delete;
    VariantCall(const std::pair<VariantReference, std::vector<Phred<double>>>& p)
    : variant {p.first}
    , posteriors {p.second}
    {}
    VariantCall(const Variant& variant, std::vector<Phred<double>> posterior)
    : variant {variant}
    , posteriors {posterior}
    {}
    
    const GenomicRegion& mapped_region() const noexcept
    {
        return octopus::mapped_region(variant.get());
    }
    
    VariantReference variant;
    std::vector<Phred<double>> posteriors;
};

using VariantCalls = std::vector<VariantCall>;

struct GenotypeCall
{
    Genotype<Allele> genotype;
    Phred<double> posterior;
};

using GenotypeCalls = std::vector<std::vector<GenotypeCall>>;

// allele posterior calculations

using AlleleBools           = std::deque<bool>; // using std::deque because std::vector<bool> is evil
using GenotypePropertyBools = std::vector<AlleleBools>;

auto marginalise(const GenotypeProbabilityMap& genotype_posteriors,
                 const AlleleBools& contained_alleles)
{
    auto p = std::inner_product(std::cbegin(genotype_posteriors), std::cend(genotype_posteriors),
                                std::cbegin(contained_alleles), 0.0, std::plus<> {},
                                [] (const auto& p, const bool is_contained) {
                                    return is_contained ? 0.0 : p.second;
                                });
    return probability_false_to_phred(p);
}

auto compute_sample_allele_posteriors(const GenotypeProbabilityMap& genotype_posteriors,
                                      const GenotypePropertyBools& contained_alleles)
{
    std::vector<Phred<double>> result {};
    result.reserve(contained_alleles.size());
    for (const auto& allele : contained_alleles) {
        result.emplace_back(marginalise(genotype_posteriors, allele));
    }
    return result;
}

auto get_contained_alleles(const PopulationGenotypeProbabilityMap& genotype_posteriors,
                           const std::vector<Allele>& alleles)
{
    const auto num_genotypes = genotype_posteriors.size2();
    GenotypePropertyBools result {};
    if (num_genotypes == 0 || genotype_posteriors.empty1() || alleles.empty()) {
        return result;
    }
    result.reserve(alleles.size());
    const auto& test_sample   = genotype_posteriors.begin()->first;
    const auto genotype_begin = genotype_posteriors.begin(test_sample);
    const auto genotype_end   = genotype_posteriors.end(test_sample);
    for (const auto& allele : alleles) {
        result.emplace_back(num_genotypes);
        std::transform(genotype_begin, genotype_end, std::begin(result.back()),
                       [&] (const auto& p) { return contains(p.first, allele); });
    }
    return result;
}

using AllelePosteriorMatrix = std::vector<std::vector<Phred<double>>>;

auto compute_posteriors(const std::vector<SampleName>& samples,
                        const std::vector<Allele>& alleles,
                        const PopulationGenotypeProbabilityMap& genotype_posteriors)
{
    const auto contained_alleles = get_contained_alleles(genotype_posteriors, alleles);
    AllelePosteriorMatrix result {};
    result.reserve(genotype_posteriors.size1());
    for (const auto& sample : samples) {
        result.emplace_back(compute_sample_allele_posteriors(genotype_posteriors[sample], contained_alleles));
    }
    return result;
}

auto extract_ref_alleles(const std::vector<Variant>& variants)
{
    std::vector<Allele> result {};
    result.reserve(variants.size());
    std::transform(std::cbegin(variants), std::cend(variants), std::back_inserter(result),
                   [] (const auto& variant) { return variant.ref_allele(); });
    return result;
}

auto extract_alt_alleles(const std::vector<Variant>& variants)
{
    std::vector<Allele> result {};
    result.reserve(variants.size());
    std::transform(std::cbegin(variants), std::cend(variants), std::back_inserter(result),
                   [] (const auto& variant) { return variant.alt_allele(); });
    return result;
}

auto compute_posteriors(const std::vector<SampleName>& samples,
                        const std::vector<Variant>& variants,
                        const PopulationGenotypeProbabilityMap& genotype_posteriors)
{
    const auto allele_posteriors = compute_posteriors(samples, extract_alt_alleles(variants), genotype_posteriors);
    VariantPosteriorVector result {};
    result.reserve(variants.size());
    for (std::size_t i {0}; i < variants.size(); ++i) {
        std::vector<Phred<double>> sample_posteriors(samples.size());
        std::transform(std::cbegin(allele_posteriors), std::cend(allele_posteriors), std::begin(sample_posteriors),
                       [i] (const auto& ps) { return ps[i]; });
        result.emplace_back(variants[i], std::move(sample_posteriors));
    }
    return result;
}

// haplotype genotype calling

auto call_genotype(const PopulationGenotypeProbabilityMap::InnerMap& genotype_posteriors)
{
    return std::max_element(std::cbegin(genotype_posteriors), std::cend(genotype_posteriors),
                            [] (const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; })->first;
}

auto call_genotypes(const std::vector<SampleName>& samples, const PopulationGenotypeProbabilityMap& genotype_posteriors)
{
    std::vector<Genotype<Haplotype>> result {};
    result.reserve(samples.size());
    for (const auto& sample : samples) {
        result.push_back(call_genotype(genotype_posteriors[sample]));
    }
    return result;
}

// variant calling

bool has_above(const std::vector<Phred<double>>& posteriors, const Phred<double> min_posterior)
{
    return std::any_of(std::cbegin(posteriors), std::cend(posteriors), [=] (auto p) { return p >= min_posterior; });
}

bool contains_alt(const Genotype<Haplotype>& genotype_call, const VariantReference& candidate)
{
    return includes(genotype_call, candidate.get().alt_allele());
}

bool contains_alt(const std::vector<Genotype<Haplotype>>& genotype_calls, const VariantReference& candidate)
{
    return std::any_of(std::cbegin(genotype_calls), std::cend(genotype_calls),
                       [&] (const auto& genotype) { return contains_alt(genotype, candidate); });
}

VariantCalls call_candidates(const VariantPosteriorVector& candidate_posteriors,
                             const std::vector<Genotype<Haplotype>>& genotype_calls,
                             const Phred<double> min_posterior)
{
    VariantCalls result {};
    result.reserve(candidate_posteriors.size());
    std::copy_if(std::cbegin(candidate_posteriors), std::cend(candidate_posteriors),
                 std::back_inserter(result),
                 [&genotype_calls, min_posterior] (const auto& p) {
                     return has_above(p.second, min_posterior) && contains_alt(genotype_calls, p.first);
                 });
    return result;
}

// allele genotype calling

auto marginalise(const Genotype<Allele>& genotype, const GenotypeProbabilityMap& genotype_posteriors)
{
    auto p = std::accumulate(std::cbegin(genotype_posteriors), std::cend(genotype_posteriors), 0.0,
                             [&genotype] (const double curr, const auto& p) {
                                 return curr + (contains(p.first, genotype) ? 0.0 : p.second);
                             });
    return probability_false_to_phred(p);
}

auto call_genotypes(const std::vector<SampleName>& samples,
                    const std::vector<Genotype<Haplotype>>& genotype_calls,
                    const PopulationGenotypeProbabilityMap& genotype_posteriors,
                    const std::vector<GenomicRegion>& variant_regions)
{
    GenotypeCalls result {};
    result.reserve(variant_regions.size());
    for (const auto& region : variant_regions) {
        std::vector<GenotypeCall> region_calls {};
        region_calls.reserve(samples.size());
        for (std::size_t s {0}; s < samples.size(); ++s) {
            auto genotype_chunk = copy<Allele>(genotype_calls[s], region);
            const auto posterior = marginalise(genotype_chunk, genotype_posteriors[samples[s]]);
            region_calls.push_back({std::move(genotype_chunk), posterior});
        }
        result.push_back(std::move(region_calls));
    }
    return result;
}

// output

octopus::VariantCall::GenotypeCall convert(GenotypeCall&& call)
{
    return octopus::VariantCall::GenotypeCall {std::move(call.genotype), call.posterior};
}

std::unique_ptr<octopus::VariantCall>
transform_call(const std::vector<SampleName>& samples,
               VariantCall&& variant_call,
               std::vector<GenotypeCall>&& sample_genotype_calls)
{
    std::vector<std::pair<SampleName, Call::GenotypeCall>> tmp {};
    tmp.reserve(samples.size());
    std::transform(std::cbegin(samples), std::cend(samples),
                   std::make_move_iterator(std::begin(sample_genotype_calls)),
                   std::back_inserter(tmp),
                   [] (const auto& sample, auto&& genotype) {
                       return std::make_pair(sample, convert(std::move(genotype)));
                   });
    auto quality = *std::max_element(std::cbegin(variant_call.posteriors), std::cend(variant_call.posteriors));
    return std::make_unique<CellVariantCall>(variant_call.variant.get(), std::move(tmp), quality);
}

auto transform_calls(const std::vector<SampleName>& samples,
                     VariantCalls&& variant_calls,
                     GenotypeCalls&& genotype_calls)
{
    std::vector<std::unique_ptr<octopus::VariantCall>> result {};
    result.reserve(variant_calls.size());
    std::transform(std::make_move_iterator(std::begin(variant_calls)), std::make_move_iterator(std::end(variant_calls)),
                   std::make_move_iterator(std::begin(genotype_calls)), std::back_inserter(result),
                   [&samples] (auto&& variant_call, auto&& genotype_call) {
                       return transform_call(samples, std::move(variant_call), std::move(genotype_call));
                   });
    return result;
}

} // namespace

std::vector<std::unique_ptr<octopus::VariantCall>>
CellCaller::call_variants(const std::vector<Variant>& candidates, const Latents& latents) const
{
    const auto& genotype_posteriors = *(latents.genotype_posteriors());
    const auto sample_candidate_posteriors = compute_posteriors(samples_, candidates, genotype_posteriors);
    const auto genotype_calls = call_genotypes(samples_, genotype_posteriors);
    auto variant_calls = call_candidates(sample_candidate_posteriors, genotype_calls, parameters_.min_variant_posterior);
    const auto called_regions = extract_regions(variant_calls);
    auto allele_genotype_calls = call_genotypes(samples_, genotype_calls, genotype_posteriors, called_regions);
    return transform_calls(samples_, std::move(variant_calls), std::move(allele_genotype_calls));
}

std::vector<std::unique_ptr<ReferenceCall>>
CellCaller::call_reference(const std::vector<Allele>& alleles, const Caller::Latents& latents, const ReadPileupMap& pileup) const
{
    return call_reference(alleles, dynamic_cast<const Latents&>(latents), pileup);
}

std::vector<std::unique_ptr<ReferenceCall>>
CellCaller::call_reference(const std::vector<Allele>& alleles, const Latents& latents, const ReadPileupMap& pileup) const
{
    return {};
}

std::unique_ptr<GenotypePriorModel> CellCaller::make_prior_model(const std::vector<Haplotype>& haplotypes) const
{
    if (parameters_.prior_model_params) {
        return std::make_unique<CoalescentGenotypePriorModel>(CoalescentModel {
        Haplotype {mapped_region(haplotypes.front()), reference_},
        *parameters_.prior_model_params, haplotypes.size(), CoalescentModel::CachingStrategy::address
        });
    } else {
        return std::make_unique<UniformGenotypePriorModel>();
    }
}

} // namespace octopus
