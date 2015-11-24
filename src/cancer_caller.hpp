//
//  cancer_caller.hpp
//  Octopus
//
//  Created by Daniel Cooke on 16/09/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__cancer_caller__
#define __Octopus__cancer_caller__

#include <string>

#include "variant_caller.hpp"
#include "haplotype_phaser.hpp"

class GenomicRegion;
class ReadManager;
class ReadTransform;
class Variant;
class VcfRecord;

namespace Octopus
{
    class CancerVariantCaller : public VariantCaller
    {
    public:
        CancerVariantCaller() = delete;
        explicit CancerVariantCaller(ReferenceGenome& reference, CandidateVariantGenerator& candidate_generator,
                                     RefCallType refcalls, double min_variant_posterior, double min_somatic_posterior,
                                     double min_refcall_posterior, const SampleIdType& normal_sample,
                                     bool call_somatics_only);
        ~CancerVariantCaller() = default;
        
        CancerVariantCaller(const CancerVariantCaller&)            = delete;
        CancerVariantCaller& operator=(const CancerVariantCaller&) = delete;
        CancerVariantCaller(CancerVariantCaller&&)                 = delete;
        CancerVariantCaller& operator=(CancerVariantCaller&&)      = delete;
        
    private:
        HaplotypePhaser phaser_;
        
        const SampleIdType normal_sample_;
        const double min_variant_posterior_          = 0.95;
        const double min_somatic_mutation_posterior_ = 0.9;
        const double min_refcall_posterior_          = 0.5;
        
        bool call_somatics_only_;
        
        std::string do_get_details() const override;
        
        std::vector<VcfRecord> call_variants(const GenomicRegion& region, const std::vector<Variant>& candidates,
                                             const ReadMap& reads) override;
    };
    
} // namespace Octopus

#endif /* defined(__Octopus__cancer_caller__) */