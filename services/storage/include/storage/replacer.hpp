#pragma once

#include "common/types.hpp"
#include "common/concurrency.hpp"
#include <list>
#include <unordered_map>
#include <vector>

namespace aether {

using frame_id_t = int32_t;

/**
 * Replacer is the abstract base class for tracking which page frames
 * are currently unpinned and eligible for eviction.
 */
class Replacer {
public:
    virtual ~Replacer() = default;

    // Pick a victim frame to evict. Returns true if successful and populates frame_id.
    virtual bool Victim(frame_id_t *frame_id) = 0;

    // Pin a frame, making it ineligible for eviction. Removes it from the replacer.
    virtual void Pin(frame_id_t frame_id) = 0;

    // Unpin a frame, making it eligible for eviction. Adds it to the replacer.
    virtual void Unpin(frame_id_t frame_id) = 0;

    // Return the number of frames currently in the replacer (eligible for eviction)
    virtual size_t Size() = 0;
};

/**
 * LRUReplacer implements the Least-Recently-Used eviction strategy.
 */
class LRUReplacer : public Replacer {
public:
    explicit LRUReplacer(size_t num_frames);
    ~LRUReplacer() override = default;

    bool Victim(frame_id_t *frame_id) override;
    void Pin(frame_id_t frame_id) override;
    void Unpin(frame_id_t frame_id) override;
    size_t Size() override;

private:
    size_t num_frames_;
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
    Mutex latch_;
};

/**
 * ClockReplacer implements the Clock second-chance eviction strategy.
 */
class ClockReplacer : public Replacer {
public:
    explicit ClockReplacer(size_t num_frames);
    ~ClockReplacer() override = default;

    bool Victim(frame_id_t *frame_id) override;
    void Pin(frame_id_t frame_id) override;
    void Unpin(frame_id_t frame_id) override;
    size_t Size() override;

private:
    size_t num_frames_;
    std::vector<bool> ref_flags_;
    std::vector<bool> in_replacer_;
    size_t clock_hand_{0};
    size_t size_{0};
    Mutex latch_;
};

} // namespace aether
