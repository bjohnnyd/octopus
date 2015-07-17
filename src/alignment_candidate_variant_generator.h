//
//  alignment_candidate_variant_generator.h
//  Octopus
//
//  Created by Daniel Cooke on 28/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__alignment_candidate_variant_generator__
#define __Octopus__alignment_candidate_variant_generator__

#include <vector>
#include <cstddef> // std::size_t

#include "i_candidate_variant_generator.h"
#include "aligned_read.h"

class ReferenceGenome;
class GenomicRegion;
class Variant;

class AlignmentCandidateVariantGenerator : public ICandidateVariantGenerator
{
public:
    using QualityType = AlignedRead::QualityType;
    
    AlignmentCandidateVariantGenerator() = delete;
    explicit AlignmentCandidateVariantGenerator(ReferenceGenome& the_reference,
                                                QualityType min_base_quality=0);
    ~AlignmentCandidateVariantGenerator() override = default;
    
    AlignmentCandidateVariantGenerator(const AlignmentCandidateVariantGenerator&)            = default;
    AlignmentCandidateVariantGenerator& operator=(const AlignmentCandidateVariantGenerator&) = default;
    AlignmentCandidateVariantGenerator(AlignmentCandidateVariantGenerator&&)                 = default;
    AlignmentCandidateVariantGenerator& operator=(AlignmentCandidateVariantGenerator&&)      = default;
    
    void add_read(const AlignedRead& a_read) override;
    void add_reads(ReadIterator first, ReadIterator last) override;
    std::vector<Variant> get_candidates(const GenomicRegion& a_region) override;
    void reserve(std::size_t n) override;
    void clear() override;
    
private:
    using SequenceType      = AlignedRead::SequenceType;
    using SequenceIterator  = SequenceType::const_iterator;
    using QualitiesIterator = AlignedRead::Qualities::const_iterator;
    
    ReferenceGenome& the_reference_;
    std::vector<Variant> candidates_;
    QualityType min_base_quality_;
    bool are_candidates_sorted_;
    
    bool is_good_sequence(const SequenceType& sequence) const noexcept;
    template <typename T1, typename T2, typename T3>
    void add_variant(T1&& the_region, T2&& sequence_removed, T3&& sequence_added);
    void get_snvs_in_match_range(const GenomicRegion& the_region, SequenceIterator first_base,
                                 SequenceIterator last_base, QualitiesIterator first_quality);
    std::size_t estimate_num_variants(std::size_t num_reads) const noexcept;
};

template <typename T1, typename T2, typename T3>
void AlignmentCandidateVariantGenerator::add_variant(T1&& the_region, T2&& sequence_removed,
                                                     T3&& sequence_added)
{
    candidates_.emplace_back(std::forward<T1>(the_region), std::forward<T2>(sequence_removed),
                             std::forward<T3>(sequence_added));
    are_candidates_sorted_ = false;
}

#endif /* defined(__Octopus__alignment_candidate_variant_generator__) */
