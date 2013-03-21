/*
    Copyright (c) 2011-2012 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2012 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef COCAINE_REMOTE_LOGGER_HPP
#define COCAINE_REMOTE_LOGGER_HPP

#include <cocaine/rpc/encoder.hpp>
#include <cocaine/common.hpp>
#include <cocaine/json.hpp>
#include <cocaine/format.hpp>

#define COCAINE_LOG(log, level, ...) \
    if(log->verbosity() >= level) log->emit(level, __VA_ARGS__);

#define COCAINE_LOG_DEBUG(log, ...) \
    COCAINE_LOG(log, logging::debug, __VA_ARGS__)

#define COCAINE_LOG_INFO(log, ...) \
    COCAINE_LOG(log, logging::info, __VA_ARGS__)

#define COCAINE_LOG_WARNING(log, ...) \
    COCAINE_LOG(log, logging::warning, __VA_ARGS__)

#define COCAINE_LOG_ERROR(log, ...) \
    COCAINE_LOG(log, logging::error, __VA_ARGS__)

namespace cocaine { namespace logger {

struct logger_t {
    virtual
    ~logger_t() {
        // Empty.
    }

    virtual
    logging::priorities
    verbosity() const = 0;

    virtual
    void
    emit(logging::priorities priority,
         const std::string& source,
         const std::string& message) = 0;
};

struct log_t {
    log_t(std::shared_ptr<logger_t> logger,
          const std::string& source) :
        m_logger(logger),
        m_source(source)
    {
        // pass
    }

    logging::priorities
    verbosity() const {
        return m_logger->verbosity();
    }

    template<typename... Args>
    void
    emit(logging::priorities level,
         const std::string& format,
         const Args&... args)
    {
        m_logger->emit(level, m_source, cocaine::format(format, args...));
    }

    void
    emit(logging::priorities level,
         const std::string& message)
    {
        m_logger->emit(level, m_source, message);
    }

private:
    std::shared_ptr<logger_t> m_logger;

    // The name of this log, to be used as the logging source.
    const std::string m_source;
};

class remote_t:
    public logger_t
{
public:
    remote_t(const std::string& name,
             const Json::Value& args,
             io::service_t& service);

    virtual
    logging::priorities
    verbosity() const {
        return logging::priorities::debug;
    }

    virtual
    void
    emit(logging::priorities priority,
         const std::string& source,
         const std::string& message);

private:
    io::encoder<io::writable_stream<io::socket<io::tcp>>> m_encoder;
};

}} // namespace cocaine::logger

#endif
