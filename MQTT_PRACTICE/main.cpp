#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/version.hpp>
#include <boost/asio/yield.hpp>
#include <async_mqtt/all.hpp>
#include <openssl/core.h>
#include <openssl/opensslv.h>

#define		BOOST_VER_REQ		"1_84"
#define		CPP_VER_REQ			202002L

namespace as = boost::asio;
namespace am = async_mqtt;

bool check_version();

template <typename Executor>
struct app {
    app(Executor exe,
        std::string_view host,
        std::string_view port
    ) :host_{ std::move(host) },
        port_{ std::move(port) },
        amep_{ am::protocol_version::v3_1_1, exe }
    {
        impl_();
    }

private:
    struct impl {
        impl(app& a) :app_{ a }
        {
        }
        // forwarding callbacks
        void operator()() const {
            proc({}, am::packet_variant{});
        }
        void operator()(am::error_code const& ec) const {
            proc(ec, am::packet_variant{});
        }
        void operator()(am::error_code const& ec, am::packet_variant pv) const {
            proc(ec, am::force_move(pv));
        }
    private:
        void proc(
            am::error_code const& ec,
            am::packet_variant pv
        ) const {

            reenter(coro_) {
                std::cout << "start" << std::endl;

                // Handshake undlerying layer (Name resolution and TCP handshaking)
                yield am::async_underlying_handshake(
                    app_.amep_.next_layer(),
                    app_.host_,
                    app_.port_,
                    *this
                );

                std::cout
                    << "Underlying layer connected ec:"
                    << ec.message()
                    << std::endl;

                if (ec) return;

                // Send MQTT CONNECT
                yield app_.amep_.async_send(
                    am::v3_1_1::connect_packet{
                        true,   // clean_session
                        0x1234, // keep_alive
                        "ClientIdentifier1",
                        app_.will,   // you can pass std::nullopt if you don't want to set the will message
                        "UserName1",
                        "Password1"
                    },
                    *this
                );
                if (ec) {
                    std::cout << "MQTT CONNECT send error:" << ec.message() << std::endl;
                    return;
                }

                // Recv MQTT CONNACK
                yield app_.amep_.async_recv(*this);
                if (ec) {
                    std::cout << "MQTT CONNACK recv error:" << ec.message() << std::endl;
                    return;
                }
                BOOST_ASSERT(pv);
                pv.visit(
                    am::overload{
                        [&](am::v3_1_1::connack_packet const& p) {
                            std::cout
                                << "MQTT CONNACK recv"
                                << " sp:" << p.session_present()
                                << std::endl;
                        },
                        [](auto const&) {}
                    }
                );

                // Send MQTT SUBSCRIBE
                yield app_.amep_.async_send(
                    am::v3_1_1::subscribe_packet{
                        *app_.amep_.acquire_unique_packet_id(), // sync version only works thread safe context
                        { {"topic1", am::qos::at_most_once} }
                    },
                    *this
                );
                if (ec) {
                    std::cout << "MQTT SUBSCRIBE send error:" << ec.message() << std::endl;
                    return;
                }
                // Recv MQTT SUBACK
                yield app_.amep_.async_recv(*this);
                if (ec) {
                    std::cout << "MQTT SUBACK recv error:" << ec.message() << std::endl;
                    return;
                }
                BOOST_ASSERT(pv);
                pv.visit(
                    am::overload{
                        [&](am::v3_1_1::suback_packet const& p) {
                            std::cout
                                << "MQTT SUBACK recv"
                                << " pid:" << p.packet_id()
                                << " entries:";
                            for (auto const& e : p.entries()) {
                                std::cout << e << " ";
                            }
                            std::cout << std::endl;
                        },
                        [](auto const&) {}
                    }
                );

                // Send MQTT PUBLISH
                yield app_.amep_.async_send(
                    am::v3_1_1::publish_packet{
                        *app_.amep_.acquire_unique_packet_id(), // sync version only works thread safe context
                        "topic2424",
                        "hello",
                        am::qos::at_least_once
                    },
                    *this
                );
                if (ec) {
                    std::cout << "MQTT PUBLISH send error:" << ec.message() << std::endl;
                    return;
                }
                // Recv MQTT PUBLISH and PUBACK (order depends on broker)
                for (app_.count_ = 0; app_.count_ != 2; ++app_.count_) {
                    yield app_.amep_.async_recv(*this);
                    if (ec) {
                        std::cout << "MQTT recv error:" << ec.message() << std::endl;
                        return;
                    }
                    BOOST_ASSERT(pv);
                    pv.visit(
                        am::overload{
                            [&](am::v3_1_1::publish_packet const& p) {
                                std::cout
                                    << "MQTT PUBLISH recv"
                                    << " pid:" << p.packet_id()
                                    << " topic:" << p.topic()
                                    << " payload:" << p.payload()
                                    << " qos:" << p.opts().get_qos()
                                    << " retain:" << p.opts().get_retain()
                                    << " dup:" << p.opts().get_dup()
                                    << std::endl;
                            },
                            [&](am::v3_1_1::puback_packet const& p) {
                                std::cout
                                    << "MQTT PUBACK recv"
                                    << " pid:" << p.packet_id()
                                    << std::endl;
                            },
                            [](auto const&) {}
                        }
                    );
                }
                std::cout << "close" << std::endl;
                yield app_.amep_.async_close(*this);
            }
        }

