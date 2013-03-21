#include "worker.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <boost/program_options.hpp>
#include <msgpack.hpp>
#include <crypto++/cryptlib.h>
#include <crypto++/sha.h>

class App1 :
    public application_t
{
    class on_event1 :
        public handler_t<App1>
    {
    public:
        on_event1(App1& a) :
            handler_t<App1>(a),
            m_state(0)
        {
            // pass
        }

        void
        invoke(const std::string& event,
               std::shared_ptr<cocaine::api::stream_t> response)
        {
            m_state = 1;
            m_response = response;
            m_event_name = event;
        }

        void
        write(const char *chunk,
             size_t size)
        {
            msgpack::sbuffer buffer;
            msgpack::packer<msgpack::sbuffer> pk(&buffer);
            pk.pack_map(2);
            pk.pack(std::string("code"));
            pk.pack(200);

            std::vector<std::pair<std::string, std::string>> headers;
            headers.push_back(std::make_pair(std::string("Content-Type"), std::string("text/plain")));

            pk.pack(std::string("headers"));
            pk.pack(headers);

            m_response->write(buffer.data(), buffer.size());

            std::string msg = "jvbherjhvhejrhbgvehjrbgvhjerbvgherbvgherb";
            byte abd[CryptoPP::SHA512::DIGESTSIZE];
            CryptoPP::SHA512().CalculateDigest(abd, (const byte*)msg.data(), msg.size());

            std::string body = "<html><body>" + m_event_name + "</body></html>";

            msgpack::pack(buffer, body);
            m_response->write(buffer.data(), buffer.size());
            m_response->close();
        }

        void
        close() {
            m_state = 0;
        }

        void
        error(cocaine::error_code code,
              const std::string& message)
        {
            // pass
        }
    private:
        int m_state;
        std::shared_ptr<cocaine::api::stream_t> m_response;
        std::string m_event_name;
    };

    class on_exit :
        public handler_t<App1>
    {
    public:
        on_exit(App1& a) :
            handler_t<App1>(a),
            m_state(0)
        {
            // pass
        }

        void
        invoke(const std::string& event,
               std::shared_ptr<cocaine::api::stream_t> response)
        {
            m_state = 1;
            m_response = response;
        }

        void
        write(const char *chunk,
             size_t size)
        {
            m_response->close();
            exit(0);
        }

        void
        close() {
            m_state = 0;
        }

        void
        error(cocaine::error_code code,
              const std::string& message)
        {
            // pass
        }
    private:
        int m_state;
        std::shared_ptr<cocaine::api::stream_t> m_response;
    };

public:
    App1()
    {
        on<on_event1>("event1");
        // on("event2", method_factory(&App1::on_event2, this));
        on("event2", method_factory_t<App1>(&App1::on_event2));
        on<on_exit>("exit");
    }

    std::string on_event2(const std::string& event,
                          const std::vector<std::string>& input)
    {
        return "on_event2:" + event;
    }
};

std::shared_ptr<worker_t>
make_worker(int argc,
            char *argv[])
{
    using namespace boost::program_options;

    variables_map vm;

    options_description options;
    options.add_options()
        ("app", value<std::string>())
        ("uuid", value<std::string>());

    try {
        command_line_parser parser(argc, argv);
        parser.options(options);
        parser.allow_unregistered();
        store(parser.run(), vm);
        notify(vm);
    } catch(const ambiguous_option& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    try {
        return std::make_shared<worker_t>(vm["app"].as<std::string>(),
                                          vm["uuid"].as<std::string>());
    } catch(const std::exception& e) {
        std::cerr << cocaine::format("ERROR: unable to start the worker - %s", e.what()) << std::endl;
        exit(EXIT_FAILURE);
    }
}

int
main(int argc, char *argv[])
{
    std::shared_ptr<worker_t> worker = make_worker(argc, argv);

    worker->add("app1", App1());

    worker->run();

    return 0;
}
