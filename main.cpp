//
// Copyright (c) 2021 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/beast-issues/2340
//

#include <boost/asio.hpp>
#include <boost/asio/experimental/append.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include <iostream>

// type aliases
namespace asio = boost::asio;
using boost::system::error_code;

template < class Executor, class BoolHandler >
struct my_async_thing_op
{
    using executor_type = Executor;

    using timer_type = asio::steady_timer;

    Executor
    get_executor() const
    {
        return wg_.get_executor();
    }

    my_async_thing_op(timer_type &timer, Executor e, BoolHandler handler)
    : timer_(timer)
    , wg_(e)
    , handler_(std::move(handler))
    {
    }

    void operator()(error_code ec = {})
    {
        switch (state_)
        {
        case init:
            state_ = waiting;
            return timer_.async_wait(std::move(*this));
        case waiting:
            if (ec)
            {
                if (ec == asio::error::operation_aborted)
                {
                    // timer was cancelled, return false with no error
                    return complete(error_code(), false);
                }
                else
                {
                    // other error, return false with errpr
                    return complete(ec, false);
                }
            }
            else
            {
                // operation complete. return true with no error
                return complete(ec, true);
            }
        }
    }

  private:
    void
    complete(error_code ec, bool result)
    {
        auto final =
            asio::experimental::append(std::move(handler_), ec, result);
        asio::post(wg_.get_executor(), std::move(final));
    }

  private:
    asio::steady_timer                                    &timer_;
    asio::executor_work_guard< timer_type::executor_type > timer_wg_ {
        timer_.get_executor()
    };
    asio::executor_work_guard< Executor > wg_;
    BoolHandler                           handler_;
    enum
    {
        init,
        waiting
    } state_ = init;
};

template < BOOST_ASIO_COMPLETION_TOKEN_FOR(void(error_code, bool)) BoolHandler >
BOOST_ASIO_INITFN_RESULT_TYPE(BoolHandler, void(error_code, bool))
my_async_thing(asio::steady_timer &timer, BoolHandler &&token)
{
    auto init = [&timer]< class Handler >(Handler &&handler) mutable
    {
        auto exec =
            asio::get_associated_executor(handler, timer.get_executor());
        using exec_type = decltype(exec);

        auto op = my_async_thing_op< exec_type, std::decay_t< Handler > >(
            timer, exec, std::forward< Handler >(handler));
        op();
    };

    return asio::async_initiate< BoolHandler, void(error_code, bool) >(init,
                                                                       token);
}

asio::awaitable< void >
run_test()
try
{
    using namespace std::literals;
    using asio::experimental::as_tuple;

    auto exec = co_await asio::this_coro::executor;

    auto timer = asio::steady_timer(exec, 1s);
    auto [ec, ok] =
        co_await my_async_thing(timer, as_tuple(asio::use_awaitable));

    std::cout << "ec = " << ec.message() << "\nok = " << std::boolalpha << ok
              << '\n';
}
catch (std::exception &e)
{
    std::cout << __func__ << ": exception: " << e.what() << "\n";
    throw;
}

int
main()
{
    auto ioc = asio::io_context();

    asio::co_spawn(ioc, run_test(), asio::detached);

    ioc.run();
}