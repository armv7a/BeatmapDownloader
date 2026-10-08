#pragma once

#include <atomic>

namespace ui {
class InputBlock {
    inline static std::atomic<int> block = 0;

public:
    InputBlock() = delete;
    
    static void Push() {
        block.fetch_add(1);
    }

    static void Pop() {
        int cur = block.load();
        while (cur > 0 && !block.compare_exchange_weak(cur, cur - 1)) {
        }
    }

    static bool IsBlocked() {
        return block.load() > 0;
    }
};
}
