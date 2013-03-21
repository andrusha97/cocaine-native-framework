#ifndef COCAINE_GRAPE_WORKER
#define COCAINE_GRAPE_WORKER

#include <typeinfo>
#include <functional>
#include <string>
#include <map>
#include <boost/utility.hpp>
#include <cocaine/common.hpp>
#include <cocaine/api/stream.hpp>
#include <cocaine/asio/local.hpp>
#include <cocaine/asio/service.hpp>
#include <cocaine/asio/socket.hpp>
#include <cocaine/rpc/channel.hpp>
#include <cocaine/unique_id.hpp>

#include "logger.hpp"

class base_handler_t :
    public cocaine::api::stream_t,
    public boost::noncopyable
{
public:
    virtual
    void
    invoke(const std::string& event,
           std::shared_ptr<cocaine::api::stream_t>) = 0;
};

template<class AppT>
class handler_t :
    public base_handler_t
{
public:
    typedef AppT application_type;

public:
    handler_t(application_type& a) :
        app(a)
    {
        // pass
    }

protected:
    application_type& app;
};

struct bad_factory_exception :
    std::exception
{
    // pass
};

class base_factory_t {
public:
    virtual
    std::shared_ptr<base_handler_t>
    make_handler() = 0;
};

class application_t;

template<class HandlerT>
class handler_factory_t :
    public base_factory_t
{
    friend class application_t;

    typedef typename HandlerT::application_type application_type;

public:
    handler_factory_t() :
        m_app(nullptr)
    {
        // pass
    }

    std::shared_ptr<base_handler_t>
    make_handler()
    {
        if (m_app) {
            return std::shared_ptr<base_handler_t>(new HandlerT(*m_app));
        } else {
            throw bad_factory_exception();
        }
    }

protected:
    void
    set_application(application_type *a) {
        m_app = a;
    }

protected:
    application_type *m_app;
};


class function_handler_t :
    public base_handler_t
{
public:
    typedef std::function<std::string(const std::string&, const std::vector<std::string>&)>
            function_type;
public:
    function_handler_t(function_type f) :
        m_func(f)
    {
        // pass
    }

    void
    invoke(const std::string& event,
           std::shared_ptr<cocaine::api::stream_t> response)
    {
        m_response = response;
        m_event = event;
    }

    void
    write(const char *chunk,
         size_t size)
    {
        m_input.push_back(std::string(chunk, size));
    }

    void
    close() {
        std::string result = m_func(m_event, m_input);
        m_response->write(result.data(), result.size());
        m_response->close();
    }

    void
    error(cocaine::error_code code,
          const std::string& message)
    {
        // pass
    }

private:
    function_type m_func;
    std::vector<std::string> m_input;
    std::string m_event;
    std::shared_ptr<cocaine::api::stream_t> m_response;
};

template<class AppT>
class method_factory_t :
    public base_factory_t
{
    friend class application_t;

    typedef AppT application_type;
    typedef std::function<std::string(AppT*, const std::string&, const std::vector<std::string>&)>
            method_type;
public:
    method_factory_t(method_type f) :
        m_func(f),
        m_app(nullptr)
    {
        // pass
    }

    std::shared_ptr<base_handler_t>
    make_handler()
    {
        if (m_app) {
            return std::shared_ptr<base_handler_t>(
                new function_handler_t(std::bind(m_func,
                                                 m_app,
                                                 std::placeholders::_1,
                                                 std::placeholders::_2))
            );
        } else {
            throw bad_factory_exception();
        }
    }

protected:
    void
    set_application(application_type *a) {
        m_app = a;
    }

protected:
    method_type m_func;
    application_type *m_app;
};

class function_factory_t :
    public base_factory_t
{
public:
    function_factory_t(function_handler_t::function_type f) :
        m_func(f)
    {
        // pass
    }

    std::shared_ptr<base_handler_t>
    make_handler()
    {
        return std::shared_ptr<base_handler_t>(new function_handler_t(m_func));
    }

private:
    function_handler_t::function_type m_func;
};
//
//template<class MethodT, class ObjectT>
//std::shared_ptr<base_factory_t>
//method_factory(MethodT method,
//               ObjectT *object)
//{
//    return std::shared_ptr<base_factory_t>(
//        new function_factory_t(std::bind(method,
//                                         object,
//                                         std::placeholders::_1,
//                                         std::placeholders::_2))
//    );
//}

