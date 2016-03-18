//
//  haplotype_liklihood_model.cpp
//  Octopus
//
//  Created by Daniel Cooke on 25/09/2015.
//  Copyright © 2015 Oxford University. All rights reserved.
//

#include "haplotype_liklihood_model.hpp"

#include <utility>
#include <cmath>
#include <limits>
#include <cassert>

#include "mappable.hpp"
#include "aligned_read.hpp"

#include <iostream> // TEST

namespace Octopus
{
    namespace
    {
        std::size_t num_out_of_range_bases(const std::size_t mapping_position,
                                           const AlignedRead& read, const Haplotype& haplotype)
        {
            const auto alignment_size = sequence_size(read) + mapping_position + 15;
            
            if (alignment_size > sequence_size(haplotype)) {
                return alignment_size - sequence_size(haplotype);
            }
            
            return 0;
        }
        
        bool is_in_range(const std::size_t mapping_position,
                         const AlignedRead& read, const Haplotype& haplotype)
        {
            return num_out_of_range_bases(mapping_position, read, haplotype) == 0;
        }
    } // namespace
    
    template <typename InputIt, typename T>
    double log_probability(const AlignedRead& read, const Haplotype& haplotype,
                           InputIt first_mapping_position, InputIt last_mapping_position,
                           const T& gap_open_penalities, const PairHMM::Model& model)
    {
        assert(contains(haplotype, read));
        
        const auto original_mapping_position = begin_distance(read, haplotype);
        
//        // debug
//        if (region_begin(read) == 17907341
//            && haplotype.contains(ContigAllele(ContigRegion(17907349, 17907350), "C"))
//            && haplotype.contains(ContigAllele(ContigRegion(17907410, 17907411), "G"))
//            && !haplotype.contains(ContigAllele(ContigRegion(17907548, 17907548), "ATC"))) {
//            ::debug::print_variant_alleles(haplotype); std::cout << std::endl;
//        }
        
        auto max_log_probability = std::numeric_limits<double>::lowest();
        
        bool is_original_position_mapped {false};
        bool has_in_range_mapping_position {false};
        
        std::for_each(first_mapping_position, last_mapping_position,
                      [&] (const auto position) {
                          if (is_in_range(position, read, haplotype)) {
                              has_in_range_mapping_position = true;
                              
                              auto cur = PairHMM::align_around_offset(haplotype.get_sequence(), read.get_sequence(),
                                                                      read.get_qualities(), gap_open_penalities,
                                                                      position, model);
                              
                              if (cur > max_log_probability) {
                                  max_log_probability = cur;
                              }
                          }
                          
                          if (position == original_mapping_position) {
                              is_original_position_mapped = true;
                          }
                      });
        
        if (!is_original_position_mapped && is_in_range(original_mapping_position, read, haplotype)) {
            has_in_range_mapping_position = true;
            
            auto cur = PairHMM::align_around_offset(haplotype.get_sequence(), read.get_sequence(),
                                                    read.get_qualities(), gap_open_penalities,
                                                    original_mapping_position, model);
            
            if (cur > max_log_probability) {
                max_log_probability = cur;
            }
        }
        
        if (!has_in_range_mapping_position) {
            const auto min_shift = num_out_of_range_bases(original_mapping_position, read, haplotype);
            
            assert(original_mapping_position >= min_shift);
            
            auto final_mapping_position = original_mapping_position - min_shift;
            
            max_log_probability = PairHMM::align_around_offset(haplotype.get_sequence(), read.get_sequence(),
                                                               read.get_qualities(), gap_open_penalities,
                                                               final_mapping_position, model);
        }
        
        assert(max_log_probability > std::numeric_limits<double>::lowest());
        
        return max_log_probability;
    }
    
    // public methods
    
    int calculate_flank_padding(const HaplotypeLikelihoodModel::FlankState& flank_state)
    {
        if (region_size(flank_state.active_region) == flank_state.max_active_region_sequence_size_) {
            return 0;
        }
        return static_cast<int>(flank_state.max_active_region_sequence_size_)
                - region_size(flank_state.active_region);
    }
    
    HaplotypeLikelihoodModel::HaplotypeLikelihoodModel(const Haplotype& haplotype,
                                                       FlankState flank_state)
    :
    indel_error_model_ {},
    haplotype_ {haplotype},
    haplotype_gap_open_penalities_ {indel_error_model_.calculate_gap_open_penalties(haplotype)},
    haplotype_flank_state_ {flank_state},
    model_ {2, 3}
    {
        if (haplotype_flank_state_.has_lhs_flank_inactive_candidates) {
            model_.lhs_flank_size = begin_distance(haplotype_flank_state_.active_region,
                                                   contig_region(haplotype));
            
            model_.lhs_flank_size -= calculate_flank_padding(haplotype_flank_state_);
        }
        
        if (haplotype_flank_state_.has_rhs_flank_inactive_candidates) {
            model_.rhs_flank_size = end_distance(contig_region(haplotype),
                                                 haplotype_flank_state_.active_region);
            
            model_.rhs_flank_size -= calculate_flank_padding(haplotype_flank_state_);
        }
    }
    
    double HaplotypeLikelihoodModel::log_probability(const AlignedRead& read,
                                                     MapPositionItr first_mapping_position,
                                                     MapPositionItr last_mapping_position) const
    {
        return Octopus::log_probability(read, haplotype_,
                                        first_mapping_position, last_mapping_position,
                                        haplotype_gap_open_penalities_, model_);
    }
    
} // namespace Octopus
