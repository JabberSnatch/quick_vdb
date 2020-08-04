/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Samuel Bourasseau wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#include <iostream>

#include <quick_vdb.hpp>

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
