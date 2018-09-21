/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Samuel Bourasseau wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#include <array>
#include <bitset>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>


using Integer_t = std::int64_t;
using Position_t = std::array<Integer_t, 3u>;

namespace std {
template <typename T, std::size_t Size>
std::ostream &operator<<(std::ostream &_ostream, std::array<T, Size> const &_array)
{
	if constexpr (Size == 0u) return _ostream;
	else
	{
		_ostream << _array[0];
		for (std::size_t index = 1u; index < Size; ++index)
			_ostream << " " << _array[index];
		return _ostream;
	}
}
} // namespace std

namespace quick_vdb {

template <std::size_t Log2Side>
class LeafNode
{
public:
	static constexpr std::size_t kLog2Side = Log2Side;
	LeafNode() = default;
	explicit LeafNode(bool _active){ if (_active) active_bits_.set(); }
public:
	void set(Position_t const &_p, bool const _v = true)
	{
		std::size_t const bit_index = BitIndex_(_p);
		active_bits_[bit_index] = _v;
	}
	bool get(Position_t const &_p)
	{
		std::size_t const bit_index = BitIndex_(_p);
		return active_bits_[bit_index];
	}
	bool all() const
	{
		return active_bits_.all();
	}
	bool none() const
	{
		return active_bits_.none();
	}
private:
	static std::size_t const BitIndex_(Position_t const &_p)
	{
		// z + y * Nz + x * Nz*Ny -> puts z in the least significant bits
		// x, y, and z being local coordinates ( [0, 2^kLog2Side[ )
		constexpr std::size_t kLocalMask = (1u << kLog2Side) - 1u;
		return
			(_p[2] & kLocalMask) |
			(_p[1] & kLocalMask) << kLog2Side |
			(_p[0] & kLocalMask) << kLog2Side*2u;
	}
private:
	std::bitset<1u << (kLog2Side * 3u)> active_bits_;
};

template <typename Child, std::size_t Log2Side>
class BranchNode
{
public:
	static constexpr std::size_t kLog2Side = Log2Side + Child::kLog2Side;
	BranchNode() = default;
	explicit BranchNode(bool _active){ if (_active) active_bits_.set(); }
public:
	void set(Position_t const &_p, bool const _v = true)
	{
		std::size_t const bit_index = BitIndex_(_p);
		if (!child_bits_[bit_index])
		{
			if (_v != active_bits_[bit_index])
			{
				children_[bit_index].reset(new Child(active_bits_[bit_index]));
				children_[bit_index]->set(_p, _v);
			}
		}
		else
		{
			children_[bit_index]->set(_p, _v);
			if (children_[bit_index]->all())
			{
				active_bits_.set(bit_index);
				children_[bit_index].reset(nullptr);
			}
			else if (children_[bit_index]->none())
			{
				active_bits_.reset(bit_index);
				children_[bit_index].reset(nullptr);
			}
		}
	}
	bool get(Position_t const &_p) const
	{
		std::size_t const bit_index = BitIndex_(_p);
		if (child_bits_[bit_index]) return children_[bit_index]->get(_p);
		else return active_bits_[bit_index];
	}
	bool all() const
	{
		return active_bits_.all();
	}
	bool none() const
	{
		return active_bits_.none();
	}
private:
	static constexpr std::size_t kInternalLog2Side = Log2Side;
	static std::size_t const BitIndex_(Position_t const &_p)
	{
		// z/Child::Nz + (y/Child::Ny) * Nz + (x/Child::Nx) * Nz*Ny
		// computes position in terms of children and packs the result the same way as a LeafNode would
		// x, y, and z are local coordinates as well ( [0, 2^kLog2Side[ )
		// Nx, Ny, and Nz are bounds in terms of children ( [0, 2^kInternalLog2Side[ )
		constexpr std::size_t kLocalMask = (1u << kLog2Side) - 1u;
		return
			((_p[2] & kLocalMask) >> Child::kLog2Side) |
			((_p[1] & kLocalMask) >> Child::kLog2Side) << kInternalLog2Side |
			((_p[0] & kLocalMask) >> Child::kLog2Side) << kInternalLog2Side*2u;
	}
private:
	static constexpr std::size_t kSize = kInternalLog2Side * 3u;
	std::array<std::unique_ptr<Child>, 1u << kSize> children_;
	std::bitset<1u << kSize> active_bits_;
	std::bitset<1u << kSize> child_bits_;
};

template <typename Child>
class RootNode
{
public:
	using RootKey_t = std::array<Integer_t, 3>;
	struct RootKeyHash {
		RootKeyHash(){}
		std::size_t operator()(RootKey_t const &_key) const
		{
			std::hash<Integer_t> integer_hash;
			std::size_t const hash = integer_hash(_key[0] ^ _key[1] ^ _key[2]);
			return hash;
		}
	};
	struct RootData {
		std::unique_ptr<Child> child_;
		bool active_;
	};
	using RootMap_t = std::unordered_map<RootKey_t, RootData, RootKeyHash>;
public:
	void set(Position_t const &_p, bool const _v = true)
	{
		RootKey_t const key = RootKey_(_p);
		RootMap_t::iterator nit = root_map_.find(key);
		if (nit == root_map_.end())
		{
			root_map_[key] = RootData{ std::unique_ptr<Child>{}, false };
			nit = root_map_.find(key);
		}

		RootData &data = nit->second;
		if (data.child_ == nullptr)
		{
			if (_v != data.active_)
			{
				data.child_.reset(new Child(data.active_));
				data.child_->set(_p, _v);
			}
		}
		else
		{
			data.child_->set(_p, _v);
			if (data.child_->all())
			{
				data.active_ = true;
				data.child_.reset(nullptr);
			}
			else if (data.child_->none())
			{
				data.active_ = false;
				data.child_.reset(nullptr);
			}
		}
	}
	void reset(Position_t const &_p) { set(_p, false); }
	bool get(Position_t const &_p) const
	{
		RootKey_t const key = RootKey_(_p);
		RootMap_t::const_iterator const nit = root_map_.find(key);
		if (nit != root_map_.end())
		{
			RootData const &data = nit->second;
			if (data.child_ != nullptr)
				return data.child_->get(_p);
			else
				return data.active_;
		}
		else
			return false;
	}
private:
	static RootKey_t RootKey_(Position_t const &_p)
	{
		constexpr std::size_t kChildLocalMask = (1u << Child::kLog2Side) - 1u;
		return {
			_p[0] & ~kChildLocalMask,
			_p[1] & ~kChildLocalMask,
			_p[2] & ~kChildLocalMask
		};
	}
private:
	RootMap_t root_map_;

// =============================================================================
// UNIT TESTS
// =============================================================================
public:
	struct UnitTests
	{
		using VDB_t = RootNode<Child>;

