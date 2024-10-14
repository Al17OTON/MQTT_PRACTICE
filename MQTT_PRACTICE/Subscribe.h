#pragma once
#include <async_mqtt/all.hpp>
#include <coroutine>

#define     PORT        "1883"
#define     HOST        "async-mqtt.redboltz.net"

namespace am = async_mqtt;

//test용 서버가 제공된다. 
//https://redboltz.github.io/async_mqtt/doc/9.0.2/tool/trial.html
class Subscribe
{
public:
	struct promise_type {
        Subscribe get_return_object()
        {
            return  Subscribe{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        std::suspend_always initial_suspend()
        {
            return std::suspend_always{};
        }

        std::suspend_always final_suspend() noexcept
        {
            return std::suspend_always{};
        }

        void unhandled_exception() {}
	};
    Subscribe(std::coroutine_handle<promise_type> _Handler) : Handler(_Handler) {}

    ~Subscribe()
    {
        if ((bool)Handler == true)
        {
            Handler.destroy();
        }
    }

    const std::coroutine_handle<promise_type> GetHandler()
    {
        return Handler;
    }

    Subscribe init();

private:
	std::coroutine_handle<promise_type> Handler;
};

