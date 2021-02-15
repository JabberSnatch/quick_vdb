/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Samuel Bourasseau wrote this file. You can do whatever you want with this
 * stuff. If we meet some day, and you think this stuff is worth it, you can
 * buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <unordered_map>


namespace quick_vdb {

using Integer_t = std::int64_t;
using Unsigned_t = std::uint64_t;
using Position_t = std::array<Integer_t, 3u>;
using Extent_t = std::array<Unsigned_t, 3u>;

struct Box_t
{
    Position_t base;
    Extent_t extent;
};

struct CacheEntry
{
    Position_t base;
    void* node;
};

template <std::size_t Log2Side>
class LeafNode
{
public:
    static constexpr unsigned kNodeLevel = 0u;
    using ChildT = void;

public:
    static constexpr std::size_t kLog2Side = Log2Side;
    LeafNode() = default;

    explicit LeafNode(bool _active, Position_t const& _base)
        : base_{ _base } {
        if (_active)
            active_bits_.set();
    }

public:
    void set(CacheEntry*, Position_t const &_p, bool const _v = true)
    {
        std::size_t const bit_index = BitIndex_(_p);
        active_bits_[bit_index] = _v;
    }

    bool get(CacheEntry*, Position_t const &_p)
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
    Position_t base_;
};

template <typename Child, std::size_t Log2Side>
class BranchNode
{
public:
    static constexpr unsigned kNodeLevel = Child::kNodeLevel + 1u;
    using ChildT = Child;

public:
    static constexpr std::size_t kLog2Side = Log2Side + Child::kLog2Side;
    BranchNode() = default;

    explicit BranchNode(bool _active, Position_t const& _base)
        : base_{ _base } {
        if (_active)
            active_bits_.set();
    }

public:

    void set(CacheEntry* _root_cache, Position_t const &_p, bool const _v = true)
    {
        std::size_t const bit_index = BitIndex_(_p);
        if (!child_bits_[bit_index])
        {
            if (_v != active_bits_[bit_index])
            {
                Position_t child_base = ChildBase_(_p);
                children_[bit_index].reset(new Child(active_bits_[bit_index], child_base));

                children_[bit_index]->set(_root_cache, _p, _v);
                child_bits_.set(bit_index);

                _root_cache[kNodeLevel-1u] = CacheEntry{
                    child_base,
                    (void*)children_[bit_index].get()
                };
            }
        }
        else
        {
            children_[bit_index]->set(_root_cache, _p, _v);

            bool all = children_[bit_index]->all();
            bool none = children_[bit_index]->none();
            if (all != none)
            {
                active_bits_.set(bit_index, all);
                child_bits_.set(bit_index, false);

#if 0
                if (_root_cache[kNodeLevel-1u] == (void*)children_[bit_index].get())
                    _root_cache[kNodeLevel-1u] = nullptr;
#endif
                children_[bit_index].reset(nullptr);
            }

            _root_cache[kNodeLevel-1u] = CacheEntry{
                ChildBase_(_p),
                (void*)children_[bit_index].get()
            };
        }
    }

    bool get(CacheEntry* _root_cache, Position_t const &_p) const
    {
        std::size_t const bit_index = BitIndex_(_p);
        if (child_bits_[bit_index])
        {
            _root_cache[kNodeLevel-1u] = CacheEntry{
                ChildBase_(_p),
                (void*)children_[bit_index].get()
            };
            return children_[bit_index]->get(_root_cache, _p);
        }
        else
            return active_bits_[bit_index];
    }

    bool all() const
    {
        return active_bits_.all() && child_bits_.none();
    }

    bool none() const
    {
        return active_bits_.none() && child_bits_.none();
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

public:
    static Position_t ChildBase_(Position_t const& _p)
    {
        constexpr std::int64_t kChildLocalMask = (1u << Child::kLog2Side) - 1u;
        return {
            _p[0] & ~kChildLocalMask,
            _p[1] & ~kChildLocalMask,
            _p[2] & ~kChildLocalMask
        };
    }

private:
    static constexpr std::size_t kSize = kInternalLog2Side * 3u;
    std::array<std::unique_ptr<Child>, 1u << kSize> children_;
    std::bitset<1u << kSize> active_bits_;
    std::bitset<1u << kSize> child_bits_;

    Position_t base_;
};

template <typename Child>
class RootNode
{

public:
    static constexpr unsigned kNodeLevel = Child::kNodeLevel + 1u;
    using ChildT = Child;

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
        std::unique_ptr<Child> child_{ nullptr };
        bool active_ = false;
    };
    using RootMap_t = std::unordered_map<RootKey_t, RootData, RootKeyHash>;

public:
    RootNode()
    {
        for (unsigned i = 0u; i < kNodeLevel; ++i)
            node_cache_[i] = CacheEntry{};
    }