		template <typename T>
		static void Fill_FirstLevelChild(T &_vdb)
		{
			constexpr std::size_t kChildSide = 1 << Child::kLog2Side;
			for (Integer_t i = 0; i < kChildSide; ++i)
				for (Integer_t j = 0; j < kChildSide; ++j)
					for (Integer_t k = 0; k < kChildSide; ++k)
						_vdb.set({i, j, k});
		}

		static bool FirstLevelChildAlloc_SingleChild()
		{
			VDB_t vdb{};
			vdb.set({0, 0, 0});
			return vdb.root_map_.size() == 1u;
		}
		static bool FirstLevelChildAlloc_DifferentChild()
		{
			VDB_t vdb{};
			vdb.set({ 0, 0, 0 });
			vdb.set({ 0, 0, 1 << Child::kLog2Side });
			return vdb.root_map_.size() == 2u;
		}
		static bool FirstLevelChildAlloc_SameChild()
		{
			// This test is meaningless for 2^0 wide children, so it should always pass
			if constexpr (Child::kLog2Side == 0u) return true;
			else
			{
				VDB_t vdb{};
				vdb.set({ 0, 0, 0 });
				vdb.set({ 0, 0, 1 });
				return vdb.root_map_.size() == 1u;
			}
		}

		static bool FirstLevelChildExists_FullChild_True()
		{
			VDB_t vdb{};
			Fill_FirstLevelChild(vdb);
			return vdb.root_map_.find(RootKey_({0, 0, 0})) != vdb.root_map_.end();
		}
		static bool FirstLevelChildExists_FullChild_False()
		{
			VDB_t vdb{};
			vdb.reset({0, 0, 0});
			return vdb.root_map_.find(RootKey_({0, 0, 0})) != vdb.root_map_.end();
		}

		static bool FirstLevelChildFree_FullChild_True()
		{
			VDB_t vdb{};
			Fill_FirstLevelChild(vdb);
			return vdb.root_map_.find(RootKey_({0, 0, 0}))->second.child_ == nullptr;
		}
		static bool FirstLevelChildFree_FullChild_False()
		{
			VDB_t vdb{};
			vdb.reset({0, 0, 0});
			return vdb.root_map_.find(RootKey_({0, 0, 0}))->second.child_ == nullptr;
		}

		static bool FirstLevelChildGet_ExistingChild()
		{
			VDB_t vdb{};
			vdb.set({0, 0, 0});
			return vdb.get({0, 0, 0});
		}
		static bool FirstLevelChildGet_MissingChild()
		{
			VDB_t vdb{};
			return !vdb.get({0, 0, 0});
		}
		static bool FirstLevelChildGet_FullChild_True()
		{
			VDB_t vdb{};
			Fill_FirstLevelChild(vdb);
			return vdb.get({0, 0, 0});
		}
		static bool FirstLevelChildGet_FullChild_False()
		{
			VDB_t vdb{};
			vdb.reset({0, 0, 0});
			return !vdb.get({0, 0, 0});
		}

		static bool FirstLevelChildSet_FullChild_NeighbourTest()
		{
			if constexpr (Child::kLog2Side == 0u) return true;
			else
			{
				VDB_t vdb{};
				Fill_FirstLevelChild(vdb);
				vdb.reset({0, 0, 0});
				return vdb.get({0, 0, 1});
			}
		}
	};
};

} // namespace quick_vdb

static constexpr std::size_t kLeafSide = 3u;
static constexpr std::size_t kBranch1Side = 3u;
using OneLevelVDB_t = quick_vdb::RootNode<quick_vdb::LeafNode<kLeafSide>>;
using TwoLevelVDB_t = quick_vdb::RootNode<quick_vdb::BranchNode<quick_vdb::LeafNode<kLeafSide>, kBranch1Side>>;

#define LOG_UNIT_TEST(func)										\
	if (func())													\
		std::cout << #func << " test passed" << std::endl;		\
	else														\
		std::cout << #func << " test failed" << std::endl;

template <typename VDB>
void UnitTests()
{
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildAlloc_SingleChild);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildAlloc_DifferentChild);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildAlloc_SameChild);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildExists_FullChild_True);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildExists_FullChild_False);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildFree_FullChild_True);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildFree_FullChild_False);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildGet_ExistingChild);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildGet_MissingChild);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildGet_FullChild_True);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildGet_FullChild_False);
	LOG_UNIT_TEST(VDB::UnitTests::FirstLevelChildSet_FullChild_NeighbourTest);
}

int main()
{
	std::cout << "OneLevelVDB tests" << std::endl;
	UnitTests<OneLevelVDB_t>();
	std::cout << "TwoLevelVDB tests" << std::endl;
	UnitTests<TwoLevelVDB_t>();
	return 0;
}
