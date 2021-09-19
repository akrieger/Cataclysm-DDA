#include "cata_bitset.h"

#include <stdexcept>

static size_t bit_count_to_block_count( size_t bits )
{
    return ( bits + TinyBitSet::kBitsPerBlock - 1 ) / TinyBitSet::kBitsPerBlock;
}

void TinyBitSet::resize_heap( size_t requested_bits )
{
    size_t blocks_needed = bit_count_to_block_count( requested_bits );

    // Round up to a multiple of the block allocation size.
    // The allocation size is stored by malloc so realloc() can be optimized if we keep resizing by small amounts.
    blocks_needed = ( ( blocks_needed + ( kMinimumBlockPerAlloc - 1 ) ) / kMinimumBlockPerAlloc ) *
                    kMinimumBlockPerAlloc + 1;

    if( is_inline() ) {
        // Allocate an extra block for prepending the capacity to.
        BlockT *data = static_cast<BlockT *>( malloc( sizeof( BlockT ) * ( blocks_needed + 1 ) ) );
        if( data == nullptr ) {
            throw std::runtime_error( "malloc failed" );
        }
        // Capacity goes at the front ahead of the actual bits.
        data[ 0 ] = requested_bits;
        // Copy inline bits to heap.
        data[ 1 ] = storage_ & ~kMetaMask;
        set_storage( data + 1 );
    } else {
        BlockT *data = static_cast<BlockT *>( realloc( real_heap_allocation(),
                                              sizeof( BlockT ) * ( blocks_needed + 1 ) ) );
        if( data != real_heap_allocation() ) {
            set_storage( data + 1 );
        }
    }

    set_size( requested_bits );
}


void TinyBitSet::set_storage( BlockT *data )
{
    memcpy( &storage_, &data, sizeof( data ) );
}


void TinyBitSet::set_size( size_t number_of_bits )
{
    if( is_inline() ) {
        assert( number_of_bits <= kMaxInlineBits );
        uint8_t inline_storage_bits = ( number_of_bits << kInlineSizeBitsOffset ) & kInlineSizeMask;
        storage_ = ( storage_ & ~kInlineSizeMask ) | inline_storage_bits;
    } else {
        *real_heap_allocation() = number_of_bits;
    }
}
