//
//  mappable_set.hpp
//  Octopus
//
//  Created by Daniel Cooke on 19/07/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef Octopus_mappable_set_hpp
#define Octopus_mappable_set_hpp

#include <memory>     // std::allocator
#include <functional> // std::less
#include <algorithm>  // std::max, std::minmax, std::swap, std::for_each
#include <iterator>   // std::begin, std::end, std::cbegin, std::cend, std::next, std::prev
#include <stdexcept>  // std::out_of_range
#include <boost/container/flat_set.hpp>

#include "comparable.hpp"
#include "genomic_region.hpp"
#include "mappable.hpp"
#include "mappable_ranges.hpp"
#include "mappable_algorithms.hpp"

#include <iostream> // TEST

/**
 MappableSet is a container designed to allow fast retrival of MappableType elements with minimal
 memory overhead.
 */
template <typename MappableType, typename Allocator = std::allocator<MappableType>>
class MappableSet : public Comparable<MappableSet<MappableType, Allocator>>
{
protected:
    using base_t = boost::container::flat_multiset<MappableType, std::less<MappableType>, Allocator>;
    
public:
    using allocator_type  = typename base_t::allocator_type;
    using value_type      = typename base_t::value_type;
    using reference       = typename base_t::reference ;
    using const_reference = typename base_t::const_reference;
    using difference_type = typename base_t::difference_type ;
    using size_type       = typename base_t::size_type ;
    
    using iterator               = typename base_t::iterator;
    using const_iterator         = typename base_t::const_iterator;
    using reverse_iterator       = typename base_t::reverse_iterator;
    using const_reverse_iterator = typename base_t::const_reverse_iterator;
    
    MappableSet() = default;
    template <typename InputIterator>
    MappableSet(InputIterator first, InputIterator second);
    ~MappableSet() = default;
    
    MappableSet(const MappableSet&)            = default;
    MappableSet& operator=(const MappableSet&) = default;
    MappableSet(MappableSet&&)                 = default;
    MappableSet& operator=(MappableSet&&)      = default;
    
    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    const_iterator cbegin() const noexcept;
    iterator end() noexcept;
    const_iterator end() const noexcept;
    const_iterator cend() const noexcept;
    reverse_iterator rbegin() noexcept;
    const_reverse_iterator rbegin() const noexcept;
    const_reverse_iterator crbegin() const noexcept;
    reverse_iterator rend() noexcept;
    const_reverse_iterator rend() const noexcept;
    const_reverse_iterator crend() const noexcept;
    
    reference at(size_type pos);
    const_reference at(size_type pos) const;
    reference operator[](size_type pos);
    const_reference operator[](size_type pos) const;
    reference front();
    const_reference front() const;
    reference back();
    const_reference back() const;
    
    template <typename ...Args>
    iterator emplace(Args...);
    iterator insert(const MappableType&);
    iterator insert(MappableType&&);
    template <typename InputIterator>
    void insert(InputIterator, InputIterator);
    iterator insert(std::initializer_list<MappableType>);
    iterator erase(const_iterator);
    iterator erase(const MappableType&);
    iterator erase(const_iterator, const_iterator);
    //void erase(OverlapRange<const_iterator>);
    //void erase(ContainedRange<const_iterator>);
    void clear();
    
    void swap(const MappableSet&);
    size_type size() const noexcept;
    size_type capacity() const noexcept;
    size_type max_size() const noexcept;
    bool empty() const noexcept;
    void reserve(size_type n);
    void shrink_to_fit();
    
    allocator_type get_allocator() noexcept;
    
    const MappableType& leftmost() const;
    const MappableType& rightmost() const;
    
    template <typename MappableType_>
    bool has_overlapped(const MappableType_& mappable) const;
    template <typename MappableType_>
    bool has_overlapped(iterator first, iterator last, const MappableType_& mappable) const;
    template <typename MappableType_>
    bool has_overlapped(const_iterator first, const_iterator last, const MappableType_& mappable) const;
    
    template <typename MappableType_>
    size_type count_overlapped(const MappableType_& mappable) const;
    template <typename MappableType_>
    size_type count_overlapped(iterator first, iterator last, const MappableType_& mappable) const;
    template <typename MappableType_>
    size_type count_overlapped(const_iterator first, const_iterator last, const MappableType_& mappable) const;
    
