#pragma once
#ifndef CATA_SRC_CATA_BITSET_H
#define CATA_SRC_CATA_BITSET_H

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

class TinyBitSet
{
    public:
        using BlockT = uintptr_t;
        // The layout of this struct is a single uintptr_t type which either holds:
        // - A pointer to heap allocated data, with size + capacity prepended
        // - N-1 bytes of inline storage and 1 byte of metadata
        //   The least significant byte of metadata is, in order of most to least significant,
        //   7 bits of size, and 1 bit indicating inline storage.
        // Whether 32 or 64 bit, just use 6 bits for inline storage (instead of optionally 5 on 32 bit)
        static constexpr BlockT kInlineSizeMask      = 0b11111110;
        static constexpr BlockT kStorageIsInlineMask = 0b00000001;
        // Also known as 0xFF
        static constexpr BlockT kMetaMask            = 0b11111111;

        // Bits available for inline storage minus the control byte.
        static constexpr size_t kMaxInlineBits = std::numeric_limits<uintptr_t>::digits - 8;
        static constexpr size_t kBitsPerBlock = std::numeric_limits<uintptr_t>::digits;

        // Amount to right shift control byte to get capacity.
        static constexpr size_t kInlineSizeBitsOffset = 1;

        // Convenience type for a 1 bit of the appropriate sized type for shifting into masks.
        // This is an _unsigned_ type so right shifting from LEFT_ONE does not sign extend.
        static constexpr BlockT ONE = 1;
        static constexpr BlockT LEFT_ONE = ONE << ( kBitsPerBlock - 1 );
        // Allocate this many blocks at a time, for hardcoded loop unrolling.
        // In 64 bit this is 256 bits. Seems unlikely we'll need more than this.
        static constexpr size_t kMinimumBlockPerAlloc = 4;

        inline TinyBitSet() : TinyBitSet( kMaxInlineBits ) { }

        inline TinyBitSet( size_t initial_capacity ) {
            storage_ = kStorageIsInlineMask;
            resize( initial_capacity );
        }

        inline ~TinyBitSet() {
            if( !is_inline() ) {
                free( real_heap_allocation() );
            }
        }

        // Nontrivial copy constructor due to heap allocation potential
        inline TinyBitSet( TinyBitSet const &rhs ) {
            if( rhs.is_inline() ) {
                storage_ = rhs.storage_;
            } else {
                resize( rhs.capacity() );
                memcpy( bits(), rhs.bits(), rhs.size() / kBitsPerBlock );
            }
        }

        TinyBitSet &operator=( TinyBitSet const &rhs ) {
            if( this == &rhs ) {
                return *this;
            }
            if( rhs.is_inline() ) {
                if( !is_inline() ) {
                    free( real_heap_allocation() );
                }
                storage_ = rhs.storage_;
            } else {
                resize( rhs.capacity() );
                memcpy( bits(), rhs.bits(), rhs.size() / kBitsPerBlock );
            }
            return *this;
        }

        inline TinyBitSet( TinyBitSet &&rhs ) noexcept {
            storage_ = rhs.storage_;
            rhs.storage_ = kStorageIsInlineMask;
        }

        inline TinyBitSet &operator=( TinyBitSet &&rhs ) noexcept {
            std::swap( storage_, rhs.storage_ );
            return *this;
        }

        void swap( TinyBitSet &rhs ) noexcept {
            std::swap( storage_, rhs.storage_ );
        }

        inline void set( size_t idx ) {
            assert( idx < size() );
            size_t block_idx = idx / kBitsPerBlock;
            auto bit_mask = LEFT_ONE >> ( idx % kBitsPerBlock );
            bits()[block_idx] |= bit_mask;
        }

        inline bool test( size_t idx ) {
            assert( idx < size() );
            size_t block_idx = idx / kBitsPerBlock;
            auto bit_mask = LEFT_ONE >> ( idx % kBitsPerBlock );
            return bits()[block_idx] & bit_mask;
        }

        inline void clear( size_t idx ) {
            assert( idx < size() );
            size_t block_idx = idx / kBitsPerBlock;
            // Do not do uint8_t bit_mask = ~(1 << (idx % 8));
            // The RHS evaluates to an _int_
            auto bit_mask = LEFT_ONE >> ( idx % kBitsPerBlock );
            bits()[block_idx] &= ~bit_mask;
        }

        inline void resize( size_t requested_bits ) {
            if( is_inline() && requested_bits <= kMaxInlineBits ) {
                set_size( requested_bits );
                return;
            }
            resize_heap( requested_bits );
        }

        inline bool is_inline() const {
            // Heap pointers can generally be assumed to be aligned to at least 2 bytes.
            // Really, we can assume at least 8 byte alignment, but we only need one bit
            // to denote if we're on the heap or not.
            // If we're on the heap, at least the least significant bit will be 0.
            return static_cast<bool>( storage_ & kStorageIsInlineMask );
        }

        inline size_t size() const {
            if( is_inline() ) {
                return ( storage_ & kInlineSizeMask ) >> kInlineSizeBitsOffset;
            } else {
                return *real_heap_allocation();
            }
        }

        inline size_t capacity() const {
            // We don't track size and capacity separately for convenience.
            return size();
        }

        inline bool all() const {
            AndMapper mapper;
            const_cast<TinyBitSet &>( *this ).map( mapper );
            return ~mapper.value == 0;
        }

