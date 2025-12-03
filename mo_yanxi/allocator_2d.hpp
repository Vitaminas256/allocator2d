
#ifdef MO_YANXI_ALLOCATOR_2D_ENABLE_MODULE
#define MO_YANXI_ALLOCATOR_2D_EXPORT export
#else
#pragma once
#define MO_YANXI_ALLOCATOR_2D_EXPORT
#endif

#ifdef MO_YANXI_ALLOCATOR_2D_USE_STD_MODULE
import std;
#else
#include <utility>
#include <map>
#include <unordered_map>
#include <memory>
#include <scoped_allocator>
#include <optional>
#include <iostream>
#endif


#ifdef MO_YANXI_ALLOCATOR_2D_HAS_MATH_VECTOR2
import mo_yanxi.math.vector2;
#else


namespace mo_yanxi::math{
	MO_YANXI_ALLOCATOR_2D_EXPORT
	template <typename T>
	struct vector2{
		T x;
		T y;

		constexpr vector2 operator+(const vector2& other) const noexcept{
			return {x + other.x, y + other.y};
		}

		constexpr vector2 operator-(const vector2& other) const noexcept{
			return {x - other.x, y - other.y};
		}

		constexpr bool operator==(const vector2& other) const noexcept = default;

		template <typename U>
		constexpr vector2<U> as() const noexcept{
			return vector2<U>{static_cast<U>(x), static_cast<U>(y)};
		}

		constexpr T area() const noexcept{
			return x * y;
		}

		constexpr bool beyond(const vector2& other) const noexcept{
			return x > other.x || y > other.y;
		}
	};

	using usize2 = vector2<std::uint32_t>;
}

template <typename T>
struct std::hash<mo_yanxi::math::vector2<T>>{
	std::size_t operator()(const mo_yanxi::math::vector2<T>& v) const noexcept{
		std::size_t seed = 0;
		auto hash_combine = [&seed](std::size_t v){
			seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		};
		hash_combine(std::hash<T>{}(v.x));
		hash_combine(std::hash<T>{}(v.y));
		return seed;
	}
};

#endif


namespace{

	template <typename T>
	struct exchange_on_move{
		T value;

		[[nodiscard]] exchange_on_move() = default;

		[[nodiscard]] exchange_on_move(const T& value)
			: value(value){
		}

		exchange_on_move(const exchange_on_move& other) noexcept = default;

		exchange_on_move(exchange_on_move&& other) noexcept
			: value{std::exchange(other.value, {})}{
		}

		exchange_on_move& operator=(const exchange_on_move& other) noexcept = default;

		exchange_on_move& operator=(const T& other) noexcept{
			value = other;
			return *this;
		}

		exchange_on_move& operator=(exchange_on_move&& other) noexcept{
			value = std::exchange(other.value, {});
			return *this;
		}
	};
}

namespace mo_yanxi{

	MO_YANXI_ALLOCATOR_2D_EXPORT
	template <typename Alloc = std::allocator<std::byte>>
	struct allocator2d{
	private:
		using T = std::uint32_t;

	public:
		using size_type = T;
		using large_size_type = std::uint64_t;
		using extent_type = math::vector2<T>;
		using point_type = math::vector2<T>;
		using allocator_type = Alloc;

	private:
		exchange_on_move<extent_type> extent_{};
		exchange_on_move<large_size_type> remain_area_{};
		exchange_on_move<large_size_type> fragment_threshold_{};

		struct split_point;

		using map_type = std::unordered_map<
			point_type, split_point,
			std::hash<point_type>, std::equal_to<point_type>,
			typename std::allocator_traits<allocator_type>::template rebind_alloc<std::pair<
				const point_type, split_point>>
		>;
		map_type map{};

		using inner_tree_type = std::multimap<
			size_type, point_type,
			std::less<size_type>,
			typename std::allocator_traits<allocator_type>::template rebind_alloc<std::pair<
				const size_type, point_type>>
		>;

		using tree_type = std::map<
			size_type, inner_tree_type,
			std::less<size_type>,
			std::scoped_allocator_adaptor<typename std::allocator_traits<allocator_type>::template rebind_alloc<
				std::pair<const size_type, inner_tree_type>>>
		>;

