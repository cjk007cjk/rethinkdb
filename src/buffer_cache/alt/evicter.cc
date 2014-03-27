#include "buffer_cache/alt/evicter.hpp"

#include "buffer_cache/alt/page.hpp"
#include "buffer_cache/alt/page_cache.hpp"
#include "buffer_cache/alt/cache_balancer.hpp"

namespace alt {

evicter_t::evicter_t()
    : initialized_(false),
      page_cache_(NULL),
      balancer_(NULL),
      bytes_loaded_counter_(0),
      access_count_counter_(0),
      access_time_counter_(INITIAL_ACCESS_TIME) { }

evicter_t::~evicter_t() {
    assert_thread();
    if (initialized_) {
        balancer_->remove_evicter(this);
    }
}

void evicter_t::initialize(page_cache_t *page_cache, cache_balancer_t *balancer) {
    guarantee(balancer != NULL);
    initialized_ = true;  // Can you really say this class is 'initialized_'?
    page_cache_ = page_cache;
    memory_limit_ = balancer->base_mem_per_store();
    balancer_ = balancer;
    balancer_->add_evicter(this);
}

void evicter_t::update_memory_limit(uint64_t new_memory_limit,
                                    uint64_t bytes_loaded_accounted_for,
                                    uint64_t access_count_accounted_for,
                                    bool read_ahead_ok) {
    assert_thread();
    guarantee(initialized_);

    if (!read_ahead_ok) {
        page_cache_->have_read_ahead_cb_destroyed();
    }

    bytes_loaded_counter_ -= bytes_loaded_accounted_for;
    access_count_counter_ -= access_count_accounted_for;
    memory_limit_ = new_memory_limit;
    evict_if_necessary();
}

uint64_t evicter_t::get_clamped_bytes_loaded() const {
    assert_thread();
    guarantee(initialized_);
    return std::max<int64_t>(bytes_loaded_counter_, 0);
}

uint64_t evicter_t::memory_limit() const {
    assert_thread();
    guarantee(initialized_);
    return memory_limit_;
}

uint64_t evicter_t::access_count() const {
    assert_thread();
    guarantee(initialized_);
    return access_count_counter_;
}

void evicter_t::notify_bytes_loading(int64_t in_memory_buf_change) {
    assert_thread();
    guarantee(initialized_);
    bytes_loaded_counter_ += in_memory_buf_change;
    access_count_counter_ += 1;
}

void evicter_t::add_deferred_loaded(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    evicted_.add(page, page->hypothetical_memory_usage());
}

void evicter_t::catch_up_deferred_load(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    rassert(unevictable_.has_page(page));
    notify_bytes_loading(page->hypothetical_memory_usage());
}

void evicter_t::add_not_yet_loaded(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    unevictable_.add(page, page->hypothetical_memory_usage());
    evict_if_necessary();
    notify_bytes_loading(page->hypothetical_memory_usage());
}

void evicter_t::reloading_page(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    notify_bytes_loading(page->hypothetical_memory_usage());
}

bool evicter_t::page_is_in_unevictable_bag(page_t *page) const {
    assert_thread();
    guarantee(initialized_);
    return unevictable_.has_page(page);
}

bool evicter_t::page_is_in_evicted_bag(page_t *page) const {
    assert_thread();
    guarantee(initialized_);
    return evicted_.has_page(page);
}

void evicter_t::add_to_evictable_unbacked(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    evictable_unbacked_.add(page, page->hypothetical_memory_usage());
    evict_if_necessary();
    notify_bytes_loading(page->hypothetical_memory_usage());
}

void evicter_t::add_to_evictable_disk_backed(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    evictable_disk_backed_.add(page, page->hypothetical_memory_usage());
    evict_if_necessary();
    notify_bytes_loading(page->hypothetical_memory_usage());
}

void evicter_t::move_unevictable_to_evictable(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    rassert(unevictable_.has_page(page));
    unevictable_.remove(page, page->hypothetical_memory_usage());
    eviction_bag_t *new_bag = correct_eviction_category(page);
    rassert(new_bag == &evictable_disk_backed_
            || new_bag == &evictable_unbacked_);
    new_bag->add(page, page->hypothetical_memory_usage());
    evict_if_necessary();
}

void evicter_t::change_to_correct_eviction_bag(eviction_bag_t *current_bag,
                                               page_t *page) {
    assert_thread();
    guarantee(initialized_);
    rassert(current_bag->has_page(page));
    current_bag->remove(page, page->hypothetical_memory_usage());
    eviction_bag_t *new_bag = correct_eviction_category(page);
    new_bag->add(page, page->hypothetical_memory_usage());
    evict_if_necessary();
}

eviction_bag_t *evicter_t::correct_eviction_category(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    if (page->is_loading() || page->has_waiters()) {
        return &unevictable_;
    } else if (page->is_not_loaded()) {
        return &evicted_;
    } else if (page->is_disk_backed()) {
        return &evictable_disk_backed_;
    } else {
        return &evictable_unbacked_;
    }
}

void evicter_t::remove_page(page_t *page) {
    assert_thread();
    guarantee(initialized_);
    eviction_bag_t *bag = correct_eviction_category(page);
    bag->remove(page, page->hypothetical_memory_usage());
    evict_if_necessary();
    notify_bytes_loading(-static_cast<int64_t>(page->hypothetical_memory_usage()));
}

uint64_t evicter_t::in_memory_size() const {
    assert_thread();
    guarantee(initialized_);
    return unevictable_.size()
        + evictable_disk_backed_.size()
        + evictable_unbacked_.size();
}

void evicter_t::evict_if_necessary() {
    assert_thread();
    guarantee(initialized_);
    // KSI: Implement eviction of unbacked evictables too.  When flushing, you
    // could use the page_t::eviction_index_ field to identify pages that are
    // currently in the process of being evicted, to avoid reflushing a page
    // currently being written for the purpose of eviction.

    page_t *page;
    while (in_memory_size() > memory_limit_
           && evictable_disk_backed_.remove_oldish(&page, access_time_counter_)) {
        evicted_.add(page, page->hypothetical_memory_usage());
        page->evict_self();
        page_cache_->consider_evicting_current_page(page->block_id());
    }
}

}  // namespace alt
