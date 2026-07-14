#include "storage/replacer.hpp"

namespace aether {

// ==========================================
// LRUReplacer
// ==========================================

LRUReplacer::LRUReplacer(size_t num_frames) : num_frames_(num_frames) {}

bool LRUReplacer::Victim(frame_id_t *frame_id) {
    LockGuard lock(latch_);
    if (lru_list_.empty()) {
        return false;
    }
    *frame_id = lru_list_.front();
    lru_map_.erase(*frame_id);
    lru_list_.pop_front();
    return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
    LockGuard lock(latch_);
    auto iter = lru_map_.find(frame_id);
    if (iter != lru_map_.end()) {
        lru_list_.erase(iter->second);
        lru_map_.erase(iter);
    }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
    LockGuard lock(latch_);
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= num_frames_) {
        return;
    }
    // If already in replacer, do not update its position in the LRU queue.
    // (A frame becomes an eviction candidate upon unpinning, and keeps its status)
    if (lru_map_.find(frame_id) != lru_map_.end()) {
        return;
    }
    lru_list_.push_back(frame_id);
    lru_map_[frame_id] = std::prev(lru_list_.end());
}

size_t LRUReplacer::Size() {
    LockGuard lock(latch_);
    return lru_list_.size();
}

// ==========================================
// ClockReplacer
// ==========================================

ClockReplacer::ClockReplacer(size_t num_frames)
    : num_frames_(num_frames), ref_flags_(num_frames, false), in_replacer_(num_frames, false) {}

bool ClockReplacer::Victim(frame_id_t *frame_id) {
    LockGuard lock(latch_);
    if (size_ == 0) {
        return false;
    }
    while (true) {
        if (in_replacer_[clock_hand_]) {
            if (ref_flags_[clock_hand_]) {
                ref_flags_[clock_hand_] = false;
            } else {
                // Found a victim
                *frame_id = static_cast<frame_id_t>(clock_hand_);
                in_replacer_[clock_hand_] = false;
                size_--;
                clock_hand_ = (clock_hand_ + 1) % num_frames_;
                return true;
            }
        }
        clock_hand_ = (clock_hand_ + 1) % num_frames_;
    }
}

void ClockReplacer::Pin(frame_id_t frame_id) {
    LockGuard lock(latch_);
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= num_frames_) {
        return;
    }
    if (in_replacer_[frame_id]) {
        in_replacer_[frame_id] = false;
        size_--;
    }
}

void ClockReplacer::Unpin(frame_id_t frame_id) {
    LockGuard lock(latch_);
    if (frame_id < 0 || static_cast<size_t>(frame_id) >= num_frames_) {
        return;
    }
    if (!in_replacer_[frame_id]) {
        in_replacer_[frame_id] = true;
        ref_flags_[frame_id] = true;
        size_++;
    }
}

size_t ClockReplacer::Size() {
    LockGuard lock(latch_);
    return size_;
}

} // namespace aether
