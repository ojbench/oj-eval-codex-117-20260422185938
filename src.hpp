
#ifndef SRC_HPP
#define SRC_HPP

#include <cstddef>

enum class ReplacementPolicy { kDEFAULT = 0, kFIFO, kLRU, kMRU, kLRU_K };

class PageNode {
public:
  PageNode() = default;
  explicit PageNode(std::size_t id) : page_id_(id) {}

  void SetId(std::size_t id) { page_id_ = id; }
  std::size_t Id() const { return page_id_; }

  void Record(std::size_t t) {
    if (history_size_ < k_capacity_) {
      for (std::size_t i = history_size_; i > 0; --i) history_[i] = history_[i - 1];
      history_[0] = t;
      ++history_size_;
    } else {
      for (std::size_t i = k_capacity_ - 1; i > 0; --i) history_[i] = history_[i - 1];
      history_[0] = t;
    }
    last_visit_ = t;
    if (first_visit_ == 0) first_visit_ = t;
  }

  static void SetHistoryCapacity(std::size_t k) {
    if (k == 0) k = 1;
    if (k > k_capacity_) k = k_capacity_;
    k_effective_ = k;
  }

  std::size_t LastVisit() const { return last_visit_; }
  std::size_t FirstVisit() const { return first_visit_; }
  std::size_t HistorySize() const { return history_size_; }

  std::size_t KthRecent(std::size_t kreq) const {
    if (kreq == 0) return 0;
    if (history_size_ < kreq) return 0; // treat as -inf
    return history_[kreq - 1];
  }

private:
  std::size_t page_id_{};
  static constexpr std::size_t k_capacity_ = 64; // fixed upper bound
  static inline std::size_t k_effective_ = 1;
  std::size_t history_[k_capacity_]{}; // most-recent first
  std::size_t history_size_{};
  std::size_t first_visit_{};
  std::size_t last_visit_{};
};

class ReplacementManager {
public:
  constexpr static std::size_t npos = static_cast<std::size_t>(-1);

  ReplacementManager() = delete;

  ReplacementManager(std::size_t max_size, std::size_t k, ReplacementPolicy default_policy) {
    max_size_ = max_size;
    size_ = 0;
    time_ = 0;
    default_policy_ = default_policy;
    k_ = (k == 0 ? 1 : k);
    PageNode::SetHistoryCapacity(k_);
    pages_ = (max_size_ ? new PageNode[max_size_] : nullptr);
    in_use_ = (max_size_ ? new bool[max_size_] : nullptr);
    arrival_ = (max_size_ ? new std::size_t[max_size_] : nullptr);
    for (std::size_t i = 0; i < max_size_; ++i) {
      in_use_[i] = false;
      arrival_[i] = 0;
    }
    arrival_seq_ = 0;
  }

  ~ReplacementManager() {
    delete[] pages_;
    delete[] in_use_;
    delete[] arrival_;
  }

  void SwitchDefaultPolicy(ReplacementPolicy default_policy) { default_policy_ = default_policy; }

  void Visit(std::size_t page_id, std::size_t &evict_id, ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) {
    if (policy == ReplacementPolicy::kDEFAULT) policy = default_policy_;
    // If already present, just record access
    std::size_t idx = FindPage(page_id);
    if (idx != npos) {
      pages_[idx].Record(++time_);
      evict_id = npos;
      return;
    }
    // Not present: insert or evict then insert
    if (size_ < max_size_) {
      std::size_t free_idx = FirstFreeIndex();
      in_use_[free_idx] = true;
      pages_[free_idx].SetId(page_id);
      pages_[free_idx].Record(++time_);
      arrival_[free_idx] = ++arrival_seq_;
      ++size_;
      evict_id = npos;
      return;
    }
    // Full: evict victim
    std::size_t victim_idx = SelectVictim(policy);
    evict_id = pages_[victim_idx].Id();
    // Overwrite slot with new page
    pages_[victim_idx] = PageNode(page_id);
    in_use_[victim_idx] = true;
    pages_[victim_idx].Record(++time_);
    arrival_[victim_idx] = ++arrival_seq_;
  }

  bool RemovePage(std::size_t page_id) {
    std::size_t idx = FindPage(page_id);
    if (idx == npos) return false;
    in_use_[idx] = false;
    pages_[idx] = PageNode();
    --size_;
    return true;
  }

  std::size_t TryEvict(ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) const {
    if (policy == ReplacementPolicy::kDEFAULT) policy = default_policy_;
    if (size_ < max_size_) return npos;
    std::size_t idx = SelectVictimConst(policy);
    return idx == npos ? npos : pages_[idx].Id();
  }

  bool Empty() const { return size_ == 0; }
  bool Full() const { return size_ == max_size_; }
  std::size_t Size() const { return size_; }

private:
  std::size_t FindPage(std::size_t id) const {
    for (std::size_t i = 0; i < max_size_; ++i) {
      if (in_use_[i] && pages_[i].Id() == id) return i;
    }
    return npos;
  }

  std::size_t FirstFreeIndex() const {
    for (std::size_t i = 0; i < max_size_; ++i) {
      if (!in_use_[i]) return i;
    }
    return npos;
  }

  std::size_t SelectVictim(ReplacementPolicy p) const { return SelectVictimConst(p); }

  std::size_t SelectVictimConst(ReplacementPolicy p) const {
    std::size_t chosen = npos;
    if (p == ReplacementPolicy::kFIFO) {
      std::size_t best_arr = static_cast<std::size_t>(-1);
      for (std::size_t i = 0; i < max_size_; ++i) {
        if (!in_use_[i]) continue;
        if (arrival_[i] < best_arr) { best_arr = arrival_[i]; chosen = i; }
      }
    } else if (p == ReplacementPolicy::kLRU) {
      std::size_t best_time = static_cast<std::size_t>(-1);
      bool init = false;
      for (std::size_t i = 0; i < max_size_; ++i) if (in_use_[i]) {
        std::size_t lv = pages_[i].LastVisit();
        if (!init || lv < best_time) { best_time = lv; chosen = i; init = true; }
      }
    } else if (p == ReplacementPolicy::kMRU) {
      std::size_t best_time = 0;
      bool init = false;
      for (std::size_t i = 0; i < max_size_; ++i) if (in_use_[i]) {
        std::size_t lv = pages_[i].LastVisit();
        if (!init || lv > best_time) { best_time = lv; chosen = i; init = true; }
      }
    } else {
      // LRU-K
      std::size_t best_k_val = static_cast<std::size_t>(-1);
      std::size_t best_first = static_cast<std::size_t>(-1);
      bool has_insufficient = false;
      bool init = false;
      for (std::size_t i = 0; i < max_size_; ++i) if (in_use_[i]) {
        std::size_t kth = pages_[i].KthRecent(k_);
        if (kth == 0) {
          if (!has_insufficient) { has_insufficient = true; best_first = pages_[i].FirstVisit(); chosen = i; }
          else { std::size_t fv = pages_[i].FirstVisit(); if (fv < best_first) { best_first = fv; chosen = i; } }
        } else if (!has_insufficient) {
          if (!init || kth < best_k_val) { best_k_val = kth; chosen = i; init = true; }
        }
      }
    }
    return chosen;
  }

  // state
  std::size_t max_size_{};
  std::size_t size_{};
  std::size_t time_{};
  std::size_t k_{};
  ReplacementPolicy default_policy_{};
  PageNode *pages_{};
  bool *in_use_{};
  std::size_t *arrival_{};
  std::size_t arrival_seq_{};
};

#endif
