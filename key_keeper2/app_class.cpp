#include "app_class.h"
#include <syslog.h>
#include <fstream>
#include <stdexcept>
#include <util/string_utils.h>
#ifdef USE_DB
#include <orm/domain_object.h>
#endif

using namespace std;

char SyslogAppender::process_name[100];

const std::string get_process_name()
{
    const std::string file_name = "/proc/self/cmdline";
    std::ifstream inp(file_name.c_str());
    if (!inp)
        throw std::runtime_error("can't open file: " + file_name);
    std::string cmdline;
    std::copy(std::istream_iterator<char>(inp),
              std::istream_iterator<char>(),
              std::back_inserter(cmdline));
    size_t pos = cmdline.find('\0');
    if (std::string::npos != pos)
        cmdline = cmdline.substr(0, pos);
    pos = cmdline.rfind('/');
    if (std::string::npos != pos)
        cmdline = cmdline.substr(pos + 1);
    return cmdline;
}

const std::string fmt_string_escape(const std::string &s)
{
    std::string r;
    r.reserve(s.size() * 2);
    for (size_t pos = 0; pos < s.size(); ++pos) {
        if ('%' == s[pos])
            r += '%';
        r += s[pos];
    }
    return r;
}

int SyslogAppender::log_level_to_syslog(int log_level)
{
    switch (log_level) {
    case Yb::ll_CRITICAL:
        return LOG_CRIT;
    case Yb::ll_ERROR:
        return LOG_ERR;
    case Yb::ll_WARNING:
        return LOG_WARNING;
    case Yb::ll_INFO:
        return LOG_INFO;
    case Yb::ll_DEBUG:
    case Yb::ll_TRACE:
        return LOG_DEBUG;
    }
    return LOG_DEBUG;
}

SyslogAppender::SyslogAppender()
{
    std::string process = get_process_name();
    strncpy(process_name, process.c_str(), sizeof(process_name));
    process_name[sizeof(process_name) - 1] = 0;
    ::openlog(process_name, LOG_NDELAY | LOG_PID, LOG_USER);
}

SyslogAppender::~SyslogAppender()
{
    ::closelog();
}

void SyslogAppender::really_append(const Yb::LogRecord &rec)
{
    int priority = log_level_to_syslog(rec.get_level());
    std::ostringstream msg;
    msg << "T" << rec.get_tid() << " "
        << rec.get_component() << ": "
        << rec.get_msg();
    ::syslog(priority, fmt_string_escape(msg.str()).c_str());
}

void SyslogAppender::append(const Yb::LogRecord &rec)
{
    Yb::ScopedLock lk(appender_mutex_);
    int target_level = Yb::ll_ALL;
    LogLevelMap::iterator it = log_levels_.find(rec.get_component());
    if (log_levels_.end() != it)
        target_level = it->second;
    if (rec.get_level() <= target_level) {
        really_append(rec);
    }
}

int SyslogAppender::get_level(const std::string &name)
{
    Yb::ScopedLock lk(appender_mutex_);
    LogLevelMap::iterator it = log_levels_.find(name);
    if (log_levels_.end() == it)
        return Yb::ll_ALL;
    return it->second;
}

void SyslogAppender::set_level(const std::string &name, int level)
{
    Yb::ScopedLock lk(appender_mutex_);
    if (name.size() >= 2 && name.substr(name.size() - 2) == ".*")
    {
        std::string prefix = name.substr(0, name.size() - 1);
        LogLevelMap::iterator it = log_levels_.begin(),
                              it_end = log_levels_.end();
        for (; it != it_end; ++it)
            if (it->first.substr(0, prefix.size()) == prefix)
                it->second = level;
        std::string prefix0 = name.substr(0, name.size() - 2);
        log_levels_[prefix0] = level;
    }
    else {
        LogLevelMap::iterator it = log_levels_.find(name);
        if (log_levels_.end() == it)
            log_levels_[name] = level;
        else
            it->second = level;
    }
}

static int decode_log_level(const string &log_level0)
{
    using Yb::StrUtils::str_to_lower;
    const string log_level = str_to_lower(log_level0);
    if (log_level.empty())
        return Yb::ll_DEBUG;
    if (log_level == "critical" || log_level == "cri" || log_level == "crit")
        return Yb::ll_CRITICAL;
    if (log_level == "error"    || log_level == "err" || log_level == "erro")
        return Yb::ll_ERROR;
    if (log_level == "warning"  || log_level == "wrn" || log_level == "warn")
        return Yb::ll_WARNING;
    if (log_level == "info"     || log_level == "inf")
        return Yb::ll_INFO;
    if (log_level == "debug"    || log_level == "dbg" || log_level == "debg")
        return Yb::ll_DEBUG;
    if (log_level == "trace"    || log_level == "trc" || log_level == "trac")
        return Yb::ll_TRACE;
    throw runtime_error("invalid log level: " + log_level);
}

static const string encode_log_level(int level)
{
    Yb::LogRecord r(level, "__xxx__", "yyy");
    return string(r.get_level_name());
}