    void set(Position_t const &_p, bool const _v = true)
    {
        unsigned index = CacheIndex_(node_cache_, _p);

        RootKey_t const key = RootKey_(_p);
        typename RootMap_t::iterator nit = root_map_.find(key);
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
                Position_t child_base = ChildBase_(_p);
                data.child_.reset(new Child(data.active_, child_base));
                data.child_->set(node_cache_, _p, _v);

                node_cache_[kNodeLevel-1u] = CacheEntry{
                    child_base,
                    (void*)data.child_.get()
                };
            }
        }
        else
        {
            data.child_->set(node_cache_, _p, _v);

            bool all = data.child_->all();
            bool none = data.child_->none();
            if (all != none)
            {
                data.active_ = all;
                data.child_.reset(nullptr);
            }

            node_cache_[kNodeLevel-1u] = CacheEntry{
                ChildBase_(_p),
                (void*)data.child_.get()
            };
        }
    }

    void reset(Position_t const &_p) { set(_p, false); }

    bool get(Position_t const &_p)
    {
        unsigned index = CacheIndex_(node_cache_, _p);

        RootKey_t const key = RootKey_(_p);
        typename RootMap_t::const_iterator const nit = root_map_.find(key);
        if (nit != root_map_.end())
        {
            RootData const &data = nit->second;
            if (data.child_ != nullptr)
            {
                node_cache_[kNodeLevel-1u] = CacheEntry{
                    ChildBase_(_p),
                    (void*)data.child_.get()
                };
                return data.child_->get(node_cache_, _p);
            }
            else
                return data.active_;
        }
        else
            return false;
    }

    void clear()
    {
        root_map_.clear();
    }

private:
    static RootKey_t RootKey_(Position_t const &_p)
    {
        constexpr std::int64_t kChildLocalMask = (1u << Child::kLog2Side) - 1u;
        return {
            _p[0] & ~kChildLocalMask,
            _p[1] & ~kChildLocalMask,
            _p[2] & ~kChildLocalMask
        };
    }

    static Position_t ChildBase_(Position_t const&_p)
    {
        return (Position_t)RootKey_(_p);
    }

private:
    RootMap_t root_map_{};
    Box_t bounds_{};

private:
    CacheEntry node_cache_[kNodeLevel];

    template <typename Type, unsigned Index> struct TreeTypes;

    using TypeHierarchy = TreeTypes<RootNode<Child>, kNodeLevel>;

    template <typename Type>
    struct TreeTypes<Type, 1u>
    {
        static constexpr unsigned Index = 0u;
        using type = Type;
        using Next = void;
    };

    template <typename Type, unsigned Length>
    struct TreeTypes
    {
        static constexpr unsigned Index = Length-1u;
        using type = Type;
        using Next = TreeTypes<typename Type::ChildT, Index>;
    };

    template <typename T, typename P, unsigned E>
    struct Crawler
    {
        using Type = T;
        static constexpr unsigned Index = E;
        using Prev = P;
        using This = Crawler<Type, Prev, Index>;
        using Next = Crawler<typename Type::ChildT, This, Index-1u>;

        static unsigned up(CacheEntry const* _cache, Position_t const& _p)
        {
            auto childbase_ = Type::ChildBase_;
            unsigned index = Index;

            Position_t child_base = childbase_(_p);
            if (_cache[index].base == child_base)
                return index;
            else
                return Prev::up(_cache, _p);
        }

        static unsigned LookupP(CacheEntry const* _cache, Position_t const& _p)
        {
            return Next::LookupP(_cache, _p);
        }
    };

    template <typename P, unsigned E>
    struct Crawler<void, P, E>
    {
        static unsigned LookupP(CacheEntry const* _cache, Position_t const& _p)
        {
            return P::Prev::up(_cache, _p);
        }
    };

    struct Exit
    {
        static unsigned up(CacheEntry const* _cache, Position_t const& _p)
        {
            return -1u;
        }
    };

    unsigned CacheIndex_(CacheEntry const* _cache, Position_t const& _p)
    {
        unsigned findres = Crawler<RootNode<Child>, Exit, kNodeLevel-1u>::LookupP(_cache, _p);
        return findres;
    }


#ifdef QVDB_BUILD_TESTS
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
#endif // QVDB_BUILD_TESTS
};

} // namespace quick_vdb
