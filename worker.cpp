#include "worker.hpp"
#include <cocaine/messages.hpp>
#include <cocaine/traits/unique_id.hpp>

using namespace cocaine;

namespace {
    class upstream_t:
        public api::stream_t
    {
        enum class state_t: int {
            open,
            closed
        };
    public:
        upstream_t(uint64_t id,
                   worker_t * const worker):
            m_id(id),
            m_worker(worker),
            m_state(state_t::open)
        {
            // pass
        }

        virtual
        ~upstream_t() {
            if(m_state != state_t::closed) {
                close();
            }
        }

        virtual
        void
        write(const char * chunk,
             size_t size)
        {
            if(m_state == state_t::closed) {
                throw cocaine::error_t("the stream has been closed");
            } else {
                send<io::rpc::chunk>(std::string(chunk, size));
            }
        }

        virtual
        void
        error(error_code code,
              const std::string& message)
        {
            if(m_state == state_t::closed) {
                throw cocaine::error_t("the stream has been closed");
            } else {
                m_state = state_t::closed;
                send<io::rpc::error>(static_cast<int>(code), message);
                send<io::rpc::choke>();
            }
        }

        virtual
        void
        close() {
            if(m_state == state_t::closed) {
                throw cocaine::error_t("the stream has been closed");
            } else {
                m_state = state_t::closed;
                send<io::rpc::choke>();
            }
        }

    private:
        template<class Event, typename... Args>
        void
        send(Args&&... args) {
            m_worker->send<Event>(m_id, std::forward<Args>(args)...);
        }

    private:
        const uint64_t m_id;
        worker_t * const m_worker;
        state_t m_state;
    };

    struct ignore_t {
        void
        operator()(const std::error_code& /* ec */) {
            // Empty.
        }
    };
}

worker_t::worker_t(const std::string& name,
                   const std::string& uuid):
    m_id(uuid),
    m_heartbeat_timer(m_service.loop()),
    m_disown_timer(m_service.loop()),
    m_app_name(name)
{
    auto logger = std::shared_ptr<logger::logger_t>
                      (new logger::remote_t("remote", Json::Value(), m_service));

    m_log.reset(new logger::log_t(logger, cocaine::format("worker/%s", name)));

//    auto endpoint = io::local::endpoint(format(
//        "%2%/%1%",
//        engines,
//        name
//    ));
    auto endpoint = io::local::endpoint(format(
        "/var/run/cocaine/engines/%1%",
        name
    ));

    auto socket_ = std::make_shared<io::socket<io::local>>(endpoint);

    m_channel.reset(new io::channel<io::socket<io::local>>(m_service, socket_));

    using namespace std::placeholders;

    m_channel->rd->bind(std::bind(&worker_t::on_message, this, _1), ignore_t());
    m_channel->wr->bind(ignore_t());

    // Greet the engine!
    send<io::rpc::handshake>(m_id);

    m_heartbeat_timer.set<worker_t, &worker_t::on_heartbeat>(this);
    m_heartbeat_timer.start(0.0f, 5.0f);

    m_disown_timer.set<worker_t, &worker_t::on_disown>(this);
    m_disown_timer.start(2.0f);
}

worker_t::~worker_t() {
    // pass
}

void
worker_t::run() {
    if (m_application) {
        m_service.loop().loop();
    }
}

void
worker_t::on_message(const io::message_t& message) {
    COCAINE_LOG_DEBUG(
        m_log,
        "worker %s received type %d message",
        m_id,
        message.id()
    );

    switch(message.id()) {
        case io::event_traits<io::rpc::heartbeat>::id:
            m_disown_timer.stop();

            break;

        case io::event_traits<io::rpc::invoke>::id: {
            uint64_t session_id;
            std::string event;

            message.as<io::rpc::invoke>(session_id, event);

            COCAINE_LOG_DEBUG(m_log, "worker %s invoking session %s with event '%s'", m_id, session_id, event);

            std::shared_ptr<api::stream_t> upstream(
                std::make_shared<upstream_t>(session_id, this)
            );

            try {
                io_pair_t io = {
                    upstream,
                    m_application->invoke(event, upstream)
                };

                m_streams.insert(std::make_pair(session_id, io));
            } catch(const std::exception& e) {
                upstream->error(invocation_error, e.what());
            } catch(...) {
                upstream->error(invocation_error, "unexpected exception");
            }

            break;
        }

        case io::event_traits<io::rpc::chunk>::id: {
            uint64_t session_id;
            std::string chunk;

            message.as<io::rpc::chunk>(session_id, chunk);

            stream_map_t::iterator it(m_streams.find(session_id));

            // NOTE: This may be a chunk for a failed invocation, in which case there
            // will be no active stream, so drop the message.
            if(it != m_streams.end()) {
                try {
                    it->second.downstream->write(chunk.data(), chunk.size());
                } catch(const std::exception& e) {
                    it->second.upstream->error(invocation_error, e.what());
                    m_streams.erase(it);
                } catch(...) {
                    it->second.upstream->error(invocation_error, "unexpected exception");
                    m_streams.erase(it);
                }
            }

            break;
        }

        case io::event_traits<io::rpc::choke>::id: {
            uint64_t session_id;

            message.as<io::rpc::choke>(session_id);

            stream_map_t::iterator it = m_streams.find(session_id);

            // NOTE: This may be a choke for a failed invocation, in which case there
            // will be no active stream, so drop the message.
            if(it != m_streams.end()) {
                try {
                    it->second.downstream->close();
                } catch(const std::exception& e) {
                    it->second.upstream->error(invocation_error, e.what());
                } catch(...) {
                    it->second.upstream->error(invocation_error, "unexpected exception");
                }

                m_streams.erase(it);
            }

            break;
        }

        case io::event_traits<io::rpc::terminate>::id:
            terminate(io::rpc::terminate::normal, "per request");
            break;

        default: ;
            COCAINE_LOG_WARNING(
                m_log,
                "worker %s dropping unknown type %d message",
                m_id,
                message.id()
            );
    }
}

void
worker_t::on_heartbeat(ev::timer&, int) {
    send<io::rpc::heartbeat>();
    m_disown_timer.start(2.0f);
}

void
worker_t::on_disown(ev::timer&, int) {
    COCAINE_LOG_ERROR(
        m_log,
        "worker %s has lost the controlling engine",
        m_id
    );

    m_service.loop().unloop(ev::ALL);
}

void
worker_t::terminate(int reason,
                    const std::string& message)
{
    send<io::rpc::terminate>(reason, message);
    m_service.loop().unloop(ev::ALL);
}


std::shared_ptr<base_handler_t>
application_t::invoke(const std::string& event,
                      std::shared_ptr<cocaine::api::stream_t> response)
{
    auto it = m_handlers.find(event);

    if (it != m_handlers.end()) {
        std::shared_ptr<base_handler_t> new_handler = it->second->make_handler();
        new_handler->invoke(event, response);
        return new_handler;
    } else if (m_default_handler) {
        std::shared_ptr<base_handler_t> new_handler = m_default_handler->make_handler();
        new_handler->invoke(event, response);
        return new_handler;
    } else {
        throw std::exception();
    }
}

void
application_t::on(const std::string& event,
                  std::shared_ptr<base_factory_t> factory)
{
    m_handlers[event] = factory;
}

void
application_t::on_unregistered(std::shared_ptr<base_factory_t> factory) {
    m_default_handler = factory;
}

void
application_t::initialize(const std::string& name,
                          std::shared_ptr<logger::logger_t> logger)
{
    m_name = name;
    m_log.reset(new logger::log_t(logger, cocaine::format("app/%s", name)));
}