		// Large pool: store area >= 1/8
		tree_type large_nodes_XY{};
		tree_type large_nodes_YX{};

		// Fragment pool: store area < 1/8
		tree_type frag_nodes_XY{};
		tree_type frag_nodes_YX{};

		using ItrOuter = typename tree_type::iterator;
		using ItrInner = typename tree_type::mapped_type::iterator;

		struct ItrPair{
			ItrOuter outer{};
			ItrInner inner{};
			[[nodiscard]] auto value() const{
				return inner->second;
			}

			void locateNextInner(const size_type second){
				inner = outer->second.lower_bound(second);
			}

			bool locateNextOuter(tree_type& tree){
				++outer;
				if(outer == tree.end()) return false;
				return true;
			}

			[[nodiscard]] bool valid(const tree_type& tree) const noexcept{
				return outer != tree.end() && inner != outer->second.end();
			}
		};

		struct split_point{
			point_type parent{};
			point_type bot_lft{};
			point_type top_rit{};
			point_type split{top_rit};

			bool idle{true};
			bool idle_top_lft{true};
			bool idle_top_rit{true};
			bool idle_bot_rit{true};

			[[nodiscard]] split_point() = default;
			[[nodiscard]] split_point(
				const point_type parent,
				const point_type bot_lft,
				const point_type top_rit)
				: parent(parent), bot_lft(bot_lft), top_rit(top_rit){
			}

			[[nodiscard]] bool is_leaf() const noexcept{
				return split == top_rit;
			}

			[[nodiscard]] bool is_root() const noexcept{
				return parent == bot_lft;
			}

			[[nodiscard]] bool is_split_idle() const noexcept{
				return idle_top_lft && idle_top_rit && idle_bot_rit;
			}

			void mark_captured(map_type& map) noexcept{
				idle = false;
				if(is_root()) return;
				auto& p = get_parent(map);
				if(parent.x == bot_lft.x){
					p.idle_top_lft = false;
				} else if(parent.y == bot_lft.y){
					p.idle_bot_rit = false;
				} else{
					p.idle_top_rit = false;
				}
			}

			bool check_merge(allocator2d& alloc) noexcept{
				if(idle && is_split_idle()){
					// 1. Top-Left Region
					point_type tl_src{bot_lft.x, split.y};
					point_type tl_end{split.x, top_rit.y};
					if((tl_end - tl_src).area() > 0) alloc.erase_split(tl_src, tl_end);

					// 2. Top-Right Region
					point_type tr_src = split;
					point_type tr_end = top_rit;
					if((tr_end - tr_src).area() > 0) alloc.erase_split(tr_src, tr_end);

					// 3. Bottom-Right Region
					point_type br_src{split.x, bot_lft.y};
					point_type br_end{top_rit.x, split.y};
					if((br_end - br_src).area() > 0) alloc.erase_split(br_src, br_end);

					// Erase self
					alloc.erase_mark(bot_lft, split);

					split = top_rit;

					if(is_root()) return false;
					auto& p = get_parent(alloc.map);
					if(parent.x == bot_lft.x){
						p.idle_top_lft = true;
					} else if(parent.y == bot_lft.y){
						p.idle_bot_rit = true;
					} else{
						p.idle_top_rit = true;
					}
					return true;
				}
				return false;
			}

			split_point& get_parent(map_type& map) const noexcept{
				return map.at(parent);
			}

			void acquire_and_split(allocator2d& alloc, const math::usize2 extent){
				assert(idle);
				if(is_leaf()){
					// first split
					split = bot_lft + extent;
					alloc.erase_mark(bot_lft, top_rit);

					// 1. Bottom-Right Region
					point_type br_src{split.x, bot_lft.y};
					point_type br_end{top_rit.x, split.y};
					if((br_end - br_src).area() > 0){
						alloc.add_split(bot_lft, br_src, br_end);
					}

					// 2. Top-Right Region
					point_type tr_src = split;
					point_type tr_end = top_rit;
					if((tr_end - tr_src).area() > 0){
						alloc.add_split(bot_lft, tr_src, tr_end);
					}

					// 3. Top-Left Region
					point_type tl_src{bot_lft.x, split.y};
					point_type tl_end{split.x, top_rit.y};
					if((tl_end - tl_src).area() > 0){
						alloc.add_split(bot_lft, tl_src, tl_end);
					}

					mark_captured(alloc.map);
				} else{
					alloc.erase_mark(bot_lft, split);
				}
			}