    template <typename MappableType_>
    OverlapRange<const_iterator> overlap_range(const MappableType_& mappable) const;
    template <typename MappableType_>
    OverlapRange<iterator> overlap_range(iterator first, iterator last, const MappableType_& mappable) const;
    template <typename MappableType_>
    OverlapRange<const_iterator> overlap_range(const_iterator first, const_iterator last, const MappableType_& mappable) const;
    
    template <typename MappableType_>
    bool has_contained(const MappableType_& mappable) const;
    template <typename MappableType_>
    bool has_contained(iterator first, iterator last, const MappableType_& mappable) const;
    template <typename MappableType_>
    bool has_contained(const_iterator first, const_iterator last, const MappableType_& mappable) const;
    
    template <typename MappableType_>
    size_type count_contained(const MappableType_& mappable) const;
    template <typename MappableType_>
    size_type count_contained(iterator first, iterator last, const MappableType_& mappable) const;
    template <typename MappableType_>
    size_type count_contained(const_iterator first, const_iterator last, const MappableType_& mappable) const;
    
    template <typename MappableType_>
    ContainedRange<const_iterator> contained_range(const MappableType_& mappable) const;
    template <typename MappableType_>
    ContainedRange<iterator> contained_range(iterator first, iterator last, const MappableType_& mappable) const;
    template <typename MappableType_>
    ContainedRange<const_iterator> contained_range(const_iterator first, const_iterator last, const MappableType_& mappable) const;
    