        inline bool any() const {
            OrMapper mapper;
            const_cast<TinyBitSet &>( *this ).map( mapper );
            return mapper.value != 0;
        }

        inline bool none() const {
            return !any();
        }

        inline const BlockT *bits() const {
            return const_cast<TinyBitSet &>( *this ).bits();
        }

        inline BlockT *bits() {
            if( is_inline() ) {
                return &storage_;
            } else {
                // The hashtag blessed UB-avoiding way of type punning a pointer
                // out of an integer.
                BlockT *ret;
                memcpy( &ret, &storage_, sizeof( storage_ ) );
                return ret;
            }
        }

        inline void clear_all() {
            map( BitSetter<false> {} );
        }

        inline void set_all() {
            map( BitSetter<true> {} );
        }

    private:
        // foldl helpers
        struct AndMapper {
            BlockT value = ~( BlockT( 0 ) );
            void operator()( BlockT &bits ) {
                value &= bits;
            }

            void operator()( BlockT &partial, BlockT mask ) {
                // Mask indicates bits in v which not 'part of the value'.
                value &= ( partial | mask );
            }
        };
        struct OrMapper {
            BlockT value = 0;
            void operator()( BlockT &bits ) {
                value |= bits;
            }

            void operator()( BlockT &partial, BlockT mask ) {
                // Mask indicates bits in v which not 'part of the value'.
                value |= ( partial & ~mask );
            }
        };
        template<bool set>
        struct BitSetter {
            void operator()( BlockT &bits ) {
                if( set ) {
                    bits = ~0;
                } else {
                    bits = 0;
                }
            }

            void operator()( BlockT &partial, BlockT mask ) {
                if( set ) {
                    partial |= ~mask;
                } else {
                    partial &= mask;
                }
            }
        };

        // Op needs to support two functions
        // void(BlockT&) and void(BlockT&, BlockT). References to bits
        // are passed and may be read or written directly.
        // The second form takes an argument which is a bitmask where set
        // bits correspond to bits to ignore in the data argument. These bits
        // must not be changed, so write operations must appropriately include
        // the existing value in what is written.
        template<typename Op>
        inline void map( Op op ) {
            if( is_inline() ) {
                op( storage_, kMetaMask );
                return;
            }

            // The compiler auto vectorizer is too smart and inserts a bunch of code
            // to handle various cases of how big the data is. We expect things to be
            // small, so we can hand roll a loop which doesn't blow up code size or
            // insert a huge number of conditionals into the hot path, and probably
            // not measurably slower than the fully expanded vectorized code which handles
            // 128 bytes (1,024 bits!) in one pass.
            size_t bits_used = size();
            size_t used_blocks = bits_used / kBitsPerBlock;
            // The final block requires special handling.
            size_t used_bits_in_final_block = bits_used % kBitsPerBlock;
            size_t used_whole_blocks = used_bits_in_final_block > 0 &&
                                       used_blocks > 0 ? used_blocks - 1 : used_blocks;

            // Process 4 at a time for some manual unrolling.
            auto blocks = bits();
            size_t i = 0;
            for( size_t end = used_whole_blocks; i + 3 < end; i += 4 ) {
                op( blocks[i + 0] );
                op( blocks[i + 1] );
                op( blocks[i + 2] );
                op( blocks[i + 3] );
            }
            // final whole blocks that don't fit in unrolled loop.
            for( ; i < used_whole_blocks; ++i ) {
                op( blocks[i] );
            }
            // final, partial block
            BlockT unused_mask;
            if( used_bits_in_final_block != 0 ) {
                // Create mask which is all bits set for bits not used in capacity.
                // If we used N bits, create a mask of the k - N least significant bits by
                // shifting a 1 k-N times, into the k-N'th bit _index_ (or k-N+1'th bit) then
                // subtracting one, setting k-N of the least significant bits to 1.
                unused_mask = ( ONE << ( kBitsPerBlock - used_bits_in_final_block ) ) - 1;
                op( blocks[i], unused_mask );
            }
        }

        inline BlockT const *real_heap_allocation() const {
            return const_cast<TinyBitSet &>( *this ).real_heap_allocation();
        }

        inline BlockT *real_heap_allocation() {
            // When heapified, we store the capacity of the allocation shifted behind the data.
            // For convenience, we assume a size_t can fit into a uintptr_t.
            static_assert( sizeof( size_t ) <= sizeof( uintptr_t ), "What platform has size_t > uintptr_t?" );
            static_assert( alignof( size_t ) <= alignof( uintptr_t ),
                           "What platform has alignof(size_t) > alignof(uintptr_t)?" );

            return bits() - 1;
        }

        void resize_heap( size_t requested_bits );
        void set_storage( BlockT *data );
        void set_size( size_t number_of_bits );

        // This is either inline storage of N-1 bits for N being bits in uintptr_t,
        // or it's a uint8_t* stored in a uintptr_t.
        // If your platform of choice doesn't cast pointers to uintptr_t in the expected
        // way, have a nickel get yourself a better computer.
        uintptr_t storage_;

        static_assert( std::numeric_limits<uintptr_t>::radix == 2, "Non binary integers are forbidden" );
        static_assert( std::numeric_limits<uintptr_t>::digits > 0, "Must be > 0 bits are in uintptr_t" );
};

#endif // CATA_SRC_CATA_BITSET_H
