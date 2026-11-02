#include <algorithm>
#include <deque>
#include <list>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "memoryforge/vm/replacement_policy.hpp"

namespace memoryforge {
namespace {

[[noreturn]] void no_victim(std::string_view policy) {
    throw std::logic_error(std::string(policy) + ": select_victim() with no resident page");
}

// First in, first out: evict the page that has been resident the longest.
class FifoPolicy final : public ReplacementPolicy {
public:
    std::string_view name() const noexcept override { return "fifo"; }
    void on_hit(Vpn) override {}  // hits never change FIFO order
    void on_load(Vpn vpn) override { queue_.push_back(vpn); }
    Vpn select_victim() override {
        if (queue_.empty()) no_victim(name());
        return queue_.front();
    }
    void on_evict(Vpn vpn) override {
        if (!queue_.empty() && queue_.front() == vpn) {
            queue_.pop_front();
        } else {
            queue_.erase(std::remove(queue_.begin(), queue_.end(), vpn), queue_.end());
        }
    }

private:
    std::deque<Vpn> queue_;
};

// Least recently used: most-recent page at the list front, victim at the back.
class LruPolicy final : public ReplacementPolicy {
public:
    std::string_view name() const noexcept override { return "lru"; }
    void on_hit(Vpn vpn) override {
        const auto it = index_.find(vpn);
        if (it != index_.end()) order_.splice(order_.begin(), order_, it->second);
    }
    void on_load(Vpn vpn) override {
        order_.push_front(vpn);
        index_[vpn] = order_.begin();
    }
    Vpn select_victim() override {
        if (order_.empty()) no_victim(name());
        return order_.back();
    }
    void on_evict(Vpn vpn) override {
        const auto it = index_.find(vpn);
        if (it == index_.end()) return;
        order_.erase(it->second);
        index_.erase(it);
    }
    std::optional<std::string> diagnose() const override {
        if (order_.size() != index_.size()) return "lru: list and index sizes differ";
        return std::nullopt;
    }

private:
    std::list<Vpn> order_;
    std::unordered_map<Vpn, std::list<Vpn>::iterator> index_;
};

// Clock (second chance): a hand sweeps the frames, clearing reference bits
// until it finds a page whose bit is already clear.
class ClockPolicy final : public ReplacementPolicy {
public:
    explicit ClockPolicy(std::uint64_t frames) : slots_(frames) {}
    std::string_view name() const noexcept override { return "clock"; }

    void on_hit(Vpn vpn) override {
        const auto it = slot_of_.find(vpn);
        if (it != slot_of_.end()) slots_[it->second].referenced = true;
    }
    void on_load(Vpn vpn) override {
        std::size_t slot = 0;
        if (pending_slot_) {
            slot = *pending_slot_;
            pending_slot_.reset();
        } else {
            slot = fill_next_++;  // initial fill: frames are used in order
        }
        slots_[slot] = Slot{vpn, true, true};
        slot_of_[vpn] = slot;
    }
    Vpn select_victim() override {
        if (slot_of_.empty()) no_victim(name());
        while (true) {
            Slot& s = slots_[hand_];
            const std::size_t current = hand_;
            hand_ = (hand_ + 1) % slots_.size();
            if (!s.occupied) continue;
            if (s.referenced) {
                s.referenced = false;
                continue;
            }
            pending_slot_ = current;
            return s.vpn;
        }
    }
    void on_evict(Vpn vpn) override {
        const auto it = slot_of_.find(vpn);
        if (it == slot_of_.end()) return;
        slots_[it->second].occupied = false;
        slot_of_.erase(it);
    }

private:
    struct Slot {
        Vpn vpn = 0;
        bool referenced = false;
        bool occupied = false;
    };
    std::vector<Slot> slots_;
    std::unordered_map<Vpn, std::size_t> slot_of_;
    std::size_t hand_ = 0;
    std::size_t fill_next_ = 0;
    std::optional<std::size_t> pending_slot_;
};

// Belady's optimal policy. It is an offline baseline: it reads the whole
// trace up front and evicts the resident page whose next use is farthest away.
class OptimalPolicy final : public ReplacementPolicy {
public:
    explicit OptimalPolicy(std::span<const Vpn> trace) : trace_(trace.begin(), trace.end()) {
        const std::uint64_t never = trace_.size();
        next_use_.assign(trace_.size(), never);
        std::unordered_map<Vpn, std::uint64_t> next_seen;
        for (std::size_t i = trace_.size(); i-- > 0;) {
            const auto it = next_seen.find(trace_[i]);
            next_use_[i] = (it == next_seen.end()) ? never : it->second;
            next_seen[trace_[i]] = i;
        }
    }
    std::string_view name() const noexcept override { return "opt"; }

    void begin_access(std::uint64_t step, Vpn vpn) override {
        step_ = step;
        if (step >= trace_.size() || trace_[step] != vpn) {
            healthy_ = false;  // driven with a trace other than the one it was built from
        }
    }
    void on_hit(Vpn vpn) override { resident_[vpn] = upcoming(); }
    void on_load(Vpn vpn) override { resident_[vpn] = upcoming(); }
    Vpn select_victim() override {
        if (resident_.empty()) no_victim(name());
        auto victim = resident_.begin();
        for (auto it = resident_.begin(); it != resident_.end(); ++it) {
            if (it->second > victim->second) victim = it;  // strict: ties keep the lowest VPN
        }
        return victim->first;
    }
    void on_evict(Vpn vpn) override { resident_.erase(vpn); }
    std::optional<std::string> diagnose() const override {
        if (!healthy_) return "opt: simulator was driven with a different trace than the one given";
        return std::nullopt;
    }

private:
    [[nodiscard]] std::uint64_t upcoming() const {
        return step_ < next_use_.size() ? next_use_[step_] : trace_.size();
    }
    std::vector<Vpn> trace_;
    std::vector<std::uint64_t> next_use_;
    std::map<Vpn, std::uint64_t> resident_;  // resident page -> index of its next use
    std::uint64_t step_ = 0;
    bool healthy_ = true;
};

}  // namespace

Result<std::unique_ptr<ReplacementPolicy>> make_policy(ReplacementPolicyKind kind,
                                                       std::uint64_t frames,
                                                       std::span<const Vpn> future_trace) {
    if (frames == 0) return make_error(ErrorCode::InvalidArgument, "frames must be non-zero");
    switch (kind) {
        case ReplacementPolicyKind::Fifo: return std::unique_ptr<ReplacementPolicy>(new FifoPolicy);
        case ReplacementPolicyKind::Lru: return std::unique_ptr<ReplacementPolicy>(new LruPolicy);
        case ReplacementPolicyKind::Clock:
            return std::unique_ptr<ReplacementPolicy>(new ClockPolicy(frames));
        case ReplacementPolicyKind::Optimal:
            if (future_trace.empty()) {
                return make_error(ErrorCode::InvalidArgument,
                                  "the optimal policy requires the future access trace");
            }
            return std::unique_ptr<ReplacementPolicy>(new OptimalPolicy(future_trace));
    }
    return make_error(ErrorCode::InvalidArgument, "unknown replacement policy");
}

}  // namespace memoryforge