void App::init_log(const string &log_name, const string &log_level)
{
    using Yb::StrUtils::split_str_by_chars;
    using Yb::StrUtils::trim_trailing_space;
    if (!log_.get()) {
        using Yb::StrUtils::str_to_lower;
        if ("syslog" == str_to_lower(log_name)) {
            appender_.reset(new SyslogAppender());
        }
        else {
            file_stream_.reset(new ofstream(log_name.data(), ios::app));
            if (file_stream_->fail())
                throw runtime_error("can't open logfile: " + log_name);
            appender_.reset(new Yb::LogAppender(*file_stream_));
        }
        log_.reset(new Yb::Logger(appender_.get()));
#ifdef VAULT_DEBUG_API
        info("Application started. Debug methods are enabled.");
#else
        info("Application started.");
#endif
        info("Setting level " + encode_log_level(decode_log_level(log_level))
                + " for root logger");
        log_->set_level(decode_log_level(log_level));
        const string log_level = cfg().get_value("LogLevel");
        vector<string> target_levels;
        split_str_by_chars(log_level, ",", target_levels);
        auto i = target_levels.begin(), iend = target_levels.end();
        for (; i != iend; ++i) {
            auto &target_level = *i;
            vector<string> parts;
            split_str_by_chars(target_level, ":", parts, 2);
            if (parts.size() == 2) {
                auto target = trim_trailing_space(parts[0]);
                auto level = trim_trailing_space(parts[1]);
                info("Setting level " + encode_log_level(decode_log_level(level))
                        + " for log target " + target);
                log_->get_logger(target)->set_level(decode_log_level(level));
            }
        }
    }
}

const string App::get_db_url()
{
    const string type = cfg().get_value("DbBackend/@type");
    const string db = cfg().get_value("DbBackend/DB");
    const string user = cfg().get_value("DbBackend/User");
    const string pass = cfg().get_value("DbBackend/Pass");
    if (type == "mysql+soci") {
        const string host = cfg().get_value("DbBackend/Host");
        const int port = cfg().get_value_as_int("DbBackend/Port");
        return type + "://user=" + user +
            " pass=" + pass +
            " host=" + host +
            " port=" + Yb::to_string(port) +
            " service=" + db;
    }
    if (type == "mysql+odbc") {
        return type + "://" + user + ":" + pass + "@" + db;
    }
    throw runtime_error("invalid DbBackend configuration");
}

void App::init_engine(const string &db_name)
{
#ifdef USE_DB
    if (!engine_.get()) {
        Yb::ILogger::Ptr yb_logger(new_logger("yb").release());
        Yb::init_schema();
        auto_ptr<Yb::SqlPool> pool(
                new Yb::SqlPool(
                    YB_POOL_MAX_SIZE, YB_POOL_IDLE_TIME,
                    YB_POOL_MONITOR_SLEEP, yb_logger.get(), false));
        Yb::SqlSource src(get_db_url());
        src[_T("&id")] = db_name;
        pool->add_source(src);
        engine_.reset(new Yb::Engine(Yb::Engine::READ_WRITE, pool, WIDEN(db_name)));
        engine_->set_echo(true);
        engine_->set_logger(yb_logger);
    }
#endif
}

void App::init(IConfig::Ptr config)
{
    config_.reset(config.release());
    init_log(cfg().get_value("Log"), cfg().get_value("Log/@level"));
#ifdef USE_DB
    init_engine(cfg().get_value("DbBackend/@id"));
#endif
}

IConfig &App::cfg()
{
    return *config_.get();
}

App::~App()
{
    engine_.reset(NULL);
    if (log_.get()) {
        info("log finished");
        Yb::LogAppender *appender = dynamic_cast<Yb::LogAppender *> (
                appender_.get());
        if (appender)
            appender->flush();
        if (file_stream_.get())
            file_stream_->close();
    }
    log_.reset(NULL);
    appender_.reset(NULL);
    file_stream_.reset(NULL);
}

Yb::Engine &App::get_engine()
{
    if (!engine_.get())
        throw runtime_error("engine not created");
    return *engine_.get();
}

auto_ptr<Yb::Session> App::new_session()
{
#ifdef USE_DB
    return auto_ptr<Yb::Session>(
            new Yb::Session(Yb::theSchema(), get_engine()));
#else
    return auto_ptr<Yb::Session>();
#endif
}

Yb::ILogger::Ptr App::new_logger(const string &name)
{
    if (!log_.get())
        throw runtime_error("log not open");
    return log_->new_logger(name);
}

Yb::ILogger::Ptr App::get_logger(const std::string &name)
{
    if (!log_.get())
        throw runtime_error("log not opened");
    return log_->get_logger(name);
}

int App::get_level()
{
    if (!log_.get())
        throw runtime_error("log not opened");
    return log_->get_level();
}

void App::set_level(int level)
{
    if (!log_.get())
        throw runtime_error("log not opened");
    log_->set_level(level);
}

void App::log(int level, const string &msg)
{
    if (log_.get())
        log_->log(level, msg);
}

const string App::get_name() const
{
    if (!log_.get())
        return string();
    return log_->get_name();
}

// vim:ts=4:sts=4:sw=4:et:
