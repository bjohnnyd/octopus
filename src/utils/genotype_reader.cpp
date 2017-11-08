// Copyright (c) 2017 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "genotype_reader.hpp"

#include <utility>
#include <functional>
#include <iterator>
#include <algorithm>
#include <initializer_list>
#include <cassert>

#include <boost/lexical_cast.hpp>

#include "basics/contig_region.hpp"
#include "basics/genomic_region.hpp"
#include "concepts/mappable.hpp"
#include "utils/mappable_algorithms.hpp"
#include "core/types/allele.hpp"
#include "core/types/variant.hpp"
#include "io/variant/vcf_header.hpp"
#include "io/variant/vcf_spec.hpp"
#include "io/reference/reference_genome.hpp"

#include <iostream> // DEBUG

namespace octopus {

namespace {

auto extract_phase_region(const VcfRecord& call, const SampleName& sample)
{
    auto result = get_phase_region(call, sample);
    if (result) return *result;
    return GenomicRegion {call.chrom(), contig_region(call)};
}

struct CallWrapper : public Mappable<CallWrapper>
{
    CallWrapper(const VcfRecord& record, const SampleName& sample)
    : call {std::cref(record)}
    , phase_region {extract_phase_region(record, sample)}
    {}
    
    std::reference_wrapper<const VcfRecord> call;
    GenomicRegion phase_region;
    const GenomicRegion& mapped_region() const noexcept { return phase_region; }
};

auto wrap_calls(const std::vector<VcfRecord>& calls, const SampleName& sample)
{
    std::vector<CallWrapper> result {};
    result.reserve(calls.size());
    for (const auto& call : calls) {
        result.emplace_back(call, sample);
    }
    return result;
}

decltype(auto) extract_genotype(const CallWrapper& call, const SampleName& sample)
{
    return get_genotype(call.call, sample);
}

auto extract_ploidy(const std::vector<CallWrapper>& phased_calls, const SampleName& sample)
{
    assert(!phased_calls.empty());
    return extract_genotype(phased_calls.front(), sample).size();
}

auto make_genotype(std::vector<Haplotype::Builder>&& haplotypes)
{
    Genotype<Haplotype> result {static_cast<unsigned>(haplotypes.size())};
    for (auto& haplotype : haplotypes) {
        result.emplace(haplotype.build());
    }
    return result;
}

auto mapped_contig_region(const CallWrapper& call)
{
    return contig_region(call.call.get());
}

bool is_missing(const VcfRecord::NucleotideSequence& allele)
{
    const std::string deleted_sequence {vcfspec::deletedBase};
    return allele == vcfspec::missingValue || allele == deleted_sequence;
}

auto make_allele(const ContigRegion& region, const VcfRecord::NucleotideSequence& ref_allele,
                 const VcfRecord::NucleotideSequence& alt_allele)
{
    Variant tmp {"$", region.begin(), ref_allele, alt_allele};
    if (!can_trim(tmp)) {
        return ContigAllele {region, alt_allele};
    }
    return demote(trim(tmp).alt_allele());
}

Genotype<Haplotype> extract_genotype(const std::vector<CallWrapper>& phased_calls,
                                     const GenomicRegion& region,
                                     const SampleName& sample,
                                     const ReferenceGenome& reference)
{
    assert(!phased_calls.empty());
    assert(contains(region, encompassing_region(phased_calls)));
    const auto ploidy = extract_ploidy(phased_calls, sample);
    std::vector<Haplotype::Builder> haplotypes(ploidy, Haplotype::Builder {region, reference});
    
    for (const auto& call : phased_calls) {
        const auto& genotype = extract_genotype(call, sample);
        for (unsigned i {0}; i < ploidy; ++i) {
            if (!is_missing(genotype[i])) {
                try {
                    haplotypes[i].push_back(make_allele(mapped_contig_region(call), call.call.get().ref(), genotype[i]));
                } catch (const std::logic_error& e) {
                    // can happen on overlapping reference, or if the VCF format is bad
                }
            }
        }
    }
    
    return make_genotype(std::move(haplotypes));
}

} // namespace

GenotypeMap extract_genotypes(const std::vector<VcfRecord>& calls,
                              const std::vector<VcfRecord::SampleName>& samples,
                              const ReferenceGenome& reference,
                              boost::optional<GenomicRegion> call_region)
{
    if (calls.empty()) return {};
    GenotypeMap result {samples.size()};
    
    for (const auto& sample : samples) {
        const auto wrapped_calls = segment_overlapped_copy(wrap_calls(calls, sample));
        using InitList = std::initializer_list<Genotype<Haplotype>>;
        if (wrapped_calls.size() == 1) {
            if (!call_region) {
                call_region = encompassing_region(wrapped_calls.front());
            }
            result.emplace(std::piecewise_construct,
                           std::forward_as_tuple(sample),
                           std::forward_as_tuple(InitList {
                                extract_genotype(wrapped_calls.front(), *call_region, sample, reference)
                            }));
        } else { // wrapped_calls.size() > 1
            auto it = std::cbegin(wrapped_calls);
            GenomicRegion region;
            if (call_region) {
                region = left_overhang_region(*call_region, std::next(it)->front());
            } else {
                region = left_overhang_region(it->front(), std::next(it)->front());
            }
            result.emplace(std::piecewise_construct,
                           std::forward_as_tuple(sample),
                           std::forward_as_tuple(InitList {
                                extract_genotype(*it, region, sample, reference)
                            }));
            ++it;
            for (auto penultimate = std::prev(std::cend(wrapped_calls)); it != penultimate; ++it) {
                region = *intervening_region(std::prev(it)->back(), std::next(it)->front());
                result.at(sample).insert(extract_genotype(*it, region, sample, reference));
            }
            if (call_region) {
                region = right_overhang_region(*call_region, std::prev(it)->back());
            } else {
                region = right_overhang_region(it->back(), std::prev(it)->back());
            }
            result.at(sample).insert(extract_genotype(*it, region, sample, reference));
        }
    }
    
    return result;
}
} // namespace octopus