			split_point* mark_idle(allocator2d& alloc){
				assert(!idle);
				idle = true;
				auto* p = this;
				while(p->check_merge(alloc)){
					auto* next = &p->get_parent(alloc.map);
					p = next;
				}

				if(p->is_leaf()) alloc.mark_size(p->bot_lft, p->top_rit);
				return p;
			}
		};

		std::optional<point_type> findNodeInTree(
			tree_type& treeXY,
			tree_type& treeYX,
			const extent_type size)
		{
			ItrPair itrPairXY{treeXY.lower_bound(size.x)};
			ItrPair itrPairYX{treeYX.lower_bound(size.y)};

			std::optional<point_type> node{};
			bool possibleX{itrPairXY.outer != treeXY.end()};
			bool possibleY{itrPairYX.outer != treeYX.end()};

			while(true){
				if(!possibleX && !possibleY) break;

				if(possibleX){
					itrPairXY.locateNextInner(size.y);
					if(itrPairXY.valid(treeXY)){
						node = itrPairXY.value();
						break;
					} else{
						possibleX = itrPairXY.locateNextOuter(treeXY);
					}
				}

				if(possibleY){
					itrPairYX.locateNextInner(size.x);
					if(itrPairYX.valid(treeYX)){
						node = itrPairYX.value();
						break;
					} else{
						possibleY = itrPairYX.locateNextOuter(treeYX);
					}
				}
			}
			return node;
		}

		std::optional<point_type> getValidNode(const extent_type size){
			assert(size.area() > 0);

			if(remain_area_.value < size.area()){
				return std::nullopt;
			}

			auto frag_node = findNodeInTree(frag_nodes_XY, frag_nodes_YX, size);
			if (frag_node) return frag_node;

			return findNodeInTree(large_nodes_XY, large_nodes_YX, size);
		}

		void mark_size(const point_type src, const point_type dst) noexcept{
			const auto size = dst - src;

			if (static_cast<std::uint64_t>(size.x) * size.y < fragment_threshold_.value) {
				frag_nodes_XY[size.x].insert({size.y, src});
				frag_nodes_YX[size.y].insert({size.x, src});
			} else {
				large_nodes_XY[size.x].insert({size.y, src});
				large_nodes_YX[size.y].insert({size.x, src});
			}
		}

		void add_split(const point_type parent, const point_type src, const point_type dst){
			map.insert_or_assign(src, split_point{parent, src, dst});
			mark_size(src, dst);
		}

		void erase_split(const point_type src, const point_type dst){
			map.erase(src);
			erase_mark(src, dst);
		}

		void erase_mark(const point_type src, const point_type dst){
			const auto size = dst - src;

			if (static_cast<std::uint64_t>(size.x) * size.y < fragment_threshold_.value) {
				erase(frag_nodes_XY, src, size.x, size.y);
				erase(frag_nodes_YX, src, size.y, size.x);
			} else {
				erase(large_nodes_XY, src, size.x, size.y);
				erase(large_nodes_YX, src, size.y, size.x);
			}
		}

		static void erase(tree_type& map, const point_type src, const size_type outerKey,
		                  const size_type innerKey) noexcept{
			auto& inner = map[outerKey];
			auto [begin, end] = inner.equal_range(innerKey);
			for(auto cur = begin; cur != end; ++cur){
				if(cur->second == src){
					inner.erase(cur);
					return;
				}
			}
		}

		void init_threshold(const extent_type extent) {
			fragment_threshold_ = extent.as<std::uint64_t>().area() / 8;
		}

