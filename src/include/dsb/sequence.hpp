/**
\file
\brief Main header file for dsb::sequence.
*/
#ifndef DSB_SEQUENCE_HPP
#define DSB_SEQUENCE_HPP

#include <cassert>
#include <iterator>
#include <memory>
#include <type_traits>

#include "dsb/config.h"


namespace dsb
{

/**
\brief  Module that defines a sequence iteration abstraction, similar to
        standard iterators, but simpler.

The central class in this module is dsb::sequence::Sequence.  Please see
its documentation for more information.
*/
namespace sequence
{


/**
\brief  An interface for sequence implementations.

To create a new sequence type (e.g. to support a new kind of backing storage),
create a class which inherits and implements this interface, and pass an
object of that class to the Sequence constructor.

`ElementT` is the type of the elements in the sequence, i.e., the return type
of Next().

\see Sequence
*/
template<typename ElementT>
class ISequenceImpl
{
public:
    /// Returns whether we have reached the end of the sequence.
    virtual bool Empty() = 0;

    /**
    \brief  Returns the element currently at the front of the sequence
            and iterates one step past it.

    It is OK to assume that Empty() has, or would have, returned `false`
    before this function is called (but good practice to verify it with
    an assertion).
    */
    virtual ElementT Next() = 0;

    // Allows deletions through a base class pointer.
    virtual ~ISequenceImpl() { }
};


/**
\brief  A class that is used to iterate over a sequence of elements of
        type `ElementT`.

This class defines two functions, Empty() and Next().  The former returns
whether we have reached the end of the sequence, and the latter returns
the element at the head of the range and simultaneously iterates past it.
The idiomatic way to iterate a sequence is thus:
~~~~{.cpp}
while (!sequence.Empty()) {
    auto element = seq.Next();
    // Do stuff with element here
}
~~~~

Sequence objects fulfil the main purpose of standard iterators, but
are different from them in several ways, some of which are advantages
and others of which are drawbacks, depending on the situation:

  - A Sequence currently only allows one-way iteration.  (In that respect
    it is similar to an [input iterator](http://www.cplusplus.com/reference/iterator/InputIterator/).)

  - A Sequence is actually more like a *pair* of iterators: the beginning
    and the end of the sequence.  This makes it easier to use, because it
    is only necessary to keep track of one object instead of two.  If `b`
    is the "begin" iterator and `e` is the "end" iterator, then Empty() is
    equivalent to `b==e`, while Next() is equivalent to `*b++`.

  - Iterators are typically value types, while Sequence has reference
    semantics.  That is, if `a` is a sequence, and we set `b = a`, then
    calling either `a.Next()` or `b.Next()` will iterate both sequences
    one step.

  - The type of a Sequence only depends on the value of its elements, and
    not on the underlying container.  That is, an object of type
    `Sequence<int>` may be used to iterate over the contents of a
    `std::vector<int>`, an `std::list<int>`, or even a plain array of
    `int`s.

  - A Sequence typically has worse performance than an iterator.  This is
    related to the previous point.  Polymorphic classes are used "under the
    hood" to hide the type of the underlying container, which means that
    calls to Empty() and Next() are indirect (virtual), and that memory for
    the underlying objects has to be dynamically allocated.

The Sequence class is just a thin wrapper around a `std::shared_ptr` to
an object of type ISequenceImpl, which contains the actual implementation
of the sequence.  (This is how the type of the underlying container gets
hidden.)  To create a new sequence type it is therefore necessary to
create a class that implements the ISequenceImpl interface.

Like iterators, a Sequence object may be invalidated by changes to the
underlying storage.  The circumstances under which this does or doesn't
happen are defined by the specific ISequenceImpl implementation used.

Furthermore, if `ElementT` is a reference type (e.g. `int&`), it means that
Next() returns a reference to the value in some underlying storage.  In that
case, the lifetime of the reference is defined by the specific sequence
implementation.
*/
template<typename ElementT>
class Sequence
{
public:
    /// Constructs a new sequence with the given implementation.
    Sequence(std::shared_ptr<ISequenceImpl<ElementT>> impl
                = std::shared_ptr<ISequenceImpl<ElementT>>())
        : m_impl(impl)
    { }

    typedef ElementT ElementType;

    /// Returns whether we have reached the end of the sequence.
    bool Empty() { return !m_impl.get() || m_impl->Empty(); }

