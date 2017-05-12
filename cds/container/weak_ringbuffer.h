/*
    This file is a part of libcds - Concurrent Data Structures library

    (C) Copyright Maxim Khizhinsky (libcds.dev@gmail.com) 2006-2017

    Source code repo: http://github.com/khizmax/libcds/
    Download: http://sourceforge.net/projects/libcds/files/

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CDSLIB_CONTAINER_WEAK_RINGBUFFER_H
#define CDSLIB_CONTAINER_WEAK_RINGBUFFER_H

#include <cds/container/details/base.h>
#include <cds/opt/buffer.h>
#include <cds/opt/value_cleaner.h>
#include <cds/algo/atomic.h>
#include <cds/details/bounded_container.h>

namespace cds { namespace container {

    /// \p WeakRingBuffer related definitions
    /** @ingroup cds_nonintrusive_helper
    */
    namespace weak_ringbuffer {

        /// \p WeakRingBuffer default traits
        struct traits {
            /// Buffer type for internal array
            /*
                The type of element for the buffer is not important: \p WeakRingBuffer rebind
                the buffer for required type via \p rebind metafunction.

                For \p WeakRingBuffer the buffer size should have power-of-2 size.

                You should use only uninitialized buffer for the ring buffer -
                \p cds::opt::v::uninitialized_dynamic_buffer (the default),
                \p cds::opt::v::uninitialized_static_buffer.
            */
            typedef cds::opt::v::uninitialized_dynamic_buffer< void * > buffer;

            /// A functor to clean item dequeued.
            /**
                The functor calls the destructor for popped element.
                After a set of items is dequeued, \p value_cleaner cleans the cells that the items have been occupied.
                If \p T is a complex type, \p value_cleaner may be useful feature.
                For POD types \ref opt::v::empty_cleaner is suitable

                Default value is \ref opt::v::auto_cleaner that calls destructor only if it is not trivial.
            */
            typedef cds::opt::v::auto_cleaner value_cleaner;

            /// C++ memory ordering model
            /**
                Can be \p opt::v::relaxed_ordering (relaxed memory model, the default)
                or \p opt::v::sequential_consistent (sequentially consistent memory model).
            */
            typedef opt::v::relaxed_ordering    memory_model;

            /// Padding for internal critical atomic data. Default is \p opt::cache_line_padding
            enum { padding = opt::cache_line_padding };
        };

        /// Metafunction converting option list to \p weak_ringbuffer::traits
        /**
            Supported \p Options are:
            - \p opt::buffer - an uninitialized buffer type for internal cyclic array. Possible types are:
                \p opt::v::uninitialized_dynamic_buffer (the default), \p opt::v::uninitialized_static_buffer. The type of
                element in the buffer is not important: it will be changed via \p rebind metafunction.
            - \p opt::value_cleaner - a functor to clean items dequeued.
                The functor calls the destructor for ring-buffer item.
                After a set of items is dequeued, \p value_cleaner cleans the cells that the items have been occupied.
                If \p T is a complex type, \p value_cleaner can be an useful feature.
                Default value is \ref opt::v::empty_cleaner that is suitable for POD types.
            - \p opt::padding - padding for internal critical atomic data. Default is \p opt::cache_line_padding
            - \p opt::memory_model - C++ memory ordering model. Can be \p opt::v::relaxed_ordering (relaxed memory model, the default)
                or \p opt::v::sequential_consistent (sequentially consisnent memory model).

            Example: declare \p %WeakRingBuffer with static iternal buffer of size 1024:
            \code
            typedef cds::container::WeakRingBuffer< Foo,
                typename cds::container::weak_ringbuffer::make_traits<
                    cds::opt::buffer< cds::opt::v::uninitialized_static_buffer< void *, 1024 >
                >::type
            > myRing;
            \endcode
        */
        template <typename... Options>
        struct make_traits {
#   ifdef CDS_DOXYGEN_INVOKED
            typedef implementation_defined type;   ///< Metafunction result
#   else
            typedef typename cds::opt::make_options<
                typename cds::opt::find_type_traits< traits, Options... >::type
                , Options...
            >::type type;
#   endif
        };

    } // namespace weak_ringbuffer

    /// Single-producer single-consumer ring buffer
    /** @ingroup cds_nonintrusive_queue
        Source: [2013] Nhat Minh Le, Adrien Guatto, Albert Cohen, Antoniu Pop. Correct and Effcient Bounded
            FIFO Queues. [Research Report] RR-8365, INRIA. 2013. <hal-00862450>

        Ring buffer is a bounded queue. Additionally, \p %WeakRingBuffer supports batch operations -
        you can push/pop an array of elements.

        There are a specialization \ref cds_nonintrusive_WeakRingBuffer_void "WeakRingBuffer<void, Traits>" 
        that is not a queue but a "memory pool" between producer and consumer threads. 
        \p WeakRingBuffer<void> supports data of different size.
    */
    template <typename T, typename Traits = weak_ringbuffer::traits>
    class WeakRingBuffer: public cds::bounded_container
    {
    public:
        typedef T value_type;   ///< Value type to be stored in the ring buffer
        typedef Traits traits;  ///< Ring buffer traits
        typedef typename traits::memory_model  memory_model;  ///< Memory ordering. See \p cds::opt::memory_model option
        typedef typename traits::value_cleaner value_cleaner; ///< Value cleaner, see \p weak_ringbuffer::traits::value_cleaner

        /// Rebind template arguments
        template <typename T2, typename Traits2>
        struct rebind {
            typedef WeakRingBuffer< T2, Traits2 > other;   ///< Rebinding result
        };

        //@cond
        // Only for tests
        typedef size_t item_counter;
        //@endcond

    private:
        //@cond
        typedef typename traits::buffer::template rebind< value_type >::other buffer;
        //@endcond

    public:

        /// Creates the ring buffer of \p capacity
        /**
            For \p cds::opt::v::uninitialized_static_buffer the \p nCapacity parameter is ignored.

            If the buffer capacity is a power of two, lightweight binary arithmetics is used
            instead of modulo arithmetics.
        */
        WeakRingBuffer( size_t capacity = 0 )
            : front_( 0 )
            , pfront_( 0 )
            , cback_( 0 )
            , buffer_( capacity )
        {
            back_.store( 0, memory_model::memory_order_release );
        }

        /// Destroys the ring buffer
        ~WeakRingBuffer()
        {
            value_cleaner cleaner;
            size_t back = back_.load( memory_model::memory_order_relaxed );
            for ( size_t front = front_.load( memory_model::memory_order_relaxed ); front != back; ++front )
                cleaner( buffer_[ buffer_.mod( front ) ] );
        }

        /// Batch push - push array \p arr of size \p count
        /**
            \p CopyFunc is a per-element copy functor: for each element of \p arr
            <tt>copy( dest, arr[i] )</tt> is called.
            The \p CopyFunc signature:
            \code
                void copy_func( value_type& element, Q const& source );
            \endcode
            Here \p element is uninitialized so you should construct it using placement new
            if needed; for example, if the element type is \p str::string and \p Q is <tt>char const*</tt>,
            \p copy functor can be:
            \code
            cds::container::WeakRingBuffer<std::string> ringbuf;
            char const* arr[10];
            ringbuf.push( arr, 10, 
                []( std::string& element, char const* src ) {
                    new( &element ) std::string( src );
                });
            \endcode
            You may use move semantics if appropriate:
            \code
            cds::container::WeakRingBuffer<std::string> ringbuf;
            std::string arr[10];
            ringbuf.push( arr, 10,
                []( std::string& element, std:string& src ) {
                    new( &element ) std::string( std::move( src ));
                });
            \endcode

            Returns \p true if success or \p false if not enough space in the ring
        */
        template <typename Q, typename CopyFunc>
        bool push( Q* arr, size_t count, CopyFunc copy )
        {
            assert( count < capacity() );
            size_t back = back_.load( memory_model::memory_order_relaxed );

            assert( back - pfront_ <= capacity() );

            if ( pfront_ + capacity() - back < count ) {
                pfront_ = front_.load( memory_model::memory_order_acquire );

                if ( pfront_ + capacity() - back < count ) {
                    // not enough space
                    return false;
                }
            }

            // copy data
            for ( size_t i = 0; i < count; ++i, ++back )
                copy( buffer_[buffer_.mod( back )], arr[i] );

            back_.store( back, memory_model::memory_order_release );

            return true;
        }

        /// Batch push - push array \p arr of size \p count with assignment as copy functor
        /**
            This function is equivalent for:
            \code
            push( arr, count, []( value_type& dest, Q const& src ) { dest = src; } );
            \endcode

            The function is available only if <tt>std::is_constructible<value_type, Q>::value</tt>
            is \p true.

            Returns \p true if success or \p false if not enough space in the ring
        */
        template <typename Q>
        typename std::enable_if< std::is_constructible<value_type, Q>::value, bool>::type
        push( Q* arr, size_t count )
        {
            return push( arr, count, []( value_type& dest, Q const& src ) { new( &dest ) value_type( src ); } );
        }

        /// Push one element created from \p args
        /**
            The function is available only if <tt>std::is_constructible<value_type, Args...>::value</tt>
            is \p true.

            Returns \p false if the ring is full or \p true otherwise.
        */
        template <typename... Args>
        typename std::enable_if< std::is_constructible<value_type, Args...>::value, bool>::type
        emplace( Args&&... args )
        {
            size_t back = back_.load( memory_model::memory_order_relaxed );

            assert( back - pfront_ <= capacity() );

            if ( pfront_ + capacity() - back < 1 ) {
                pfront_ = front_.load( memory_model::memory_order_acquire );

                if ( pfront_ + capacity() - back < 1 ) {
                    // not enough space
                    return false;
                }
            }

            new( &buffer_[buffer_.mod( back )] ) value_type( std::forward<Args>(args)... );

            back_.store( back + 1, memory_model::memory_order_release );

            return true;
        }

        /// Enqueues data to the ring using a functor
        /**
            \p Func is a functor called to copy a value to the ring element.
            The functor \p f takes one argument - a reference to a empty cell of type \ref value_type :
            \code
            cds::container::WeakRingBuffer< Foo > myRing;
            Bar bar;
            myRing.enqueue_with( [&bar]( Foo& dest ) { dest = std::move(bar); } );
            \endcode
        */
        template <typename Func>
        bool enqueue_with( Func f )
        {
            size_t back = back_.load( memory_model::memory_order_relaxed );

            assert( back - pfront_ <= capacity() );

            if ( pfront_ + capacity() - back < 1 ) {
                pfront_ = front_.load( memory_model::memory_order_acquire );

                if ( pfront_ + capacity() - back < 1 ) {
                    // not enough space
                    return false;
                }
            }

            f( buffer_[buffer_.mod( back )] );

            back_.store( back + 1, memory_model::memory_order_release );

            return true;

        }

        /// Enqueues \p val value into the queue.
        /**
            The new queue item is created by calling placement new in free cell.
            Returns \p true if success, \p false if the ring is full.
        */
        bool enqueue( value_type const& val )
        {
            return emplace( val );
        }

        /// Enqueues \p val value into the queue, move semantics
        bool enqueue( value_type&& val )
        {
            return emplace( std::move( val ));
        }

        /// Synonym for \p enqueue( value_type const& )
        bool push( value_type const& val )
        {
            return enqueue( val );
        }

        /// Synonym for \p enqueue( value_type&& )
        bool push( value_type&& val )
        {
            return enqueue( std::move( val ));
        }

        /// Synonym for \p enqueue_with()
        template <typename Func>
        bool push_with( Func f )
        {
            return enqueue_with( f );
        }

        /// Batch pop \p count element from the ring buffer into \p arr
        /**
            \p CopyFunc is a per-element copy functor: for each element of \p arr
            <tt>copy( arr[i], source )</tt> is called.
            The \p CopyFunc signature:
            \code
            void copy_func( Q& dest, value_type& elemen );
            \endcode

            Returns \p true if success or \p false if not enough space in the ring
        */
        template <typename Q, typename CopyFunc>
        bool pop( Q* arr, size_t count, CopyFunc copy )
        {
            assert( count < capacity() );

            size_t front = front_.load( memory_model::memory_order_relaxed );
            assert( cback_ - front < capacity() );

            if ( cback_ - front < count ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                if ( cback_ - front < count )
                    return false;
            }

            // copy data
            value_cleaner cleaner;
            for ( size_t i = 0; i < count; ++i, ++front ) {
                value_type& val = buffer_[buffer_.mod( front )];
                copy( arr[i], val );
                cleaner( val );
            }

            front_.store( front, memory_model::memory_order_release );
            return true;
        }

        /// Batch pop - push array \p arr of size \p count with assignment as copy functor
        /**
            This function is equivalent for:
            \code
            pop( arr, count, []( Q& dest, value_type& src ) { dest = src; } );
            \endcode

            The function is available only if <tt>std::is_assignable<Q&, value_type const&>::value</tt>
            is \p true.

            Returns \p true if success or \p false if not enough space in the ring
        */
        template <typename Q>
        typename std::enable_if< std::is_assignable<Q&, value_type const&>::value, bool>::type
        pop( Q* arr, size_t count )
        {
            return pop( arr, count, []( Q& dest, value_type& src ) { dest = src; } );
        }

        /// Dequeues an element from the ring to \p val
        /**
            The function is available only if <tt>std::is_assignable<Q&, value_type const&>::value</tt>
            is \p true.

            Returns \p false if the ring is full or \p true otherwise.
        */
        template <typename Q>
        typename std::enable_if< std::is_assignable<Q&, value_type const&>::value, bool>::type
        dequeue( Q& val )
        {
            return pop( &val, 1 );
        }

        /// Synonym for \p dequeue( Q& )
        template <typename Q>
        typename std::enable_if< std::is_assignable<Q&, value_type const&>::value, bool>::type
        pop( Q& val )
        {
            return dequeue( val );
        }

        /// Dequeues a value using a functor
        /**
            \p Func is a functor called to copy dequeued value.
            The functor takes one argument - a reference to removed node:
            \code
            cds:container::WeakRingBuffer< Foo > myRing;
            Bar bar;
            myRing.dequeue_with( [&bar]( Foo& src ) { bar = std::move( src );});
            \endcode

            Returns \p true if the ring is not empty, \p false otherwise.
            The functor is called only if the ring is not empty.
        */
        template <typename Func>
        bool dequeue_with( Func f )
        {
            size_t front = front_.load( memory_model::memory_order_relaxed );
            assert( cback_ - front < capacity() );

            if ( cback_ - front < 1 ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                if ( cback_ - front < 1 )
                    return false;
            }

            value_type& val = buffer_[buffer_.mod( front )];
            f( val );
            value_cleaner()( val );

            front_.store( front + 1, memory_model::memory_order_release );
            return true;
        }

        /// Synonym for \p dequeue_with()
        template <typename Func>
        bool pop_with( Func f )
        {
            return dequeue_with( f );
        }

        /// Gets pointer to first element of ring buffer
        /**
            If the ring buffer is empty, returns \p nullptr

            The function is thread-safe since there is only one consumer.
            Recall, \p WeakRingBuffer is single-producer/single consumer container.
        */
        value_type* front()
        {
            size_t front = front_.load( memory_model::memory_order_relaxed );
            assert( cback_ - front < capacity() );

            if ( cback_ - front < 1 ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                if ( cback_ - front < 1 )
                    return nullptr;
            }

            return &buffer_[buffer_.mod( front )];
        }

        /// Removes front element of ring-buffer
        /**
            If the ring-buffer is empty, returns \p false.
            Otherwise, pops the first element from the ring.
        */
        bool pop_front()
        {
            size_t front = front_.load( memory_model::memory_order_relaxed );
            assert( cback_ - front <= capacity() );

            if ( cback_ - front < 1 ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                if ( cback_ - front < 1 )
                    return false;
            }

            // clean cell
            value_cleaner()( buffer_[buffer_.mod( front )] );

            front_.store( front + 1, memory_model::memory_order_release );
            return true;
        }

        /// Clears the ring buffer (only consumer can call this function!)
        void clear()
        {
            value_type v;
            while ( pop( v ) );
        }

        /// Checks if the ring-buffer is empty
        bool empty() const
        {
            return front_.load( memory_model::memory_order_relaxed ) == back_.load( memory_model::memory_order_relaxed );
        }

        /// Checks if the ring-buffer is full
        bool full() const
        {
            return back_.load( memory_model::memory_order_relaxed ) - front_.load( memory_model::memory_order_relaxed ) >= capacity();
        }

        /// Returns the current size of ring buffer
        size_t size() const
        {
            return back_.load( memory_model::memory_order_relaxed ) - front_.load( memory_model::memory_order_relaxed );
        }

        /// Returns capacity of the ring buffer
        size_t capacity() const
        {
            return buffer_.capacity();
        }

    private:
        //@cond
        atomics::atomic<size_t>     front_;
        typename opt::details::apply_padding< atomics::atomic<size_t>, traits::padding >::padding_type pad1_;
        atomics::atomic<size_t>     back_;
        typename opt::details::apply_padding< atomics::atomic<size_t>, traits::padding >::padding_type pad2_;
        size_t                      pfront_;
        typename opt::details::apply_padding< size_t, traits::padding >::padding_type pad3_;
        size_t                      cback_;
        typename opt::details::apply_padding< size_t, traits::padding >::padding_type pad4_;

        buffer                      buffer_;
        //@endcond
    };


    /// Single-producer single-consumer ring buffer for untyped variable-sized data
    /** @ingroup cds_nonintrusive_queue
        @anchor cds_nonintrusive_WeakRingBuffer_void
    */
