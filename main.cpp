// 引入 STB 库用于写入图像
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <string>
#include <cstdint>
#include <print>
#include <algorithm>
#include <random>

#include "mo_yanxi/allocator_2d.hpp"

//----------------------------------------------------------------------------------------------------
//  -TEST USAGE-    -TEST USAGE-    -TEST USAGE-    -TEST USAGE-    -TEST USAGE-    -TEST USAGE-
//----------------------------------------------------------------------------------------------------

struct BlockInfo {
    mo_yanxi::math::vector2<std::uint32_t> pos;
    mo_yanxi::math::vector2<std::uint32_t> size;
    std::uint8_t r, g, b;
};

class Canvas {
    int width, height;
    std::vector<std::uint8_t> pixels;

public:
    Canvas(int w, int h) : width(w), height(h), pixels(w * h * 3, 0) {}

    void draw_rect(int x, int y, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (int j = y; j < y + h; ++j) {
            for (int i = x; i < x + w; ++i) {
                if (i >= 0 && i < width && j >= 0 && j < height) {
                    int idx = (j * width + i) * 3;
                    pixels[idx] = r;
                    pixels[idx + 1] = g;
                    pixels[idx + 2] = b;
                }
            }
        }
    }

    void save(const std::string& filename) {
        if (stbi_write_png(filename.c_str(), width, height, 3, pixels.data(), width * 3)) {
            std::println("  -> 已保存快照: {}", filename);
        } else {
            std::println(stderr, "  -> 错误: 无法保存快照 {}", filename);
        }
    }

    void clear() {
        std::ranges::fill(pixels, 0);
    }
};

class AllocatorTester {
public:
    struct Config {
        std::string test_name;
        std::uint32_t map_size;
        int max_fill_attempts; // 最大尝试填充次数
        std::pair<std::uint32_t, std::uint32_t> size_range; // {min, max}
    };

private:
    Config config_;
    mo_yanxi::allocator2d<> alloc_;
    Canvas canvas_;
    std::vector<BlockInfo> active_blocks_;
    std::mt19937 rng_;
    std::uniform_int_distribution<std::uint32_t> size_dist_;
    std::uniform_int_distribution<int> color_dist_;

public:
    AllocatorTester(Config config)
        : config_(std::move(config)),
          alloc_(mo_yanxi::math::vector2<std::uint32_t>{config_.map_size, config_.map_size}),
          canvas_(config_.map_size, config_.map_size),
          rng_(std::random_device{}()),
          size_dist_(config_.size_range.first, config_.size_range.second),
          color_dist_(50, 255)
    {
        std::println("========================================");
        std::println("测试套件初始化: {}", config_.test_name);
        std::println("画布尺寸: {0}x{0}, 块大小范围: [{1}, {2}]",
            config_.map_size, config_.size_range.first, config_.size_range.second);
        std::println("========================================");
    }

    void run() {
        phase_1_fill();
        phase_2_fragment();
        phase_3_refill();
        phase_4_partial_clear();
        phase_5_full_clear_and_verify();
        std::println("\n测试 [{}] 完成。\n", config_.test_name);
    }

private:
    std::string get_filename(const std::string& suffix) const {
        return std::format("{}_{}", config_.test_name, suffix);
    }

    void phase_1_fill() {
        std::println("[阶段 1] 随机分配填充...");
        int count = 0;
        for (int i = 0; i < config_.max_fill_attempts; ++i) {
            std::uint32_t w = size_dist_(rng_);
            std::uint32_t h = size_dist_(rng_);
            auto size = mo_yanxi::math::vector2<std::uint32_t>{w, h};

            auto result = alloc_.allocate(size);
            if (result) {
                std::uint8_t r = color_dist_(rng_);
                std::uint8_t g = color_dist_(rng_);
                std::uint8_t b = color_dist_(rng_);

                active_blocks_.push_back({*result, size, r, g, b});
                canvas_.draw_rect(result->x, result->y, w, h, r, g, b);
                count++;
            } else {
                // 如果连续多次分配失败，可以提前退出，或者仅依赖 max_fill_attempts
                // 这里为了填满尽量多，我们继续尝试
            }
        }
        std::println("  -> 共分配了 {} 个块", count);
        canvas_.save(get_filename("01_allocated.png"));
    }

    void phase_2_fragment() {
        std::println("[阶段 2] 随机释放 50% 的块以制造碎片...");
        std::vector<BlockInfo> remaining_blocks;
        std::ranges::shuffle(active_blocks_, rng_);

        std::size_t remove_count = active_blocks_.size() / 2;
        for (std::size_t i = 0; i < active_blocks_.size(); ++i) {
            if (i < remove_count) {
                bool success = alloc_.deallocate(active_blocks_[i].pos);
                if (!success) {
                    std::println(stderr, "  -> 错误: 释放失败于 {},{}", active_blocks_[i].pos.x, active_blocks_[i].pos.y);
                } else {
                    auto& b = active_blocks_[i];
                    canvas_.draw_rect(b.pos.x, b.pos.y, b.size.x, b.size.y, 0, 0, 0);
                }
            } else {
                remaining_blocks.push_back(active_blocks_[i]);
            }
        }
        active_blocks_ = std::move(remaining_blocks);
        canvas_.save(get_filename("02_fragmented.png"));
    }