class worker_t;

class application_t {
    friend class worker_t;
    typedef std::map<std::string, std::shared_ptr<base_factory_t>>
            handlers_map;
public:
    virtual
    ~application_t()
    {
        // pass
    }

    virtual
    std::shared_ptr<base_handler_t>
    invoke(const std::string& event,
           std::shared_ptr<cocaine::api::stream_t> response);

    const std::string&
    name() const {
        return m_name;
    }

protected:
    virtual
    void
    on(const std::string& event,
       std::shared_ptr<base_factory_t> factory);

    template<class HandlerT, template<class> class FactoryT = handler_factory_t>
    void
    on(const std::string& event,
       const FactoryT<HandlerT>& factory = FactoryT<HandlerT>());

    virtual
    void
    on_unregistered(std::shared_ptr<base_factory_t> factory);

    template<class HandlerT, template<class> class FactoryT = handler_factory_t>
    void
    on_unregistered(const FactoryT<HandlerT>& factory = FactoryT<HandlerT>());

    virtual
    void
    initialize(const std::string& name,
               std::shared_ptr<cocaine::logger::logger_t> logger);

private:
    std::string m_name;
    handlers_map m_handlers;
    std::shared_ptr<base_factory_t> m_default_handler;
    std::shared_ptr<cocaine::logger::log_t> m_log;
};

class worker_t :
    public boost::noncopyable
{
    struct io_pair_t {
        std::shared_ptr<cocaine::api::stream_t> upstream;
        std::shared_ptr<cocaine::api::stream_t> downstream;
    };

    typedef std::map<uint64_t, io_pair_t> stream_map_t;

public:
    worker_t(const std::string& name,
             const std::string& uuid);

    ~worker_t();

    void
    run();

    template<class AppT>
    void
    add(const std::string& name, const AppT& a);

    template<class Event, typename... Args>
    void
    send(Args&&... args);

private:
    void
    on_message(const cocaine::io::message_t& message);

    void
    on_heartbeat(ev::timer&, int);

    void
    on_disown(ev::timer&, int);

    void
    terminate(int code,
              const std::string& reason);

private:
    const cocaine::unique_id_t m_id;
    cocaine::io::service_t m_service;
    ev::timer m_heartbeat_timer,
              m_disown_timer;
    std::shared_ptr<cocaine::logger::log_t> m_log;
    std::shared_ptr<cocaine::io::channel<cocaine::io::socket<cocaine::io::local>>> m_channel;

    std::string m_app_name;
    std::shared_ptr<application_t> m_application;

    stream_map_t m_streams;
};

template<class Event, typename... Args>
void
worker_t::send(Args&&... args) {
    m_channel->wr->write<Event>(std::forward<Args>(args)...);
}

template<class AppT>
void
worker_t::add(const std::string& name, const AppT& a) {
    if (name == m_app_name) {
        m_application.reset(new AppT(a));

        auto logger = std::shared_ptr<cocaine::logger::logger_t>(
            new cocaine::logger::remote_t("remote", Json::Value(), m_service));
        m_application->initialize(name, logger);

    }
}

template<class HandlerT, template<class> class FactoryT>
void
application_t::on(const std::string& event,
                  const FactoryT<HandlerT>& factory)
{
    FactoryT<HandlerT> *new_factory = new FactoryT<HandlerT>(factory);
    new_factory->set_application(dynamic_cast<typename FactoryT<HandlerT>::application_type*>(this));
    this->on(event, std::shared_ptr<base_factory_t>(new_factory));
}

template<class HandlerT, template<class> class FactoryT>
void
application_t::on_unregistered(const FactoryT<HandlerT>& factory) {
    FactoryT<HandlerT> *new_factory = new FactoryT<HandlerT>(factory);
    new_factory->set_application(dynamic_cast<typename FactoryT<HandlerT>::application_type*>(this));
    this->on_unregistered(std::shared_ptr<base_factory_t>(new_factory));
}

#endif // COCAINE_GRAPE_WORKER