#ifdef CDS_DOXYGEN_INVOKED
    template <typename Traits = weak_ringbuffer::traits>
#else
    template <typename Traits>
#endif
    class WeakRingBuffer<void, Traits>: public cds::bounded_container
    {
    public:
        typedef Traits      traits;         ///< Ring buffer traits
        typedef typename    traits::memory_model  memory_model;  ///< Memory ordering. See \p cds::opt::memory_model option

    private:
        //@cond
        typedef typename traits::buffer::template rebind< uint8_t >::other buffer;
        //@endcond

    public:
        /// Creates the ring buffer of \p capacity bytes
        /**
            For \p cds::opt::v::uninitialized_static_buffer the \p nCapacity parameter is ignored.

            If the buffer capacity is a power of two, lightweight binary arithmetics is used
            instead of modulo arithmetics.
        */
        WeakRingBuffer( size_t capacity = 0 )
            : front_( 0 )
            , pfront_( 0 )
            , cback_( 0 )
            , buffer_( capacity )
        {
            back_.store( 0, memory_model::memory_order_release );
        }

        /// [producer] Reserve \p size bytes
        void* back( size_t size )
        {
            // Any data is rounded to 8-byte boundary
            size_t real_size = calc_real_size( size );

            // check if we can reserve read_size bytes
            assert( real_size < capacity() );
            size_t back = back_.load( memory_model::memory_order_relaxed );

            assert( back - pfront_ <= capacity() );

            if ( pfront_ + capacity() - back < real_size ) {
                pfront_ = front_.load( memory_model::memory_order_acquire );

                if ( pfront_ + capacity() - back < real_size ) {
                    // not enough space
                    return nullptr;
                }
            }

            uint8_t* reserved = buffer_.buffer() + buffer_.mod( back );

            // Check if the buffer free space is enough for storing real_size bytes
            size_t tail_size = capacity() - buffer_.mod( back );
            if ( tail_size < real_size ) {
                // make unused tail
                assert( tail_size >= sizeof( size_t ) );
                assert( !is_tail( tail_size ) );

                *reinterpret_cast<size_t*>( reserved ) = make_tail( tail_size - sizeof(size_t));
                back += tail_size;

                // We must be in beginning of buffer
                assert( buffer_.mod( back ) == 0 );

                if ( pfront_ + capacity() - back < real_size ) {
                    pfront_ = front_.load( memory_model::memory_order_acquire );

                    if ( pfront_ + capacity() - back < real_size ) {
                        // not enough space
                        return nullptr;
                    }
                }

                reserved = buffer_.buffer();
            }

            // reserve and store size
            uint8_t* reserved = buffer_.buffer() + buffer_.mod( back );
            *reinterpret_cast<size_t*>( reserved ) = size;

            return reinterpret_cast<void*>( reserved + sizeof( size_t ) );
        }

        /// [producer] Push reserved bytes into ring
        void push_back()
        {
            size_t back = back_.load( memory_model::memory_order_relaxed );
            uint8_t* reserved = buffer_.buffer() + buffer_.mod( back );

            size_t real_size = calc_real_size( *reinterpret_cast<size_t*>( reserved ) );
            assert( real_size < capacity() );

            back_.store( back + real_size, memory_model::memory_order_release );
        }

        /// [producer] Push \p data of \p size bytes into ring
        bool push_back( void const* data, size_t size )
        {
            void* buf = back( size );
            if ( buf ) {
                memcpy( buf, data, size );
                push_back();
                return true;
            }
            return false;
        }

        /// [consumer] Get top data from the ring
        std::pair<void*, size_t> front()
        {
            size_t front = front_.load( memory_model::memory_order_relaxed );
            assert( cback_ - front < capacity() );

            if ( cback_ - front < sizeof( size_t )) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                if ( cback_ - front < sizeof( size_t ) )
                    return std::make_pair( nullptr, 0u );
            }

            uint8_t * buf = buffer_.buffer() + buffer_.mod( front );

            // check alignment
            assert( ( reinterpret_cast<uintptr_t>( buf ) & ( sizeof( uintptr_t ) - 1 ) ) == 0 );

            size_t size = *reinterpret_cast<size_t*>( buf );
            if ( is_tail( size ) ) {
                // unused tail, skip
                CDS_VERIFY( pop_front() );

                front = front_.load( memory_model::memory_order_relaxed );
                buf = buffer_.buffer() + buffer_.mod( front );
                size = *reinterpret_cast<size_t*>( buf );

                assert( !is_tail( size ) );
            }

#ifdef _DEBUG
            size_t real_size = calc_real_size( size );
            if ( cback_ - front < real_size ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                assert( cback_ - front >= real_size );
            }
#endif

            return std::make_pair( reinterpret_cast<void*>( buf + sizeof( size_t ) ), size );
        }

        /// [consumer] Pops top data
        bool pop_front()
        {
            size_t front = front_.load( memory_model::memory_order_relaxed );
            assert( cback_ - front <= capacity() );

            if ( cback_ - front < sizeof(size_t) ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                if ( cback_ - front < sizeof( size_t ) )
                    return false;
            }

            uint8_t * buf = buffer_.buffer() + buffer_.mod( front );

            // check alignment
            assert( ( reinterpret_cast<uintptr_t>( buf ) & ( sizeof( uintptr_t ) - 1 ) ) == 0 );

            size_t size = *reinterpret_cast<size_t*>( buf );
            assert( !is_tail( size ) );

            size_t real_size = calc_real_size( size );

#ifdef _DEBUG
            if ( cback_ - front < real_size ) {
                cback_ = back_.load( memory_model::memory_order_acquire );
                assert( cback_ - front >= real_size );
            }
#endif

            front_.store( front + real_size, memory_model::memory_order_release );
            return true;

        }

        /// [consumer] Clears the ring buffer
        void clear()
        {
            for ( auto el = front(); el.first; el = front() )
                pop_front();
        }

        /// Checks if the ring-buffer is empty
        bool empty() const
        {
            return front_.load( memory_model::memory_order_relaxed ) == back_.load( memory_model::memory_order_relaxed );
        }

        /// Checks if the ring-buffer is full
        bool full() const
        {
            return back_.load( memory_model::memory_order_relaxed ) - front_.load( memory_model::memory_order_relaxed ) >= capacity();
        }

        /// Returns the current size of ring buffer
        size_t size() const
        {
            return back_.load( memory_model::memory_order_relaxed ) - front_.load( memory_model::memory_order_relaxed );
        }

        /// Returns capacity of the ring buffer
        size_t capacity() const
        {
            return buffer_.capacity();
        }

    private:
        static size_t calc_real_size( size_t size )
        {
            size_t real_size =  (( size + sizeof( uintptr_t ) - 1 ) & ~( sizeof( uintptr_t ) - 1 )) + sizeof( size_t );

            assert( real_size > size );
            assert( real_size - size >= sizeof( size_t ) );

            return real_size;
        }

        static bool is_tail( size_t size )
        {
            return ( size & ( size_t( 1 ) << ( sizeof( size_t ) * 8 - 1 ))) != 0;
        }

        static size_t make_tail( size_t size )
        {
            return size | ( size_t( 1 ) << ( sizeof( size_t ) * 8 - 1 ));
        }

    private:
        //@cond
        atomics::atomic<size_t>     front_;
        typename opt::details::apply_padding< atomics::atomic<size_t>, traits::padding >::padding_type pad1_;
        atomics::atomic<size_t>     back_;
        typename opt::details::apply_padding< atomics::atomic<size_t>, traits::padding >::padding_type pad2_;
        size_t                      pfront_;
        typename opt::details::apply_padding< size_t, traits::padding >::padding_type pad3_;
        size_t                      cback_;
        typename opt::details::apply_padding< size_t, traits::padding >::padding_type pad4_;

        buffer                      buffer_;
        //@endcond
    };

}} // namespace cds::container


#endif // #ifndef CDSLIB_CONTAINER_WEAK_RINGBUFFER_H
