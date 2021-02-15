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

template <typename T>
static Position_t NodeBase_(Position_t const& _p)
{
    constexpr std::int64_t kNodeLocalMask = (1u << T::kLog2Side) - 1u;
    return {
        _p[0] & ~kNodeLocalMask,
        _p[1] & ~kNodeLocalMask,
        _p[2] & ~kNodeLocalMask
    };
}

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

#ifdef QVDB_ENABLE_CACHE
                _root_cache[kNodeLevel-1u] = CacheEntry{
                    child_base,
                    (void*)children_[bit_index].get()
                };
#endif
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

#ifdef QVDB_ENABLE_CACHE
            _root_cache[kNodeLevel-1u] = CacheEntry{
                ChildBase_(_p),
                (void*)children_[bit_index].get()
            };
#endif
        }
    }

    bool get(CacheEntry* _root_cache, Position_t const &_p) const
    {
        std::size_t const bit_index = BitIndex_(_p);
        if (child_bits_[bit_index])
        {
#ifdef QVDB_ENABLE_CACHE
            _root_cache[kNodeLevel-1u] = CacheEntry{
                ChildBase_(_p),
                (void*)children_[bit_index].get()
            };
#endif
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
#ifdef QVDB_ENABLE_CACHE
        for (unsigned i = 0u; i < kNodeLevel; ++i)
            node_cache_[i] = CacheEntry{ Position_t{}, nullptr };
#endif
    }

    void set(Position_t const &_p, bool const _v = true)
    {
#ifdef QVDB_ENABLE_CACHE
        unsigned entry_index = ExecOnCache<SetOp>{}(*this, _p, nullptr, &node_cache_[0], _p, _v);
        if (entry_index != -1u)
            return;
#endif

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

#ifndef QVDB_ENABLE_CACHE
                data.child_->set(nullptr, _p, _v);
#else
                data.child_->set(node_cache_, _p, _v);

                node_cache_[kNodeLevel-1u] = CacheEntry{
                    child_base,
                    (void*)data.child_.get()
                };
#endif
            }
        }
        else
        {

#ifndef QVDB_ENABLE_CACHE
            data.child_->set(nullptr, _p, _v);
#else
            data.child_->set(node_cache_, _p, _v);
#endif

            bool all = data.child_->all();
            bool none = data.child_->none();
            if (all != none)
            {
                data.active_ = all;
                data.child_.reset(nullptr);
            }

#ifdef QVDB_ENABLE_CACHE
            node_cache_[kNodeLevel-1u] = CacheEntry{
                ChildBase_(_p),
                (void*)data.child_.get()
            };
#endif
        }
    }

    void reset(Position_t const &_p) { set(_p, false); }

    bool get(Position_t const &_p)
    {
#ifdef QVDB_ENABLE_CACHE
        bool result = false;
        unsigned entry_index = ExecOnCache<GetOp>{}(*this, _p, &result, &node_cache_[0], _p);
        if (entry_index != -1u)
            return result;
#endif

        RootKey_t const key = RootKey_(_p);
        typename RootMap_t::const_iterator const nit = root_map_.find(key);
        if (nit != root_map_.end())
        {
            RootData const &data = nit->second;
            if (data.child_ != nullptr)
            {

#ifndef QVDB_ENABLE_CACHE
                return data.child_->get(nullptr, _p);
#else
                node_cache_[kNodeLevel-1u] = CacheEntry{
                    ChildBase_(_p),
                    (void*)data.child_.get()
                };
                return data.child_->get(node_cache_, _p);
#endif

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

#ifdef QVDB_ENABLE_CACHE
private:
    CacheEntry node_cache_[kNodeLevel];

    enum eOpType {
        kGet,
        kSet,
        kCompareBase,
    };

    template <typename T>
    struct GetOp
    {
        template <typename ... Args>
        static void apply(CacheEntry const& _entry, void* _out, Args ... args)
        {
            *(bool*)_out = reinterpret_cast<T*>(_entry.node)->get(args...);
        }
    };

    template <typename T>
    struct SetOp
    {
        template <typename ... Args>
        static void apply(CacheEntry const& _entry, void* _out, Args ... args)
        {
            reinterpret_cast<T*>(_entry.node)->set(args...);
        }
    };

    template <typename T>
    struct CompareBaseOp
    {
        template <typename ... Args>
        static void apply(CacheEntry const& _entry, void* _out, Args ... args)
        {
            *(bool*)_out = (_entry.base == NodeBase_<T>(args...));
        }
    };


    template <template <typename> typename Op, typename T, unsigned Index, unsigned Search>
    struct Indexer
    {
        using Head = T;
        using Tail = typename Head::ChildT;
        using Next = Indexer<Op, Tail, Index-1u, Search>;

        template <typename ... Args>
        void route(CacheEntry const& _entry, void* _out, Args ... args)
        {
            Next{}.route(_entry, _out, args...);
        }
    };

    template <template <typename> typename Op, typename T, unsigned Match>
    struct Indexer<Op, T, Match, Match>
    {
        template <typename ... Args>
        void route(CacheEntry const& _entry, void* _out, Args ... args)
        {
            Op<T>::apply(_entry, _out, args...);
        }
    };

    template <template <typename> typename Op, typename T, unsigned Search>
    struct Indexer<Op, T, -1u, Search>
    {
        template <typename ... Args>
        void route(CacheEntry const&, void*, Args ...)
        {
        }
    };

    template <template <typename> typename Op, unsigned I>
    using CacheIndexer = Indexer<Op, RootNode<Child>, kNodeLevel, I>;

    template <template <typename> typename Op, typename T, unsigned R, unsigned N>
    struct For : For<Op, T, R+1, N>
    {
        template <typename ... Args>
        unsigned route(Position_t const& _p,
                       CacheEntry* _node_cache,
                       void* _out, Args ... args)
        {
            if (_node_cache[R].node)
            {
                bool compareBase = false;
                CacheIndexer<CompareBaseOp, R>{}.route(_node_cache[R], &compareBase, _p);

                if (compareBase)
                {
                    CacheIndexer<Op, R>{}.route(_node_cache[R], _out, args...);
                    return R;
                }
            }

            return reinterpret_cast<For<Op, T, R+1, N>*>(this)->route(_p, _node_cache, _out, args...);
        }
    };

    template <template <typename> typename Op, typename T, unsigned E>
    struct For<Op, T, E, E>
    {
        template <typename ... Args>
        unsigned route(Position_t const&,
                       CacheEntry*,
                       void*, Args ...)
        {
            return -1u;
        }
    };

    template <template <typename> typename Op>
    struct ExecOnCache
    {
        template <typename ... Args>
        unsigned operator()(RootNode<Child> &root, Position_t const& _p, void* _out, Args ... args)
        {
            return For<Op, RootNode<Child>, 0u, RootNode<Child>::kNodeLevel>{}.route(
                _p, root.node_cache_, _out, args...);
        }
    };

#endif


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
