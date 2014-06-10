// Copyright (c) Microsoft Open Technologies, Inc. All rights reserved. See License.txt in the project root for license information.

#pragma once

#if !defined(RXCPP_SOURCES_RX_ITERATE_HPP)
#define RXCPP_SOURCES_RX_ITERATE_HPP

#include "../rx-includes.hpp"

namespace rxcpp {

namespace sources {

namespace detail {

template<class Collection>
struct is_iterable
{
    typedef typename std::decay<Collection>::type collection_type;

    struct not_void {};
    template<class CC>
    static auto check(int) -> decltype(std::begin(*(CC*)nullptr));
    template<class CC>
    static not_void check(...);

    static const bool value = !std::is_same<decltype(check<collection_type>(0)), not_void>::value;
};

template<class Collection>
struct iterate_traits
{
    typedef typename std::decay<Collection>::type collection_type;
    typedef decltype(std::begin(*(collection_type*)nullptr)) iterator_type;
    typedef typename std::iterator_traits<iterator_type>::value_type value_type;
};

template<class Collection, class Coordination>
struct iterate : public source_base<typename iterate_traits<Collection>::value_type>
{
    typedef iterate<Collection, Coordination> this_type;
    typedef iterate_traits<Collection> traits;

    typedef typename std::decay<Coordination>::type coordination_type;
    typedef typename coordination_type::coordinator_type coordinator_type;

    typedef typename traits::collection_type collection_type;
    typedef typename traits::iterator_type iterator_type;

    struct iterate_initial_type
    {
        iterate_initial_type(collection_type c, coordination_type cn)
            : collection(std::move(c))
            , coordination(std::move(cn))
        {
        }
        collection_type collection;
        coordination_type coordination;
    };
    iterate_initial_type initial;

    iterate(collection_type c, coordination_type cn)
        : initial(std::move(c), std::move(cn))
    {
    }
    template<class Subscriber>
    void on_subscribe(Subscriber o) const {
        static_assert(is_subscriber<Subscriber>::value, "subscribe must be passed a subscriber");

        typedef typename coordinator_type::template get<Subscriber>::type output_type;

        struct iterate_state_type
            : public iterate_initial_type
        {
            iterate_state_type(const iterate_initial_type& i, output_type o)
                : iterate_initial_type(i)
                , cursor(std::begin(iterate_initial_type::collection))
                , end(std::end(iterate_initial_type::collection))
                , out(std::move(o))
            {
            }
            iterate_state_type(const iterate_state_type& o)
                : iterate_initial_type(o)
                , cursor(std::begin(iterate_initial_type::collection))
                , end(std::end(iterate_initial_type::collection))
                , out(std::move(o.out)) // since lambda capture does not yet support move
            {
            }
            mutable iterator_type cursor;
            iterator_type end;
            mutable output_type out;
        };

        // creates a worker whose lifetime is the same as this subscription
        auto coordinator = initial.coordination.create_coordinator(o.get_subscription());
        auto selectedDest = on_exception(
            [&](){return coordinator.out(o);},
            o);
        if (selectedDest.empty()) {
            return;
        }

        iterate_state_type state(initial, std::move(selectedDest.get()));

        auto controller = coordinator.get_output().get_worker();

        controller.schedule(
            [state](const rxsc::schedulable& self){
                if (!state.out.is_subscribed()) {
                    // terminate loop
                    return;
                }

                if (state.cursor != state.end) {
                    // send next value
                    state.out.on_next(*state.cursor);
                    ++state.cursor;
                }

                if (state.cursor == state.end) {
                    state.out.on_completed();
                    // o is unsubscribed
                    return;
                }

                // tail recurse this same action to continue loop
                self();
            });
    }
};

}

template<class Collection>
auto iterate(Collection c)
    ->      observable<typename detail::iterate_traits<Collection>::value_type, detail::iterate<Collection, identity_one_worker>> {
    return  observable<typename detail::iterate_traits<Collection>::value_type, detail::iterate<Collection, identity_one_worker>>(
                                                                                detail::iterate<Collection, identity_one_worker>(std::move(c), identity_one_worker(rxsc::make_current_thread())));
}
template<class Collection, class Coordination>
auto iterate(Collection c, Coordination cn)
    ->      observable<typename detail::iterate_traits<Collection>::value_type, detail::iterate<Collection, Coordination>> {
    return  observable<typename detail::iterate_traits<Collection>::value_type, detail::iterate<Collection, Coordination>>(
                                                                                detail::iterate<Collection, Coordination>(std::move(c), std::move(cn)));
}

template<class T>
auto from()
    -> decltype(iterate(std::array<T, 0>(), identity_one_worker(rxsc::make_immediate()))) {
    return      iterate(std::array<T, 0>(), identity_one_worker(rxsc::make_immediate()));
}
template<class T, class Coordination>
auto from(Coordination cn)
    -> typename std::enable_if<is_coordination<Coordination>::value,
        decltype(   iterate(std::array<T, 0>(), std::move(cn)))>::type {
    return          iterate(std::array<T, 0>(), std::move(cn));
}
template<class Value0, class... ValueN>
auto from(Value0 v0, ValueN... vn)
    -> typename std::enable_if<!is_coordination<Value0>::value,
        decltype(iterate(std::array<Value0, sizeof...(ValueN) + 1>(), identity_one_worker(rxsc::make_immediate())))>::type {
    std::array<Value0, sizeof...(ValueN) + 1> c = {v0, vn...};
    return iterate(std::move(c), identity_one_worker(rxsc::make_immediate()));
}
template<class Coordination, class Value0, class... ValueN>
auto from(Coordination cn, Value0 v0, ValueN... vn)
    -> typename std::enable_if<is_coordination<Coordination>::value,
        decltype(iterate(std::array<Value0, sizeof...(ValueN) + 1>(), std::move(cn)))>::type {
    std::array<Value0, sizeof...(ValueN) + 1> c = {v0, vn...};
    return iterate(std::move(c), std::move(cn));
}

}

}

#endif
