// Copyright (c) Microsoft Open Technologies, Inc. All rights reserved. See License.txt in the project root for license information.

#pragma once

#if !defined(RXCPP_RX_COORDINATION_HPP)
#define RXCPP_RX_COORDINATION_HPP

#include "rx-includes.hpp"

namespace rxcpp {

struct tag_coordinator {};
struct coordinator_base {typedef tag_coordinator coordinator_tag;};
template<class T>
class is_coordinator
{
    template<class C>
    static typename C::coordinator_tag* check(int);
    template<class C>
    static void check(...);
public:
    static const bool value = std::is_convertible<decltype(check<typename std::decay<T>::type>(0)), tag_coordinator*>::value;
};

struct tag_coordination {};
struct coordination_base {typedef tag_coordination coordination_tag;};
template<class T>
class is_coordination
{
    template<class C>
    static typename C::coordination_tag* check(int);
    template<class C>
    static void check(...);
public:
    static const bool value = std::is_convertible<decltype(check<typename std::decay<T>::type>(0)), tag_coordination*>::value;
};

template<class Input>
class coordinator : public coordinator_base
{
public:
    typedef Input input_type;

private:
    struct not_supported {typedef not_supported type;};

    template<class Observable>
    struct get_observable
    {
        typedef decltype((*(input_type*)nullptr).in((*(Observable*)nullptr))) type;
    };

    template<class Subscriber>
    struct get_subscriber
    {
        typedef decltype((*(input_type*)nullptr).out((*(Subscriber*)nullptr))) type;
    };

    template<class F>
    struct get_action_function
    {
        typedef decltype((*(input_type*)nullptr).act((*(F*)nullptr))) type;
    };

public:
    input_type input;

    template<class T>
    struct get
    {
        typedef typename std::conditional<
            rxsc::detail::is_action_function<T>::value, get_action_function<T>, typename std::conditional<
            is_observable<T>::value, get_observable<T>, typename std::conditional<
            is_subscriber<T>::value, get_subscriber<T>, not_supported>::type>::type>::type::type type;
    };

    coordinator(Input i) : input(i) {}

    rxsc::worker get_worker() const {
        return input.get_worker();
    }
    rxsc::scheduler get_scheduler() const {
        return input.get_scheduler();
    }

    template<class Observable>
    auto in(Observable o) const
        -> typename get_observable<Observable>::type {
        return input.in(std::move(o));
        static_assert(is_observable<Observable>::value, "can only synchronize observables");
    }

    template<class Subscriber>
    auto out(Subscriber s) const
        -> typename get_subscriber<Subscriber>::type {
        return input.out(std::move(s));
        static_assert(is_subscriber<Subscriber>::value, "can only synchronize subscribers");
    }

    template<class F>
    auto act(F f) const
        -> typename get_action_function<F>::type {
        return input.act(std::move(f));
        static_assert(rxsc::detail::is_action_function<F>::value, "can only synchronize action functions");
    }
};

class identity_one_worker : public coordination_base
{
    rxsc::scheduler factory;

    class input_type
    {
        rxsc::worker controller;
        rxsc::scheduler factory;
    public:
        explicit input_type(rxsc::worker w)
            : controller(w)
            , factory(rxsc::make_same_worker(w))
        {
        }
        rxsc::worker get_worker() const {
            return controller;
        }
        rxsc::scheduler get_scheduler() const {
            return factory;
        }
        template<class Observable>
        auto in(Observable o) const
            -> Observable {
            return std::move(o);
        }
        template<class Subscriber>
        auto out(Subscriber s) const
            -> Subscriber {
            return std::move(s);
        }
        template<class F>
        auto act(F f) const
            -> F {
            return std::move(f);
        }
    };

public:

    explicit identity_one_worker(rxsc::scheduler sc) : factory(sc) {}

    typedef coordinator<input_type> coordinator_type;

    coordinator_type create_coordinator(composite_subscription cs = composite_subscription()) const {
        auto w = factory.create_worker(std::move(cs));
        return coordinator_type(input_type(std::move(w)));
    }
};

class serialize_one_worker : public coordination_base
{
    rxsc::scheduler factory;

    template<class F>
    struct serialize_action
    {
        F dest;
        std::shared_ptr<std::mutex> lock;
        serialize_action(F d, std::shared_ptr<std::mutex> m)
            : dest(std::move(d))
            , lock(std::move(m))
        {
            if (!lock) {
                abort();
            }
        }
        auto operator()(const rxsc::schedulable& scbl) const
            -> decltype(dest(scbl)) {
            std::unique_lock<std::mutex> guard(*lock);
            return dest(scbl);
        }
    };

    template<class Subscriber>
    struct serialize_observer : public observer_base<typename Subscriber::value_type>
    {
        typedef serialize_observer<Subscriber> this_type;
        typedef observer_base<typename Subscriber::value_type> base_type;
        typedef typename base_type::value_type value_type;
        typedef typename std::decay<Subscriber>::type dest_type;
        typedef observer<value_type, this_type> observer_type;
        dest_type dest;
        std::shared_ptr<std::mutex> lock;

        serialize_observer(dest_type d, std::shared_ptr<std::mutex> m)
            : dest(std::move(d))
            , lock(std::move(m))
        {
            if (!lock) {
                abort();
            }
        }
        void on_next(value_type v) const {
            std::unique_lock<std::mutex> guard(*lock);
            dest.on_next(v);
        }
        void on_error(std::exception_ptr e) const {
            std::unique_lock<std::mutex> guard(*lock);
            dest.on_error(e);
        }
        void on_completed() const {
            std::unique_lock<std::mutex> guard(*lock);
            dest.on_completed();
        }

        static subscriber<value_type, this_type> make(dest_type d, std::shared_ptr<std::mutex> m) {
            return make_subscriber<value_type>(d, this_type(d, std::move(m)));
        }
    };

    class input_type
    {
        rxsc::worker controller;
        rxsc::scheduler factory;
        std::shared_ptr<std::mutex> lock;
    public:
        explicit input_type(rxsc::worker w, std::shared_ptr<std::mutex> m)
            : controller(w)
            , factory(rxsc::make_same_worker(w))
            , lock(std::move(m))
        {
        }
        rxsc::worker get_worker() const {
            return controller;
        }
        rxsc::scheduler get_scheduler() const {
            return factory;
        }
        template<class Observable>
        auto in(Observable o) const
            -> Observable {
            return std::move(o);
        }
        template<class Subscriber>
        auto out(Subscriber s) const
            ->      serialize_observer<Subscriber> {
            return  serialize_observer<Subscriber>(std::move(s), lock);
        }
        template<class F>
        auto act(F f) const
            ->      serialize_action<F> {
            return  serialize_action<F>(std::move(f), lock);
        }
    };

public:

    explicit serialize_one_worker(rxsc::scheduler sc) : factory(sc) {}

    typedef coordinator<input_type> coordinator_type;

    coordinator_type create_coordinator(composite_subscription cs = composite_subscription()) const {
        auto w = factory.create_worker(std::move(cs));
        std::shared_ptr<std::mutex> lock = std::make_shared<std::mutex>();
        return coordinator_type(input_type(std::move(w), std::move(lock)));
    }
};

}

#endif