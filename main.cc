#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>

using Integer_t = std::int64_t;


namespace quick_vdb {

template <std::size_t Log2Side>
struct LeafNode
{
	static constexpr std::size_t kLog2Side = Log2Side;
};

template <typename Child, std::size_t Log2Side>
struct BranchNode
{
	static constexpr std::size_t kLog2Side = Log2Side + Child::kLog2Side;
};

template <typename Child>
struct RootNode
{
public:
	using RootKey = std::array<Integer_t, 3>;
	struct RootKeyHash {
		RootKeyHash(){}
		std::size_t operator()(RootKey const& _key) const
		{
			std::hash<Integer_t> integer_hash;
			std::size_t const hash = integer_hash(_key[0] ^ _key[1] ^ _key[2]);
			std::cout << hash << std::endl;
			return hash;
		}
	};
	struct RootData {
		std::unique_ptr<Child> child_;
		bool active_;
	};
public:
	void set(Integer_t _x, Integer_t _y, Integer_t _z, bool _v)
	{
		constexpr std::size_t kChildLocalMask = (1u << Child::kLog2Side) - 1u;
		RootKey key = {
			_x & ~kChildLocalMask,
			_y & ~kChildLocalMask,
			_z & ~kChildLocalMask
		};
		root_map_[key] = RootData{ std::unique_ptr<Child>{ nullptr }, _v };
	}
private:
	std::unordered_map<RootKey, RootData, RootKeyHash> root_map_;
};

} // namespace quick_vdb

static constexpr std::size_t kLeafSide = 3u;
using VDB_t = quick_vdb::RootNode<quick_vdb::LeafNode<kLeafSide>>;

int main()
{
	VDB_t vdb{};
	vdb.set(0, 0, 0, false);
	vdb.set(1, 1, 1, true);

	vdb.set(0, 0, 1 << kLeafSide, false);
	return 0;
}