    template <typename MappableType1_, typename MappableType2_>
    bool has_shared(const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    template <typename MappableType1_, typename MappableType2_>
    bool has_shared(iterator first, iterator last, const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    template <typename MappableType1_, typename MappableType2_>
    bool has_shared(const_iterator first, const_iterator last, const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    
    template <typename MappableType1_, typename MappableType2_>
    size_type count_shared(const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    template <typename MappableType1_, typename MappableType2_>
    size_type count_shared(iterator first, iterator last, const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    template <typename MappableType1_, typename MappableType2_>
    size_type count_shared(const_iterator first, const_iterator last, const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    
    template <typename MappableType1_, typename MappableType2_>
    SharedRange<const_iterator> shared_range(const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    template <typename MappableType1_, typename MappableType2_>
    SharedRange<iterator> shared_range(iterator first, iterator last, const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    template <typename MappableType1_, typename MappableType2_>
    SharedRange<const_iterator> shared_range(const_iterator first, const_iterator last, const MappableType1_& mappable1, const MappableType2_& mappable2) const;
    
    template <typename M, typename A>
    friend bool operator==(const MappableSet<M, A>& lhs, const MappableSet<M, A>& rhs);
    template <typename M, typename A>
    friend bool operator<(const MappableSet<M, A>& lhs, const MappableSet<M, A>& rhs);
    template <typename M, typename A>
    friend void swap(MappableSet<M, A>& lhs, MappableSet<M, A>& rhs);
    
private:
    base_t elements_;
    bool is_bidirectionally_sorted_;
    GenomicRegion::SizeType max_element_size_;
};

template <typename MappableType, typename Allocator>
template <typename InputIterator>
MappableSet<MappableType, Allocator>::MappableSet(InputIterator first, InputIterator second)
:
elements_ {first, second},
is_bidirectionally_sorted_ {is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_))},
max_element_size_ {(elements_.empty()) ? 0 : ::size(*largest_element(std::cbegin(elements_), std::cend(elements_)))}
{}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::begin() noexcept
{
    return elements_.begin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_iterator
MappableSet<MappableType, Allocator>::begin() const noexcept
{
    return elements_.begin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_iterator
MappableSet<MappableType, Allocator>::cbegin() const noexcept
{
    return elements_.cbegin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::end() noexcept
{
    return elements_.end();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_iterator
MappableSet<MappableType, Allocator>::end() const noexcept
{
    return elements_.end();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_iterator
MappableSet<MappableType, Allocator>::cend() const noexcept
{
    return elements_.cend();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::reverse_iterator
MappableSet<MappableType, Allocator>::rbegin() noexcept
{
    return elements_.rbegin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reverse_iterator
MappableSet<MappableType, Allocator>::rbegin() const noexcept
{
    return elements_.rbegin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reverse_iterator
MappableSet<MappableType, Allocator>::crbegin() const noexcept
{
    return elements_.crbegin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::reverse_iterator
MappableSet<MappableType, Allocator>::rend() noexcept
{
    return elements_.rend();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reverse_iterator
MappableSet<MappableType, Allocator>::rend() const noexcept
{
    return elements_.rend();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reverse_iterator
MappableSet<MappableType, Allocator>::crend() const noexcept
{
    return elements_.crend();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::reference
MappableSet<MappableType, Allocator>::at(size_type pos)
{
    if (pos < size()) {
        return *std::next(begin(), pos);
    } else {
        throw std::out_of_range {"MappableSet"};
    }
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reference
MappableSet<MappableType, Allocator>::at(size_type pos) const
{
    if (pos < size()) {
        return *std::next(cbegin(), pos);
    } else {
        throw std::out_of_range {"MappableSet"};
    }
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::reference
MappableSet<MappableType, Allocator>::operator[](size_type pos)
{
    return *std::next(begin(), pos);
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reference
MappableSet<MappableType, Allocator>::operator[](size_type pos) const
{
    return *std::next(cbegin(), pos);
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::reference
MappableSet<MappableType, Allocator>::front()
{
    return *begin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reference
MappableSet<MappableType, Allocator>::front() const
{
    return *cbegin();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::reference
MappableSet<MappableType, Allocator>::back()
{
    return *std::prev(end());
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::const_reference
MappableSet<MappableType, Allocator>::back() const
{
    return *std::prev(cend());
}

template <typename MappableType, typename Allocator>
template <typename ...Args>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::emplace(Args... args)
{
    auto it = elements_.emplace(std::forward<Args>(args)...);
    if (is_bidirectionally_sorted_) {
        auto overlapped = overlap_range(*it);
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(overlapped.begin(), overlapped.end());
    }
    max_element_size_ = std::max(max_element_size_, ::size(*it));
    return it;
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::insert(const MappableType& m)
{
    auto it = elements_.insert(m);
    if (is_bidirectionally_sorted_) {
        auto overlapped = overlap_range(*it);
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(overlapped.begin(), overlapped.end());
    }
    max_element_size_ = std::max(max_element_size_, ::size(*it));
    return it;
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::insert(MappableType&& m)
{
    auto it = elements_.insert(std::move(m));
    if (is_bidirectionally_sorted_) {
        auto overlapped = overlap_range(*it);
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(overlapped.begin(), overlapped.end());
    }
    max_element_size_ = std::max(max_element_size_, ::size(*it));
    return it;
}

template <typename MappableType, typename Allocator>
template <typename InputIterator>
void
MappableSet<MappableType, Allocator>::insert(InputIterator first, InputIterator last)
{
    max_element_size_ = std::max(max_element_size_, ::size(*largest_element(first, last)));
    elements_.insert(first, last);
    if (is_bidirectionally_sorted_) {
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_));
    }
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::insert(std::initializer_list<MappableType> il)
{
    max_element_size_ = std::max(max_element_size_, ::size(*largest_element(il.begin(), il.end())));
    return elements_.insert(std::move(il));
    if (is_bidirectionally_sorted_) {
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_));
    }
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::erase(const_iterator p)
{
    if (max_element_size_ == ::size(*p)) {
        auto it = elements_.erase(p);
        max_element_size_ = ::size(*largest_element(std::cbegin(elements_), std::cend(elements_)));
        return it;
    }
    if (!is_bidirectionally_sorted_) {
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_));
    }
    return elements_.erase(p);
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::erase(const MappableType& m)
{
    if (max_element_size_ == ::size(m)) {
        auto it = elements_.erase(m);
        max_element_size_ = ::size(*largest_element(std::cbegin(elements_), std::cend(elements_)));
        return it;
    }
    if (!is_bidirectionally_sorted_) {
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_));
    }
    return elements_.erase(m);
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::iterator
MappableSet<MappableType, Allocator>::erase(const_iterator first, const_iterator last)
{
    if (max_element_size_ == ::size(*largest_element(first, last))) {
        auto it = elements_.erase(first, last);
        max_element_size_ = ::size(*largest_element(std::cbegin(elements_), std::cend(elements_)));
        return it;
    }
    if (!is_bidirectionally_sorted_) {
        is_bidirectionally_sorted_ = is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_));
    }
    return elements_.erase(first, last);
}

//template <typename MappableType, typename Allocator>
//void
//MappableSet<MappableType, Allocator>::erase(OverlapRange<const_iterator> overlapped)
//{
//    bool update_max_element_size {max_element_size_ == ::size(*largest_element(overlapped.begin(), overlapped.end()))};
//    
//    if (is_bidirectionally_sorted_) {
//        elements_.erase(overlapped.begin().base(), overlapped.end().base());
//    } else {
//        // we must be careful not to invalidate iterators
//        
//        
//        
//        is_bidirectionally_sorted_ = is_bidirectionally_sorted(std::cbegin(elements_), std::cend(elements_));
//    }
//    
//    if (update_max_element_size) {
//        max_element_size_ = ::size(*largest_element(std::cbegin(elements_), std::cend(elements_)));
//    }
//}

template <typename MappableType, typename Allocator>
void MappableSet<MappableType, Allocator>::clear()
{
    elements_.clear();
    is_bidirectionally_sorted_ = true;
    max_element_size_ = 0;
}

template <typename MappableType, typename Allocator>
void MappableSet<MappableType, Allocator>::swap(const MappableSet& m)
{
    std::swap(elements_, m.elements_);
    std::swap(is_bidirectionally_sorted_, m.is_bidirectionally_sorted_);
    std::swap(max_element_size_, m.max_element_size_);
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::size() const noexcept
{
    return elements_.size();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::capacity() const noexcept
{
    return elements_.capacity();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::max_size() const noexcept
{
    return elements_.max_size();
}

template <typename MappableType, typename Allocator>
bool MappableSet<MappableType, Allocator>::empty() const noexcept
{
    return elements_.empty();
}

template <typename MappableType, typename Allocator>
void
MappableSet<MappableType, Allocator>::reserve(size_type n)
{
    elements_.reserve(n);
}

template <typename MappableType, typename Allocator>
void
MappableSet<MappableType, Allocator>::shrink_to_fit()
{
    elements_.shrink_to_fit();
}

template <typename MappableType, typename Allocator>
typename MappableSet<MappableType, Allocator>::allocator_type
MappableSet<MappableType, Allocator>::get_allocator() noexcept
{
    return elements_.get_allocator();
}

template <typename MappableType, typename Allocator>
const MappableType& MappableSet<MappableType, Allocator>::leftmost() const
{
    return front();
}

template <typename MappableType, typename Allocator>
const MappableType& MappableSet<MappableType, Allocator>::rightmost() const
{
    const auto& last = *std::prev(elements_.cend());
    if (is_bidirectionally_sorted_) {
        return last;
    } else {
        auto overlapped = ::overlap_range(elements_.cbegin(), elements_.cend(), last, max_element_size_);
        return rightmost_mappable(overlapped.begin(), overlapped.end());
    }
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
bool
MappableSet<MappableType, Allocator>::has_overlapped(const MappableType_& mappable) const
{
    return (is_bidirectionally_sorted_) ?
    ::has_overlapped(std::begin(elements_), std::end(elements_), mappable, MappableRangeOrder::BidirectionallySorted)
    :
    ::has_overlapped(std::begin(elements_), std::end(elements_), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
bool
MappableSet<MappableType, Allocator>::has_overlapped(iterator first, iterator last, const MappableType_& mappable) const
{
    return (is_bidirectionally_sorted_) ?
    ::has_overlapped(first, last, mappable, MappableRangeOrder::BidirectionallySorted)
    :
    ::has_overlapped(first, last, mappable, max_element_size_);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
bool
MappableSet<MappableType, Allocator>::has_overlapped(const_iterator first, const_iterator last, const MappableType_& mappable) const
{
    return (is_bidirectionally_sorted_) ?
    ::has_overlapped(first, last, mappable, MappableRangeOrder::BidirectionallySorted)
    :
    ::has_overlapped(first, last, mappable, max_element_size_);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_overlapped(const MappableType_& mappable) const
{
    auto overlapped = overlap_range(mappable);
    return (is_bidirectionally_sorted_) ? ::size(overlapped, MappableRangeOrder::BidirectionallySorted) : ::size(overlapped);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_overlapped(iterator first, iterator last, const MappableType_& mappable) const
{
    auto overlapped = overlap_range(first, last, mappable);
    return (is_bidirectionally_sorted_) ? ::size(overlapped, MappableRangeOrder::BidirectionallySorted) : ::size(overlapped);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_overlapped(const_iterator first, const_iterator last, const MappableType_& mappable) const
{
    auto overlapped = overlap_range(first, last, mappable);
    return (is_bidirectionally_sorted_) ? ::size(overlapped, MappableRangeOrder::BidirectionallySorted) : ::size(overlapped);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
OverlapRange<typename MappableSet<MappableType, Allocator>::const_iterator>
MappableSet<MappableType, Allocator>::overlap_range(const MappableType_& mappable) const
{
    return overlap_range(std::cbegin(elements_), std::cend(elements_), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
OverlapRange<typename MappableSet<MappableType, Allocator>::iterator>
MappableSet<MappableType, Allocator>::overlap_range(iterator first, iterator last, const MappableType_& mappable) const
{
    return overlap_range(const_iterator(first), const_iterator(last), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
OverlapRange<typename MappableSet<MappableType, Allocator>::const_iterator>
MappableSet<MappableType, Allocator>::overlap_range(const_iterator first, const_iterator last, const MappableType_& mappable) const
{
    return (is_bidirectionally_sorted_) ?
        ::overlap_range(first, last, mappable, MappableRangeOrder::BidirectionallySorted)
    :
        ::overlap_range(first, last, mappable, max_element_size_);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
bool
MappableSet<MappableType, Allocator>::has_contained(const MappableType_& mappable) const
{
    return has_contained(std::cbegin(elements_), std::cend(elements_), mappable);
    
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
bool
MappableSet<MappableType, Allocator>::has_contained(iterator first, iterator last, const MappableType_& mappable) const
{
    return has_contained(const_iterator(first), const_iterator(last), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
bool
MappableSet<MappableType, Allocator>::has_contained(const_iterator first, const_iterator last, const MappableType_& mappable) const
{
    return ::has_contained(first, last, mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_contained(const MappableType_& mappable) const
{
    return count_contained(std::cbegin(elements_), std::cend(elements_), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_contained(iterator first, iterator last, const MappableType_& mappable) const
{
    return count_contained(const_iterator(first), const_iterator(last), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_contained(const_iterator first, const_iterator last, const MappableType_& mappable) const
{
    auto contained = contained_range(first, last, mappable);
    return (is_bidirectionally_sorted_) ? ::size(contained, MappableRangeOrder::BidirectionallySorted) : ::size(contained);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
ContainedRange<typename MappableSet<MappableType, Allocator>::const_iterator>
MappableSet<MappableType, Allocator>::contained_range(const MappableType_& mappable) const
{
    return contained_range(std::cbegin(elements_), std::cend(elements_), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
ContainedRange<typename MappableSet<MappableType, Allocator>::iterator>
MappableSet<MappableType, Allocator>::contained_range(iterator first, iterator last, const MappableType_& mappable) const
{
    return contained_range(const_iterator(first), const_iterator(last), mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType_>
ContainedRange<typename MappableSet<MappableType, Allocator>::const_iterator>
MappableSet<MappableType, Allocator>::contained_range(const_iterator first, const_iterator last, const MappableType_& mappable) const
{
    return ::contained_range(first, last, mappable);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
bool
MappableSet<MappableType, Allocator>::has_shared(const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    return has_shared(std::cbegin(elements_), std::cend(elements_), mappable1, mappable2);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
bool
MappableSet<MappableType, Allocator>::has_shared(iterator first, iterator last,
                                                 const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    return has_shared(const_iterator(first), const_iterator(last), mappable1, mappable2);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
bool
MappableSet<MappableType, Allocator>::has_shared(const_iterator first, const_iterator last,
                                                 const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    if (inner_distance(mappable1, mappable2) > max_element_size_) return false;
    
    auto m = std::minmax(get_region(mappable1), get_region(mappable2));
    
    auto overlapped_lhs = overlap_range(first, last, m.first);
    
    return std::any_of(overlapped_lhs.begin(), overlapped_lhs.end(),
                       [&m] (const auto& region) {
                           return overlaps(region, m.second);
                       });
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_shared(const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    return count_shared(std::cbegin(elements_), std::cend(elements_), mappable1, mappable2);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_shared(iterator first, iterator last,
                                                   const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    return count_shared(const_iterator(first), const_iterator(last), mappable1, mappable2);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
typename MappableSet<MappableType, Allocator>::size_type
MappableSet<MappableType, Allocator>::count_shared(const_iterator first, const_iterator last,
                                                   const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    if (inner_distance(mappable1, mappable2) > max_element_size_) return 0;
    
    auto m = std::minmax(get_region(mappable1), get_region(mappable2));
    
    auto overlapped_lhs = overlap_range(first, last, m.first);
    
    return std::count_if(overlapped_lhs.begin(), overlapped_lhs.end(),
                       [&m] (const auto& region) {
                           return overlaps(region, m.second);
                       });
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
SharedRange<typename MappableSet<MappableType, Allocator>::const_iterator>
MappableSet<MappableType, Allocator>::shared_range(const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    return shared_range(std::cbegin(elements_), std::cend(elements_), mappable1, mappable2);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
SharedRange<typename MappableSet<MappableType, Allocator>::iterator>
MappableSet<MappableType, Allocator>::shared_range(iterator first, iterator last,
                                                   const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    return shared_range(const_iterator(first), const_iterator(last), mappable1, mappable2);
}

template <typename MappableType, typename Allocator>
template <typename MappableType1_, typename MappableType2_>
SharedRange<typename MappableSet<MappableType, Allocator>::const_iterator>
MappableSet<MappableType, Allocator>::shared_range(const_iterator first, const_iterator last,
                                                   const MappableType1_& mappable1, const MappableType2_& mappable2) const
{
    if (inner_distance(mappable1, mappable2) > max_element_size_) {
        return make_shared_range(last, last, mappable1, mappable2);
    }
    
    auto m = std::minmax(get_region(mappable1), get_region(mappable2));
    
    auto overlapped_lhs = overlap_range(first, last, m.first);
    
    auto it = std::find_if(overlapped_lhs.begin(), overlapped_lhs.end(),
                           [&m] (const auto& region) {
                               return overlaps(region, m.second);
                           });
    
    auto end = std::prev(overlapped_lhs.end());
    
    while (end != it && !overlaps(*end, m.second)) --end;
    
    return make_shared_range(it.base(), std::next(end).base(), mappable1, mappable2);
}

// non-member methods

template <typename MappableType, typename Allocator>
bool operator==(const MappableSet<MappableType, Allocator>& lhs, const MappableSet<MappableType, Allocator>& rhs)
{
    return lhs.elements_ == rhs.elements_;
}

template <typename MappableType, typename Allocator>
bool operator<(const MappableSet<MappableType, Allocator>& lhs, const MappableSet<MappableType, Allocator>& rhs)
{
    return lhs.elements_ < rhs.elements_;
}

template <typename MappableType, typename Allocator>
void swap(MappableSet<MappableType, Allocator>& lhs, MappableSet<MappableType, Allocator>& rhs)
{
    std::swap(lhs.elements_, rhs.elements_);
    std::swap(lhs.is_bidirectionally_sorted_, rhs.is_bidirectionally_sorted_);
    std::swap(lhs.max_element_size_, rhs.max_element_size_);
}

template <typename ForwardIterator, typename MappableType1, typename MappableType2>
ForwardIterator
find_first_shared(const MappableSet<MappableType1>& mappables, ForwardIterator first, ForwardIterator last,
                  const MappableType2& mappable)
{
    return std::find_if(first, last,
                        [&mappables, &mappable] (const auto& m) {
                            return mappables.has_shared(m, mappable);
                        });
}

template <typename MappableType, typename ForwardIterator>
size_t count_if_shared_with_first(const MappableSet<MappableType>& mappables,
                                  ForwardIterator first, ForwardIterator last)
{
    if (first == last) return 0;
    
    auto overlapped = mappables.overlap_range(*first);
    
    if (empty(overlapped)) return 0;
    
    return count_overlapped(std::next(first), last, overlapped.back());
}

template <typename MappableType1, typename MappableType2>
MappableSet<MappableType1>
copy_overlapped(const MappableSet<MappableType1>& mappables, const MappableType2& mappable)
{
    auto overlapped = mappables.overlap_range(mappable);
    return MappableSet<MappableType1>(overlapped.begin(), overlapped.end());
}

template <typename MappableType1, typename MappableType2>
MappableSet<MappableType1>
copy_nonoverlapped(const MappableSet<MappableType1>& mappables, const MappableType2& mappable)
{
    auto num_overlapped = mappables.count_overlapped(mappable);
    
    if (num_overlapped == 0) return mappables;
    
    MappableSet<MappableType1> result {};
    result.reserve(mappables.size() - num_overlapped);
    
    auto overlapped = mappables.overlap_range(mappable);
    
    auto base_begin = overlapped.begin().base();
    auto base_end   = overlapped.end().base();
    
    result.insert(std::cbegin(mappables), base_begin);
    
    while (!overlapped.empty()) {
        overlapped.advance_begin(1);
        ++base_begin;
        
        if (overlapped.begin() != base_begin) {
            result.insert(base_begin, overlapped.begin().base());
            base_begin = overlapped.begin().base();
        }
    }
    
    result.insert(base_end, std::cend(mappables));
    
    return result;
}

template <typename MappableType1, typename MappableType2>
MappableSet<MappableType1>
copy_contained(const MappableSet<MappableType1>& mappables, const MappableType2& mappable)
{
    auto contained = mappables.contained_range(mappable);
    return MappableSet<MappableType1>(contained.begin(), contained.end());
}

template <typename MappableType1, typename MappableType2>
MappableSet<MappableType1>
copy_noncontained(const MappableSet<MappableType1>& mappables, const MappableType2& mappable)
{
    auto num_overlapped = mappables.count_overlapped(mappable);
    
    if (num_overlapped == 0) return mappables;
    
    MappableSet<MappableType1> result {};
    result.reserve(mappables.size() - num_overlapped);
    
    auto contained = mappables.contained_range(mappable);
    
    auto base_begin = contained.begin().base();
    auto base_end   = contained.end().base();
    
    result.insert(std::cbegin(mappables), base_begin);
    
    while (!contained.empty()) {
        contained.advance_begin(1);
        std::advance(base_begin);
        
        if (contained.begin() != base_begin) {
            result.insert(base_begin, contained.begin().base());
            base_begin = contained.begin().base();
        }
    }
    
    result.insert(base_end, std::cend(mappables));
    
    return result;
}

template <typename Region, typename Mappable>
MappableSet<Region> splice_all(const MappableSet<Region>& regions, const MappableSet<Mappable>& mappables)
{
    if (mappables.empty()) return regions;
    
    MappableSet<Region> result {};
    result.reserve(regions.size());
    
    for (const auto& region : regions) {
        auto overlapped = mappables.overlap_range(region);
        
        if (empty(overlapped)) {
            result.emplace(region);
        } else if (!is_same_region(region, overlapped.front())) {
            auto spliced = region;
            
            if (begins_before(overlapped.front(), spliced)) {
                spliced = get_right_overhang(spliced, overlapped.front());
                overlapped.advance_begin(1);
            }
            
            std::for_each(std::cbegin(overlapped), std::cend(overlapped),
                          [&result, &spliced] (const auto& region) {
                result.emplace(get_left_overhang(spliced, region));
                spliced = compress_lhs(spliced, get_begin(region) - get_begin(spliced));
            });
            
            if (ends_before(overlapped.back(), spliced)) {
                result.emplace(get_right_overhang(spliced, overlapped.back()));
            }
        }
   }
   
   result.shrink_to_fit();
   
   return result;
}

#endif