    /**
    \brief  Returns the element currently at the front of the sequence
            and iterates one step past it.

    Calling this function is only allowed if Empty() has, or would have,
    returned `false`.
    */
    ElementT Next() { return m_impl->Next(); }

private:
    std::shared_ptr<ISequenceImpl<ElementT>> m_impl;
};


// Implementation class for IteratorSequence()
template<typename Iterator>
class IteratorSequenceImpl
    : public ISequenceImpl<typename std::iterator_traits<Iterator>::reference>
{
public:
    IteratorSequenceImpl(Iterator begin, Iterator end) : m_begin(begin), m_end(end) { }

    bool Empty() DSB_FINAL override { return m_begin == m_end; }

    typename std::iterator_traits<Iterator>::reference Next() DSB_FINAL override
    {
        return *(m_begin++);
    }

private:
    Iterator m_begin;
    Iterator m_end;
};


/**
\brief  Returns a sequence that wraps a pair of standard iterators.

The sequence is valid as long as the iterators are valid (which again depends
on the type of container iterated).

\see ArraySequence, ContainerSequence
*/
template<typename Iterator>
Sequence<typename std::iterator_traits<Iterator>::reference> IteratorSequence(
    Iterator begin, Iterator end)
{
    return Sequence<typename std::iterator_traits<Iterator>::reference>(
        std::make_shared<IteratorSequenceImpl<Iterator>>(begin, end));
}


/**
\brief  Convenience function which returns a Sequence that iterates over the
        entire contents of a standard container.

This function simply forwards to IteratorSequence(), using the "begin" and "end"
iterators of `c`.
*/
template<typename Container>
Sequence<typename std::iterator_traits<typename Container::iterator>::reference>
    ContainerSequence(Container& c)
{
    return IteratorSequence(c.begin(), c.end());
}

// Same as the above, but for const containers.
template<typename Container>
Sequence<typename std::iterator_traits<typename Container::const_iterator>::reference>
    ContainerSequence(const Container& c)
{
    return IteratorSequence(c.cbegin(), c.cend());
}


/**
\brief  Convenience function which returns a Sequence that iterates over the
        entire contents of an array.

This function simply forwards to IteratorSequence(), using `pointer` and
`pointer+length` as iterators.
*/
template<typename ElementT>
Sequence<ElementT&> ArraySequence(ElementT* pointer, size_t length)
{
    return IteratorSequence(pointer, pointer + length);
}


// Implementation class for MapValueSequence()
template<typename Map>
class MapValueSequenceImpl : public ISequenceImpl<typename Map::mapped_type&>
{
public:
    /// Constructor that takes a map object.
    MapValueSequenceImpl(Map& map)
        : m_begin(map.begin()), m_end(map.end())
    { }

    bool Empty() DSB_FINAL override
    {
        return m_begin == m_end;
    }

    typename Map::mapped_type& Next() DSB_FINAL override
    {
        return (m_begin++)->second;
    }

private:
    typename Map::iterator m_begin;
    typename Map::iterator m_end;
};


/**
\brief  Returns a sequence representation of the mapped values in a `std::map`
        or `std::unordered_map` (or any other type with a compatible API).

The sequence stores and uses a pair of map iterators (i.e., those returned
by `map.begin()` and `map.end()`), and thus remains valid under the same
circumstances as the iterators.

Example:
~~~{.cpp}
std::map<int, double> m;
Sequence<double> s = MapValueSequence(m);
~~~
*/
template<typename Map>
Sequence<typename Map::mapped_type&> MapValueSequence(Map& map)
{
    return Sequence<typename Map::mapped_type&>(
        std::make_shared<MapValueSequenceImpl<Map>>(map));
}


// Implementation class for EmptySequence()
template<typename ElementT>
class EmptySequenceImpl : public ISequenceImpl<ElementT>
{
public:
    bool Empty() DSB_FINAL override { return true; }

    ElementT Next() DSB_FINAL override
    {
        assert(!"Next() called on empty sequence");
        return *(static_cast<typename std::remove_reference<ElementT>::type*>(nullptr));
    }
};


/// Returns an empty sequence, i.e. one for which Empty() is always `true`.
template<typename ElementT>
Sequence<ElementT> EmptySequence()
{
    return Sequence<ElementT>(std::make_shared<EmptySequenceImpl<ElementT>>());
}


// Implementation class for ConstSequence().
template<typename DerefElementT>
class ConstSequenceImpl : public ISequenceImpl<const DerefElementT&>
{
public:
    ConstSequenceImpl(Sequence<DerefElementT&> wrapThis) : m_wrapped(wrapThis) { }
    bool Empty() DSB_FINAL override { return m_wrapped.Empty(); }
    const DerefElementT& Next() DSB_FINAL override { return m_wrapped.Next(); }
private:
    Sequence<DerefElementT&> m_wrapped;
};


/**
\brief  Returns a sequence which provides a read-only view of the elements in
        another sequence.

This function is only defined for reference sequences.
*/
template<typename DerefElementT>
Sequence<const DerefElementT&> ConstSequence(Sequence<DerefElementT&> sequence)
{
    return Sequence<const DerefElementT&>(
        std::make_shared<ConstSequenceImpl<DerefElementT>>(sequence));
}


}}      // namespace
#endif  // header guard
