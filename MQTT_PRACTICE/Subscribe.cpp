#include "Subscribe.h"

//Subscribe Subscribe::init()
//{
//    std::string_view host(HOST);
//    std::string_view port(PORT);
//    using client_t = am::client < am::protocol_version::v3_1_1, am::protocol::mqtt>;
//    am::as::io_context ioc;
//    auto amcl = client_t{
//        ioc.get_executor() // args for underlying layer (mqtt)
//        // mqtt is as::basic_stream_socket<as::ip::tcp, as::io_context::executor_type>
//    };
//    // handshake underlying layers
//    co_await am::async_underlying_handshake(amcl.next_layer(), host, port, am::as::use_awaitable);
//}