    void phase_3_refill() {
        std::println("[阶段 3] 尝试使用较小的块重新填满空隙...");
        // 使用更小的尺寸分布来测试缝隙填充能力
        std::uniform_int_distribution<std::uint32_t> small_size_dist(5, config_.size_range.first + 5);

        int success_count = 0;
        int attempts = config_.max_fill_attempts / 2; // 尝试次数

        for (int i = 0; i < attempts; ++i) {
            std::uint32_t w = small_size_dist(rng_);
            std::uint32_t h = small_size_dist(rng_);
            auto size = mo_yanxi::math::vector2<std::uint32_t>{w, h};

            auto result = alloc_.allocate(size);
            if (result) {
                // 亮白色表示新分配
                std::uint8_t r = 255, g = 255, b = 255;
                canvas_.draw_rect(result->x, result->y, w, h, r, g, b);
                active_blocks_.push_back({*result, size, r, g, b});
                success_count++;
            }
        }
        std::println("  -> 成功重新分配了 {} 个新块", success_count);
        canvas_.save(get_filename("03_refilled.png"));
    }

    void phase_4_partial_clear() {
        std::println("[阶段 4] 再次随机释放 30% 的块...");
        std::ranges::shuffle(active_blocks_, rng_);
        std::vector<BlockInfo> remaining_blocks;

        std::size_t remove_count = static_cast<std::size_t>(active_blocks_.size() * 0.3);
        for (std::size_t i = 0; i < active_blocks_.size(); ++i) {
            if (i < remove_count) {
                alloc_.deallocate(active_blocks_[i].pos);
                auto& b = active_blocks_[i];
                canvas_.draw_rect(b.pos.x, b.pos.y, b.size.x, b.size.y, 0, 0, 0);
            } else {
                remaining_blocks.push_back(active_blocks_[i]);
            }
        }
        active_blocks_ = std::move(remaining_blocks);
        canvas_.save(get_filename("04_partial_clear.png"));
    }

    void phase_5_full_clear_and_verify() {
        std::println("[阶段 5] 执行完全清除与最终验证...");

        for(const auto& b : active_blocks_) {
            if(!alloc_.deallocate(b.pos)) {
                 std::println(stderr, "  -> 致命错误: 无法释放位于 {},{} 的块", b.pos.x, b.pos.y);
            }
            canvas_.draw_rect(b.pos.x, b.pos.y, b.size.x, b.size.y, 0, 0, 0);
        }
        active_blocks_.clear();

        // 验证状态
        std::uint32_t total_area = config_.map_size * config_.map_size;
        std::println("  -> 最终状态检查:");
        std::println("  -> 期望剩余面积: {}", total_area);
        std::println("  -> 实际剩余面积: {}", alloc_.remain_area());

        if (alloc_.remain_area() == total_area) {
            std::println("  -> [通过] 内存已完全回收，无泄漏。");

            // 完美合并验证
            auto huge_block = alloc_.allocate({config_.map_size, config_.map_size});
            if (huge_block) {
                std::println("  -> [通过] 完美合并验证：成功分配全图尺寸块。");
                canvas_.draw_rect(0, 0, config_.map_size, config_.map_size, 0, 255, 0); // 绿色
            } else {
                std::println("  -> [失败] 完美合并验证：无法分配全图块（存在碎片合并问题）。");
                canvas_.draw_rect(0, 0, config_.map_size, config_.map_size, 255, 0, 0); // 红色
            }
        } else {
            std::println("  -> [失败] 内存泄漏检测：面积不匹配，差值 {}", total_area - alloc_.remain_area());
        }

        canvas_.save(get_filename("05_full_clear.png"));
    }
};

int main() {
    {
        AllocatorTester::Config config{
            .test_name = "Standard",
            .map_size = 2048,
            .max_fill_attempts = 10000,
            .size_range = {32, 256}
        };
        AllocatorTester tester(config);
        tester.run();
    }

    {
        AllocatorTester::Config config{
            .test_name = "HighFragment",
            .map_size = 1024,
            .max_fill_attempts = 5000,
            .size_range = {4, 16}
        };
        AllocatorTester tester(config);
        tester.run();
    }

    mo_yanxi::allocator2d_checked a{{256, 256}};
    const unsigned width = 32;
    const unsigned height = 64;
    if(const std::optional<mo_yanxi::math::usize2> where = a.allocate({width, height})){
        //Do something here...

        a.deallocate(where.value());
    }

}