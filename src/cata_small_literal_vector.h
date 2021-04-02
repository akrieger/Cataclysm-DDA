#pragma once
#ifndef CATA_SRC_CATA_SMALL_LITERAL_VECTOR_H
#define CATA_SRC_CATA_SMALL_LITERAL_VECTOR_H

#include <cstdlib>
#include <memory>
#include <type_traits>

#include "cata_scope_exit.h"

template<typename T, size_t kInlineCount = 4>
struct SmallLiteralVector {
        // To avoid having to invoke constructors and destructors when copying Elements
        // around or inserting new ones, we enforce it is a literal type.
        static_assert( std::is_literal_type<T>::value, "T must be literal." );

        SmallLiteralVector() {}
        SmallLiteralVector( SmallLiteralVector const &other ) {
            copy_init_with_capacity( other, other.capacity_ );
        }
        SmallLiteralVector &operator=( SmallLiteralVector const &rhs ) {
            copy_init_with_capacity( rhs, rhs.capacity_ );
            return *this;
        }

        SmallLiteralVector( SmallLiteralVector &&other ) noexcept {
            steal_init( std::move( other ) );
        }
        SmallLiteralVector &operator=( SmallLiteralVector &&rhs ) noexcept {
            steal_init( std::move( rhs ) );
            return *this;
        }

        ~SmallLiteralVector() {
            if( on_heap() ) {
                free( heap_ );
            }
        }

        T *begin() {
            return data();
        }

        T *back() {
            return data() + size() - 1;
        }

        T *end() {
            return data() + size();
        }

        const T *begin() const {
            return data();
        }

        const T *back() const {
            return data() + size() - 1;
        }

        const T *end() const {
            return data() + size();
        }

        T *data() {
            return on_heap() ? heap_ : inline_;
        }

        const T *data() const {
            return on_heap() ? heap_ : inline_;
        }

        size_t size() const {
            return len_;
        }

        void push_back( T const &t ) {
            ensure_capacity_for( size() + 1 );
            *end() = t;
            len_ += 1;
        }
        template<typename ...US, typename std::enable_if<std::is_constructible<T, US...>::value>::type * = 0>
        void push_back( US && ...us ) {
            ensure_capacity_for( size() + 1 );
            *end() = T( std::forward < US && > ( us )... );
            len_ += 1;
        }

        // Some SFINAE goo to only let us concat SmallLiteralVector<T> and Ts.
        template<typename U>
        struct is_concatenable : std::false_type {};

        template<>
        struct is_concatenable<T> : std::true_type {};

        template<>
        struct is_concatenable<SmallLiteralVector> : std::true_type {};

        template<typename... Conds>
        struct and_ : std::true_type {};

        template<typename Cond, typename... Conds>
        struct and_<Cond, Conds...>
            : std::conditional<Cond::value, and_<Conds...>, std::false_type>::type {};

        template<class... Ts>
        using are_all_concatenable = and_<is_concatenable<typename std::decay<Ts>::type>...>;

        // Some nonstandard helpers below.
        template<typename ...Insertables, typename std::enable_if<are_all_concatenable<Insertables...>::value>::type * = nullptr>
        void concat( Insertables && ...is ) {
            ensure_capacity_for( capacity_needed_for( std::forward < Insertables && > ( is )... ) );
            concat_unsafe( std::forward < Insertables && > ( is )... );
        }

        void ensure_capacity_for( size_t count ) {
            if( count <= capacity_ ) {
                return;
            }

            size_t new_capacity = capacity_;
            while( new_capacity < count ) {
                capacity_ *= 2;
            }
            T *heap = nullptr;
            if( capacity_ == kInlineCount ) {
                cata_scope_exit heap_guard( [heap] { free( heap ); } );
                heap = ( T * )std::malloc( sizeof( T ) * new_capacity );
                std::uninitialized_copy_n( inline_, size(), heap );
                heap_ = heap;
                capacity_ = new_capacity;
                heap_guard.dismiss();
            } else {
                heap = ( T * )std::realloc( heap_, sizeof( T ) * new_capacity );
                if( heap ) {
                    heap_ = heap;
                } else {
                    throw std::runtime_error( "realloc failed." );
                }
                capacity_ = new_capacity;
            }
        }

    private:

        template<typename I, typename ...Insertables>
        static size_t capacity_needed_for( I &&i, Insertables &&...is ) {
            return capacity_needed_for( std::forward < I && > ( i ) ) +
                   capacity_needed_for( std::forward < Insertables && > ( is )... );
        }

        static size_t capacity_needed_for() {
            return 0;
        }
        template<>
        static size_t capacity_needed_for( SmallLiteralVector const &v ) {
            return v.size();
        }
        template<>
        static size_t capacity_needed_for( T const & ) {
            return 1;
        }
        template<>
        static size_t capacity_needed_for( T && ) {
            return 1;
        }

        // Some nonstandard helpers below.
        void concat_unsafe() {}
        void concat_unsafe( SmallLiteralVector const &v ) {
            std::uninitialized_copy_n( v.data(), v.size(), end() );
            len_ += v.size();
        }

        void concat_unsafe( T const &t ) {
            *end() = t;
            ++len_;
        }

        void concat_unsafe( T &&t ) {
            *end() = std::move( t );
            ++len_;
        }

        template<typename I, typename ...Insertables>
        void concat_unsafe( I &&i, Insertables &&...is ) {
            concat_unsafe( std::forward < I && > ( i ) );
            concat_unsafe( std::forward < Insertables && > ( is )... );
        }

        void copy_init_with_capacity( SmallLiteralVector const &other, size_t capacity ) {
            ensure_capacity_for( std::max( other.len_, capacity ) );
            std::uninitialized_copy_n( other.data(), other.size(), data() );
            len_ = other.len_;
        }

        void steal_init( SmallLiteralVector &&other ) noexcept {
            if( other.on_heap() ) {
                // Optimization: steal the heap pointer.
                heap_ = other.heap_;
                other.heap_ = nullptr;
            } else {
                // Skip the ensure_capacity_for call cause we know it has to be inline.
                std::uninitialized_copy_n( other.inline_, other.len_, inline_ );
            }
            len_ = other.len_;
            capacity_ = other.capacity_;
            other.capacity_ = kInlineCount;
        }

        bool on_heap() const {
            return capacity_ > kInlineCount;
        }
        // kInlineCount = 4 means this is 80 bytes big. Not bad.
        size_t len_ = 0;
        // capacity_ > kInlineCount => storage_ on heap.
        size_t capacity_ = kInlineCount;
        union {
            T *heap_;
            T inline_[ kInlineCount ];
        };
};

#endif