	public:
		[[nodiscard]] allocator2d() = default;
		[[nodiscard]] explicit allocator2d(const allocator_type& allocator)
			: map(allocator),
			  large_nodes_XY(allocator), large_nodes_YX(allocator),
			  frag_nodes_XY(allocator), frag_nodes_YX(allocator){
		}

		[[nodiscard]] explicit allocator2d(const extent_type extent)
			: extent_(extent), remain_area_(extent.area()){
			init_threshold(extent);
			add_split({}, {}, extent);
		}

		[[nodiscard]] allocator2d(const allocator_type& allocator, const extent_type extent)
			: extent_(extent), remain_area_(extent.area()), map(allocator),
			  large_nodes_XY(allocator), large_nodes_YX(allocator),
			  frag_nodes_XY(allocator), frag_nodes_YX(allocator){
			init_threshold(extent);
			add_split({}, {}, extent);
		}

		[[nodiscard]] std::optional<point_type> allocate(const math::usize2 extent){
			if(extent.area() == 0){
				return std::nullopt;
			}

			if(extent.beyond(extent_.value)) return std::nullopt;
			if(remain_area_.value < extent.as<std::uint64_t>().area()) return std::nullopt;

			auto chamber_src = getValidNode(extent);
			if(!chamber_src) return std::nullopt;

			auto& chamber = map[chamber_src.value()];
			chamber.acquire_and_split(*this, extent);
			remain_area_.value -= extent.area();
			return chamber_src.value();
		}

		bool deallocate(const point_type value) noexcept{
			if(const auto itr = map.find(value); itr != map.end()){
				const auto extent = (itr->second.split - value).area();
				itr->second.mark_idle(*this);
				remain_area_.value += extent;
				return true;
			}

			return false;
		}

		[[nodiscard]] extent_type extent() const noexcept{ return extent_.value; }
		[[nodiscard]] large_size_type remain_area() const noexcept{ return remain_area_.value; }

		allocator2d(allocator2d&& other) noexcept = default;
		allocator2d& operator=(allocator2d&& other) noexcept = default;

	protected:
		allocator2d& operator=(const allocator2d& other) = default;
		allocator2d(const allocator2d& other) = default;

		[[nodiscard]] bool is_leak_() const noexcept{
			return this->remain_area() != this->extent().template as<large_size_type>().area();
		}

		void check_leak_() const noexcept{
			if(is_leak_()){
#ifdef MO_YANXI_ALLOCATOR_2D_LEAK_BEHAVIOR
				MO_YANXI_ALLOCATOR_2D_LEAK_BEHAVIOR(*this)
#else
				std::cerr << "Allocator2D Leaked!";
				std::terminate();
#endif
			}
		}
	};

	MO_YANXI_ALLOCATOR_2D_EXPORT
	template <typename Alloc = std::allocator<std::byte>>
	struct allocator2d_checked : allocator2d<Alloc>{

		[[nodiscard]] explicit allocator2d_checked(const allocator2d<Alloc>::allocator_type& allocator)
			: allocator2d<Alloc>(allocator){
		}

		[[nodiscard]] explicit allocator2d_checked(const allocator2d<Alloc>::extent_type& extent)
			: allocator2d<Alloc>(extent){
		}

		[[nodiscard]] allocator2d_checked(const allocator2d<Alloc>::allocator_type& allocator,
			const allocator2d<Alloc>::extent_type& extent)
			: allocator2d<Alloc>(allocator, extent){
		}

		[[nodiscard]] allocator2d_checked() = default;

		~allocator2d_checked(){
			this->check_leak_();
		}

		allocator2d_checked(allocator2d_checked&& other) noexcept
			: allocator2d<Alloc>{std::move(other)}{
		}

		allocator2d_checked& operator=(allocator2d_checked&& other) noexcept{
			if(this == &other) return *this;
			this->check_leak_();
			allocator2d<Alloc>::operator =(std::move(other));
			return *this;
		}

	protected:
		allocator2d_checked& operator=(const allocator2d_checked& other) = default;
		allocator2d_checked(const allocator2d_checked& other) = default;

	};
}
#undef MO_YANXI_ALLOCATOR_2D_EXPORT