    private:
        app& app_;
        mutable as::coroutine coro_;
    };

    std::string_view host_;
    std::string_view port_;
    am::endpoint<am::role::client, am::protocol::mqtt> amep_;
    std::size_t count_ = 0;
    impl impl_{ *this };
    // prepare will message if you need.
    am::will will{
        "WillTopic1",
        "WillMessage1",
        am::qos::at_most_once,
        { // properties
            am::property::user_property {"key1", "val1"},
            am::property::content_type {"text"},
        }
    };
};

//////////////////////////////SUB EXAMPLE///////////////////////////////////
// Use as::use_awaitable as the default completion token for am::client
using awaitable_client =
boost::asio::use_awaitable_t<>::as_default_on_t<
    am::client<am::protocol_version::v5, am::protocol::mqtt>
>;

as::awaitable<void>
proc(
    awaitable_client& amcl,
    std::string_view host,
    std::string_view port) {

    auto exe = co_await as::this_coro::executor;

    std::cout << "SUB start" << std::endl;

    while (true) {
        try {
            // all underlying layer handshaking
            // (Resolve hostname, TCP handshake)
            co_await am::async_underlying_handshake(amcl.next_layer(), host, port);
            std::cout << "SUB mqtt undlerlying handshaked" << std::endl;

            // MQTT connect and receive loop start
            auto connack_opt = co_await amcl.async_start(
                true,                // clean_start
                std::uint16_t(0),    // keep_alive
                "",                  // Client Identifier, empty means generated by the broker
                std::nullopt,        // will
                "UserName1",
                "Password1"
            );
            if (connack_opt) {
                std::cout << "SUB - " << *connack_opt << std::endl;
            }

            // subscribe
            // MQTT send subscribe and wait suback
            std::vector<am::topic_subopts> sub_entry{
                {"topic2424", am::qos::at_most_once},
                {"topic2", am::qos::at_least_once},
                {"topic3", am::qos::exactly_once},
            };
            auto suback_opt = co_await amcl.async_subscribe(
                *amcl.acquire_unique_packet_id(), // sync version only works thread safe context
                am::force_move(sub_entry)
            );
            if (suback_opt) {
                std::cout << "SUB - " << *suback_opt << std::endl;
            }

            // recv (coroutine)
            while (true) {
                auto pv = co_await amcl.async_recv(as::use_awaitable);
                pv.visit(
                    am::overload{
                        [&](awaitable_client::publish_packet& p) {
                            std::cout << "(SUB ";
                            std::cout << p << std::endl;
                            std::cout << "topic   : " << p.topic() << std::endl;
                            std::cout << "payload : " << p.payload() << std::endl;
                            std::cout << " SUB)";
                        },
                        [&](awaitable_client::disconnect_packet& p) {
                            std::cout << p << std::endl;
                        },
                        [](auto&) {
                        }
                    }
                );
            }
        }
        catch (boost::system::system_error const& se) {
            std::cout << se.code().message() << std::endl;
        }

        // wait and reconnect
        as::steady_timer tim{ exe, std::chrono::seconds{1} };
        auto [ec] = co_await tim.async_wait(as::as_tuple(as::use_awaitable));
        if (ec) {
            co_return;
        }
    }
}


int main() {
	
	if (!check_version()) {
		std::cerr << "Version ERROR\n";
		return -1;
	}
    std::string_view host("async-mqtt.redboltz.net");
    std::string_view port("1883");

    async_mqtt::setup_log(async_mqtt::severity_level::trace);
    as::io_context ioc;

    auto amcl = awaitable_client{ ioc.get_executor() };
    as::co_spawn(amcl.get_executor(), proc(amcl, host, port), as::detached);

    app a{ ioc.get_executor(), host, port };
    ioc.run();


}

bool check_version() {
	bool result = true;
	std::cout << "boost version : " << BOOST_LIB_VERSION << std::endl;
	//»çÀü¼ø
	if (BOOST_LIB_VERSION > BOOST_VER_REQ) {
		std::cerr << "need boost version 1.84.0 or higher\n";
		result = false;
	}
	std::cout << "C++ version : " << __cplusplus << std::endl;
	if (__cplusplus < CPP_VER_REQ) {
		std::cerr << "need C++ version 17 or higher\n";
		result = false;
	}
	std::cout << "OpenSSL Version : " << OPENSSL_VERSION_TEXT << std::endl;
    std::cout << "--------------------------------\n\n";
	return result;
}

